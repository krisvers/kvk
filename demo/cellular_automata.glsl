#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require

#define VISUAL_MODE_FLOW_CHANGE_INTENSITY 1
#define VISUAL_MODE_COUNT_GREATER 2
#define VISUAL_MODE_COUNT_LESS 3
#define VISUAL_MODE_COUNT_WALL 4
#define VISUAL_MODE_FLOW_STRENGTH_INTENSITY 8
#define VISUAL_MODE_FLOW_DIRECTION 9
#define VISUAL_MODE_WATER_INTENSITY 0

struct Cell {
    uint ground_height; // : 8
    uint water_level;   // : 8
    ivec2 flow_dir;     // : 3
    uint flow_strength; // : 5
    bool wall;          // : 1
};

layout(std430, set = 0, binding = 0) readonly buffer InputGridBuffer {
    uint elements[];
} input_grid;

layout(std430, set = 0, binding = 1) buffer OutputGridBuffer {
    uint elements[];
} output_grid;

layout(std140, set = 0, binding = 2) uniform Uniforms {
    uint x;
    uint y;
    uint v;
    uint tick;
    uint bytes_per_cell;
    uint width;
    uint height;
    uint visual_mode;
} uniforms;

layout(set = 0, binding = 3, rgba8) uniform image2D output_image;

uint _load(uint id) {
    return input_grid.elements[nonuniformEXT(id)];
}

uint load(ivec2 pos) {
    if (pos.x < 0 || pos.x >= uniforms.width || pos.y < 0 || pos.y >= uniforms.height) {
        return 0;
    }

    return _load(pos.x + pos.y * uniforms.width);
}

void store(uint id, uint value) {
    output_grid.elements[nonuniformEXT(id)] = value;
}

Cell decompress(uint value) {
    Cell cell;
    cell.ground_height = value & 0xff;
    cell.water_level = (value >> 8) & 0xff;

    uint x = (value >> 16) & 0x1;
    uint y = (value >> 17) & 0x1;
    if ((value & (1 << 18)) == 0) {
        cell.flow_dir = ivec2(int(x << 1) - 1, int(y << 1) - 1);
    } else {
        cell.flow_dir = ivec2((1 - y) * (int(x << 1) - 1), y * (int(x << 1) - 1));
    }

    cell.flow_strength = int((value >> 19) & 0x3f);
    cell.wall = (value & (1 << 20)) != 0;
    return cell;
}

uint compress(Cell cell) {
    uint value = 0;
    value |= cell.ground_height & 0xff;
    value |= (cell.water_level & 0xff) << 8;
 
    uint d, x, y;
    if (abs(cell.flow_dir.x) == abs(cell.flow_dir.y)) {
        d = 0;
        if (cell.ground_height == 0) {
            x = 0;
            y = 0;
        } else {
            x = (cell.flow_dir.x + 1) >> 1;
            y = (cell.flow_dir.y + 1) >> 1;
        }
    } else {
        d = 1;
        x = (cell.flow_dir.x + cell.flow_dir.y + 1) >> 1;
        y = 1 - abs(cell.flow_dir.x);
    }

    value |= ((d << 2) | (y << 1) | x) << 16;
    value |= (cell.flow_strength & 0x3f) << 19;
    value |= uint(cell.wall) << 20;
    return value;
}

Cell load_cell(uint id) {
    if (id >= uniforms.width * uniforms.height) {
        Cell cell;
        cell.ground_height = 2;
        cell.wall = true;
        return cell;
    }

    return decompress(_load(id));
}

Cell load_cell(ivec2 pos) {
    if (pos.x < 0 || pos.x >= uniforms.width || pos.y < 0 || pos.y >= uniforms.height) {
        Cell cell;
        cell.ground_height = 2;
        cell.wall = true;
        return cell;
    }

    return decompress(load(pos));
}

void store_cell(uint id, Cell cell) {
    store(id, compress(cell));
}

float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898,78.233))) * 43758.5453123);
}

