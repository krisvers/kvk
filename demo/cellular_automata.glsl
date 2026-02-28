#version 460
#extension GL_ARB_separate_shader_objects : enable

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
} uniforms;

layout(set = 0, binding = 3, rgba8) uniform image2D output_image;

uint _load(uint id) {
    return input_grid.elements[id];
}

uint load(ivec2 pos) {
    if (pos.x < 0 || pos.x >= uniforms.width || pos.y < 0 || pos.y >= uniforms.height) {
        return 0;
    }

    return _load(pos.x + pos.y * uniforms.width);
}

void store(uint id, uint value) {
    output_grid.elements[id] = value;
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
        if (cell.flow_strength == 0) {
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
        cell.wall = true;
        return cell;
    }

    return decompress(_load(id));
}

Cell load_cell(ivec2 pos) {
    if (pos.x < 0 || pos.x >= uniforms.width || pos.y < 0 || pos.y >= uniforms.height) {
        Cell cell;
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

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void cellular_automata() {
    uint id = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * (gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.z * (gl_NumWorkGroups.x * gl_WorkGroupSize.x) * (gl_NumWorkGroups.y * gl_WorkGroupSize.y);

    ivec2 coord = ivec2(id % uniforms.width, id / uniforms.width);
    ivec2 pos = coord;
    uint value = load(pos);
    Cell cell = decompress(value);
    if ((uniforms.v & 0x10) != 0 && gl_GlobalInvocationID.xy == uvec2(uniforms.x, uniforms.y)) {
        float r1 = random(vec2(gl_GlobalInvocationID.xy * uniforms.tick));
        float r2 = random(vec2(gl_LocalInvocationID.xy * uniforms.tick * r1));

        cell.ground_height = 0x0;
        cell.water_level += 0x2;
        cell.flow_dir.x = 1;
        cell.flow_dir.y = -1;
        cell.flow_strength = 32;
    } else if ((uniforms.v & 0x20) == 0) {
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

        /*
        vec2 summed_flows = vec2(
            a0.flow_dir +
            a1.flow_dir +
            a2.flow_dir +
            b0.flow_dir +
            b2.flow_dir +
            c0.flow_dir +
            c1.flow_dir +
            c2.flow_dir
        );

        cell.flow_dir = ivec2(sign(summed_flows));// * step(0.0, abs(summed_flows))));
        cell.flow_strength = uint(length(summed_flows));

        cell.water_level = max(0, cell.water_level + int(
            dot(normalize(vec2(a0.flow_dir)), normalize(vec2( 1,  1))) * min(a0.flow_strength, a0.water_level) +
            dot(normalize(vec2(a1.flow_dir)), normalize(vec2( 0,  1))) * min(a1.flow_strength, a1.water_level) +
            dot(normalize(vec2(a2.flow_dir)), normalize(vec2(-1,  1))) * min(a2.flow_strength, a2.water_level) +
            dot(normalize(vec2(b0.flow_dir)), normalize(vec2( 1,  0))) * min(b0.flow_strength, b0.water_level) +
            dot(normalize(vec2(b2.flow_dir)), normalize(vec2(-1,  0))) * min(b2.flow_strength, b2.water_level) +
            dot(normalize(vec2(c0.flow_dir)), normalize(vec2( 1, -1))) * min(c0.flow_strength, c0.water_level) +
            dot(normalize(vec2(c1.flow_dir)), normalize(vec2( 0, -1))) * min(c1.flow_strength, c1.water_level) +
            dot(normalize(vec2(c2.flow_dir)), normalize(vec2(-1, -1))) * min(c2.flow_strength, c2.water_level)
        ));
        */

        vec2 flow =
            a0.water_level * normalize(vec2( 1,  1)) +
            a1.water_level *           vec2( 0,  1)  +
            a2.water_level * normalize(vec2(-1,  1)) +
            b0.water_level *           vec2( 1,  0)  +
            b2.water_level *           vec2(-1,  0)  +
            c0.water_level * normalize(vec2( 1, -1)) +
            c1.water_level *           vec2( 0, -1)  +
            c2.water_level * normalize(vec2(-1, -1));

        float diff =
            int(!a0.wall) * (cell.water_level - a0.water_level) +
            int(!a1.wall) * (cell.water_level - a1.water_level) +
            int(!a2.wall) * (cell.water_level - a2.water_level) +
            int(!b0.wall) * (cell.water_level - b0.water_level) +
            int(!b2.wall) * (cell.water_level - b2.water_level) +
            int(!c0.wall) * (cell.water_level - c0.water_level) +
            int(!c1.wall) * (cell.water_level - c1.water_level) +
            int(!c2.wall) * (cell.water_level - c2.water_level);

        float change = length(flow) / 8.0;
        cell.flow_dir = ivec2(sign(flow));
        cell.water_level = max(0, min(255, cell.water_level - int(diff * change)));
        //cell.water_level = uint(max(0, min(255, int(cell.water_level))));
        //store_cell(id, cell);
        //
        imageStore(output_image, coord, vec4(change, 0.0, cell.water_level / 255.0, 1.0));
    } else {
    }

    float t = 1 - cell.water_level / 255.0;
    store_cell(id, cell);
    //imageStore(output_image, coord, vec4(mix(mix(vec3(1.0, 0.0, 0.0), vec3(0.5, 1.0, 0.5), min(t*2.0, 1.0)), mix(vec3(0.5, 1.0, 0.5), vec3(0.0, 0.0, 1.0), max(t*2.0-1.0, 0.0)), step(0.5, t)), 1.0));
}
