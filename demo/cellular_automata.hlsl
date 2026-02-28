#define GROUP_DIMENSIONS_X 32
#define GROUP_DIMENSIONS_Y 32
#define GROUP_DIMENSIONS_Z 1
#define GROUP_DIMENSIONS_NUMTHREAD GROUP_DIMENSIONS_X, GROUP_DIMENSIONS_Y, GROUP_DIMENSIONS_Z
#define _group_dimensions uint3(GROUP_DIMENSIONS_NUMTHREAD)

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> input_grid;

[[vk::binding(1, 0)]]
RWStructuredBuffer<uint> output_grid;

[[vk::binding(2, 0)]]
cbuffer Uniforms {
    uint u_x;
    uint u_y;
    uint u_v;
    uint3 u_dispatch_dimensions;
};

[[vk::binding(3, 0)]]
[[vk::image_format("bgra8")]]
[[spv::format("Bgra8")]]
RWTexture2D<float4> output_image;

[numthreads(GROUP_DIMENSIONS_NUMTHREAD)]
void cellular_automata(
    uint3 group_id : SV_GroupID,
    uint3 group_thread_id : SV_GroupThreadID,
    uint3 thread_coordinate_id : SV_DispatchThreadID
) {
    uint2 output_image_dimensions;
    output_image.GetDimensions(output_image_dimensions.x, output_image_dimensions.y);

    uint id = group_thread_id.x + _group_dimensions.x * (group_thread_id.y + _group_dimensions.y * (group_thread_id.z + _group_dimensions.z * (group_id.x + u_dispatch_dimensions.x * (group_id.y + u_dispatch_dimensions.y * group_id.z))));
    output_image[thread_coordinate_id.xy] = (id % 2 == 0) ? float4(1.0, 0.0, 1.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
}
