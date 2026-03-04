#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_ARB_gpu_shader_int64 : enable

layout(std430, set = 0, binding = 0) readonly buffer InputGridBuffer {
    uint64_t elements[];
} input_grid;

layout(std430, set = 0, binding = 1) buffer OutputGridBuffer {
    uint64_t elements[];
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

uint64_t _load(uint id) {
    return input_grid.elements[nonuniformEXT(id)];
}

uint64_t load(ivec2 pos) {
    if (pos.x < 0 || pos.x >= uniforms.width || pos.y < 0 || pos.y >= uniforms.height) {
        return 0;
    }

    return _load(pos.x + pos.y * uniforms.width);
}

void store(uint id, uint64_t value) {
    output_grid.elements[nonuniformEXT(id)] = value;
}

vec3 intensity_color(float t) {
    return vec3(mix(mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 0.0), min(t*2.0, 1.0)), mix(vec3(0.0, 1.0, 1.0), vec3(0.0, 0.0, 1.0), max(t*2.0-1.0, 0.0)), step(0.5, t)));
}

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void cellular_automata() {
    uint id = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * (gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.z * (gl_NumWorkGroups.x * gl_WorkGroupSize.x) * (gl_NumWorkGroups.y * gl_WorkGroupSize.y);

    ivec2 coord = ivec2(id % uniforms.width, id / uniforms.width);
    ivec2 pos = coord;

    uint64_t value = load(pos);
    uint big_cursor_size = uniforms.v >> 24;
    bool near_big_cursor = int(gl_GlobalInvocationID.x) <= int(uniforms.x + big_cursor_size * uniforms.width / 512 * uniforms.height / uniforms.width) && int(gl_GlobalInvocationID.x) >= int(uniforms.x - big_cursor_size * uniforms.width / 512 * uniforms.height / uniforms.width) && int(gl_GlobalInvocationID.y) <= max(int(uniforms.y + big_cursor_size * uniforms.height / 512), 0) && int(gl_GlobalInvocationID.y) >= max(int(uniforms.y - big_cursor_size * uniforms.height / 512), 0);
    if ((uniforms.v & 0x110) == 0x110 && near_big_cursor) {
        value |= 0x80000000L;
    } else if ((uniforms.v & 0x10) != 0 && gl_GlobalInvocationID.xy == uvec2(uniforms.x, uniforms.y)) {
        value |= 0x80000000L;
    } else if ((uniforms.v & 0x100) != 0 && near_big_cursor && uniforms.tick % 2 == 0) {
        value = 0x00000000L;
    }
    
    if ((uniforms.v & 0x80) != 0) {
        value = 0x00;
    } else if ((uniforms.v & 0x400) != 0) {
        value &= 0xffffffff;
    }
    
    vec3 color = vec3(-1.0);
    if ((uniforms.v & 0x20) == 0) {
        if ((uniforms.v & 0x200) != 0) {
            value <<= 1;
        } else {
            value >>= 1;

            uint64_t a0 = load(pos + ivec2(-1, -1));
            uint64_t a1 = load(pos + ivec2( 0, -1));
            uint64_t a2 = load(pos + ivec2( 1, -1));
            uint64_t b0 = load(pos + ivec2(-1,  0));
            uint64_t b2 = load(pos + ivec2( 1,  0));
            uint64_t c0 = load(pos + ivec2(-1,  1));
            uint64_t c1 = load(pos + ivec2( 0,  1));
            uint64_t c2 = load(pos + ivec2( 1,  1));

            uint count = uint(
                (a0 >> 31) +
                (a1 >> 31) +
                (a2 >> 31) +
                (b0 >> 31) +
                (b2 >> 31) +
                (c0 >> 31) +
                (c1 >> 31) +
                (c2 >> 31)
            );

            bool was_living = (value & 0x40000000L) != 0;
            if (count == 3 || count == 2 && was_living) {
                value |= 0x80000000L;
            }
        }
    }

    store(id, value);
    int msbLower = findMSB(uint(value & 0xffffffff));
    uint msb = max(0, msbLower);
    color = intensity_color(1.0 - pow(msb / 32.0, 1.1)) * vec3(1.0, 1.0, msb / 32.0);

    if (near_big_cursor) {
        color = vec3(1.0, 1.0, 1.0) - color;
    }

    if (color != vec3(-1.0)) {
        imageStore(output_image, coord, vec4(color, 1.0));
    }
}
