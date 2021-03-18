__kernel void createMap(
    __global float *out_map_x, short map_x_step, short map_x_offset, short map_rows, short map_cols,
    __global float *out_map_y, short map_y_step, short map_y_offset,
    float src_center_x, float src_center_y, float src_focal_x, float src_focal_y,
    float map_center_x, float map_center_y, float map_focal_x, float map_focal_y,
    float rot00, float rot01, float rot02,
    float rot10, float rot11, float rot12,
    float rot20, float rot21, float rot22
) {
    short map_x = get_global_id(0);
    short map_y = get_global_id(1);

    if (map_x < map_cols && map_y < map_rows) {
        // Find the location vector of the mapped pixel
        float3 vector_identity = {
            (map_x - map_center_x) / map_focal_x,
            (map_y - map_center_y) / map_focal_y,
            1
        };

        // Apply the desired rotation
        float3 rotation_row_0 = {rot00, rot01, rot02};
        float3 rotation_row_1 = {rot10, rot11, rot12};
        float3 rotation_row_2 = {rot20, rot21, rot22};

        float3 vector_rotated = {
            dot(rotation_row_0, vector_identity),
            dot(rotation_row_1, vector_identity),
            dot(rotation_row_2, vector_identity)
        };

        float2 coordinates_rotated = {
            vector_rotated[0] / vector_rotated[2],
            vector_rotated[1] / vector_rotated[2]
        };

        // Find the correction to apply to the radius to bring it into the fisheye model
        float radius_identity = length(coordinates_rotated);
        float fisheye_correction = atan(radius_identity) / radius_identity;

        // Find the location of this pixel in the maps
        __global float *map_x_pixel = ((__global void * ) out_map_x) +
            mad24(map_y, map_x_step, mad24(map_x, sizeof(float), map_x_offset));
        __global float *map_y_pixel = ((__global void * ) out_map_y) +
            mad24(map_y, map_y_step, mad24(map_x, sizeof(float), map_y_offset));

        // Write calculated values to the maps
        *map_x_pixel = src_center_x + coordinates_rotated[0] * fisheye_correction * src_focal_x;
        *map_y_pixel = src_center_y + coordinates_rotated[1] * fisheye_correction * src_focal_y;
    }
}