vec3 intensity_color(float t) {
    return vec3(mix(mix(vec3(1.0, 0.0, 0.0), vec3(0.5, 1.0, 0.5), min(t*2.0, 1.0)), mix(vec3(0.5, 1.0, 0.5), vec3(0.0, 0.0, 1.0), max(t*2.0-1.0, 0.0)), step(0.5, t)));
}

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void cellular_automata() {
    uint id = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * (gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.z * (gl_NumWorkGroups.x * gl_WorkGroupSize.x) * (gl_NumWorkGroups.y * gl_WorkGroupSize.y);

    ivec2 coord = ivec2(id % uniforms.width, id / uniforms.width);
    ivec2 pos = coord;

    uint value = load(pos);
    Cell cell = decompress(value);

    uint big_cursor_size = uniforms.v >> 24;
    bool near_big_cursor = gl_GlobalInvocationID.x < uniforms.x + big_cursor_size * uniforms.width / 1024 && gl_GlobalInvocationID.x >= uniforms.x - big_cursor_size * uniforms.width / 1024 && gl_GlobalInvocationID.y < max(uniforms.y + big_cursor_size * uniforms.height / 1024, 1) && gl_GlobalInvocationID.y >= max(uniforms.y - big_cursor_size * uniforms.height / 1024, 1);
    if ((uniforms.v & 0x110) == 0x110 && near_big_cursor) {
        cell.water_level = 255 - cell.water_level;
        cell.flow_dir.x = -1;
        cell.flow_dir.y = 1;
        cell.ground_height = 255;
    } else if ((uniforms.v & 0x10) != 0 && gl_GlobalInvocationID.xy == uvec2(uniforms.x, uniforms.y)) {
        cell.water_level = 255 - cell.water_level;
        cell.flow_dir.x = -1;
        cell.flow_dir.y = 1;
        cell.ground_height = 255;
    } else if ((uniforms.v & 0x100) != 0 && near_big_cursor) {
        cell.water_level = 0x0;
        cell.flow_dir.x = 0;
        cell.flow_dir.y = 0;
        cell.ground_height = 0;
    }
    
    if ((uniforms.v & 0x80) != 0) {
        cell.water_level = 0x0;
        cell.flow_dir.x = 1;
        cell.flow_dir.y = -1;
        cell.ground_height = 0;
    }
    
    vec3 color = vec3(-1.0);
    if ((uniforms.v & 0x20) == 0) {
        Cell a0, a1, a2;
        Cell b0,     b2;
        Cell c0, c1, c2;

        a0 = load_cell(pos + ivec2(-1, -1));
        a1 = load_cell(pos + ivec2( 0, -1));
        a2 = load_cell(pos + ivec2( 1, -1));

        b0 = load_cell(pos + ivec2(-1,  0));
        b2 = load_cell(pos + ivec2( 1,  0));

        c0 = load_cell(pos + ivec2(-1,  1));
        c1 = load_cell(pos + ivec2( 0,  1));
        c2 = load_cell(pos + ivec2( 1,  1));

        float diff =
            int(!a0.wall) * (cell.water_level - a0.water_level) +
            int(!a1.wall) * (cell.water_level - a1.water_level) +
            int(!a2.wall) * (cell.water_level - a2.water_level) +
            int(!b0.wall) * (cell.water_level - b0.water_level) +
            int(!b2.wall) * (cell.water_level - b2.water_level) +
            int(!c0.wall) * (cell.water_level - c0.water_level) +
            int(!c1.wall) * (cell.water_level - c1.water_level) +
            int(!c2.wall) * (cell.water_level - c2.water_level);

        uint count_wall = 
            uint(a0.wall) +
            uint(a1.wall) +
            uint(a2.wall) +
            uint(b0.wall) +
            uint(b2.wall) +
            uint(c0.wall) +
            uint(c1.wall) +
            uint(c2.wall);

        uint count_greater =
            uint(a0.water_level > cell.water_level) +
            uint(a1.water_level > cell.water_level) +
            uint(a2.water_level > cell.water_level) +
            uint(b0.water_level > cell.water_level) +
            uint(b2.water_level > cell.water_level) +
            uint(c0.water_level > cell.water_level) +
            uint(c1.water_level > cell.water_level) +
            uint(c2.water_level > cell.water_level) - count_wall;

        uint count_less =
            uint(a0.water_level < cell.water_level) +
            uint(a1.water_level < cell.water_level) +
            uint(a2.water_level < cell.water_level) +
            uint(b0.water_level < cell.water_level) +
            uint(b2.water_level < cell.water_level) +
            uint(c0.water_level < cell.water_level) +
            uint(c1.water_level < cell.water_level) +
            uint(c2.water_level < cell.water_level) - count_wall;

        uint surrounding_sum = 
            a0.water_level +
            a1.water_level +
            a2.water_level +
            b0.water_level +
            b2.water_level +
            c0.water_level +
            c1.water_level +
            c2.water_level;

        float average_water_level = surrounding_sum / 8.0;

        uint surrounding_ground_height =
            a0.ground_height +
            a1.ground_height +
            a2.ground_height +
            b0.ground_height +
            b2.ground_height +
            c0.ground_height +
            c1.ground_height +
            c2.ground_height;

        float average_ground_height = surrounding_ground_height / (8.0 - count_wall);

        vec2 flow =
            (a0.wall ? average_water_level : a0.water_level) * normalize(vec2( 1,  1)) +
            (a1.wall ? average_water_level : a1.water_level) *           vec2( 0,  1)  +
            (a2.wall ? average_water_level : a2.water_level) * normalize(vec2(-1,  1)) +
            (b0.wall ? average_water_level : b0.water_level) *           vec2( 1,  0)  +
            (b2.wall ? average_water_level : b2.water_level) *           vec2(-1,  0)  +
            (c0.wall ? average_water_level : c0.water_level) * normalize(vec2( 1, -1)) +
            (c1.wall ? average_water_level : c1.water_level) *           vec2( 0, -1)  +
            (c2.wall ? average_water_level : c2.water_level) * normalize(vec2(-1, -1));

        float change = length(flow) / float(8 - count_wall);
        cell.flow_dir = ivec2(sign(flow));
        cell.water_level = uint(pow(max(0, min(255, cell.water_level + max(int(change) + count_greater, -count_less))), 0.99 * cell.ground_height / 32.0));
        cell.ground_height = uint((pow((cell.ground_height - 4) / 16.0, 1.1) * 16.0 + pow(2 * (average_ground_height - 4.1), 0.52)) / 1.9);
        //cell.water_level = uint(max(0, min(255, int(cell.water_level))));
        //store_cell(id, cell);

        if (uniforms.visual_mode == VISUAL_MODE_FLOW_CHANGE_INTENSITY) {
            float t = change;
            color = intensity_color(t);
        } else if (uniforms.visual_mode == VISUAL_MODE_COUNT_GREATER) {
            color = intensity_color(count_greater / 8.0);
        } else if (uniforms.visual_mode == VISUAL_MODE_COUNT_LESS) {
            color = intensity_color(count_less / 8.0);
        } else if (uniforms.visual_mode == VISUAL_MODE_COUNT_WALL) {
            color = intensity_color(count_wall / 8.0);
        }
    }

    store_cell(id, cell);    
    if (uniforms.visual_mode == VISUAL_MODE_WATER_INTENSITY) {
        float t = 1 - cell.water_level / 255.0;
        color = intensity_color(t);
    } else if (uniforms.visual_mode == VISUAL_MODE_FLOW_DIRECTION) {
        color = vec3((vec2(cell.flow_dir) + 1.0) / 2.0, 0.0);
    } else if (uniforms.visual_mode == VISUAL_MODE_FLOW_STRENGTH_INTENSITY) {
        float t = 1 - cell.ground_height / 196.0;
        color = intensity_color(t);
    }

    if (gl_GlobalInvocationID.x < uniforms.x + uniforms.width / 256 && gl_GlobalInvocationID.x >= uniforms.x - uniforms.width / 256 && gl_GlobalInvocationID.y < uniforms.y + uniforms.height / 256 && gl_GlobalInvocationID.y >= uniforms.y - uniforms.height / 256) {
        color = vec3(1.0, 1.0, 1.0) - color;
    }

    if (color != vec3(-1.0)) {
        imageStore(output_image, coord, vec4(color, 1.0));
    }
}
