extern "C" {
    #include <gpmf-parser/GPMF_parser.h>
}

class GyroFrame {
    double start_ts;
    double end_ts;
    double roll;
    double pitch;
    double yaw;
};

// GPMF_ERR read_sample_data(GPMF_stream gs_stream, GPMF_SampleType sample_type, uint32_t *samples, uint32_t *elements, void **buffer, int *buffer_size) {
//     int ret;
//     *samples = GPMF_Repeat(&gs_stream);
//     *elements = GPMF_ElementsInStruct(&gs_stream);
//     *buffer_size = GPMF_ScaledDataSize(&gs_stream, sample_type);
//     *buffer = malloc(*buffer_size);
//     if (*buffer == NULL) {
//         std::cerr << "Failed to allocate memory for GPMF data\n";
//         return GPMF_ERROR_MEMORY;
//     }
//     ret = GPMF_ScaledData(&gs_stream, *buffer, *buffer_size, 0, *samples, sample_type);
//     if (ret != GPMF_OK) {
//         std::cerr << "Failed to read GPMF samples: " << ret << "\n";
//         free(*buffer);
//         *buffer = NULL;
//     }
//     return ret;
// }

// int process_sensor_data(
//     IoContext *ioContext,
//     uint32_t *buffer,
//     int size,
//     double pkt_timestamp,
//     double pkt_duration
// ) {
//     GPMF_stream gs_stream;
//     int ret;
//     uint32_t samples;
//     uint32_t elements;
//     // double est_timestamp;
//     void *temp_buffer;
//     int temp_buffer_size;
//     GyroFrame gyro_frame;

//     ret = GPMF_Init(&gs_stream, buffer, size);
//     if (ret != GPMF_OK) {
//         std::cerr << "Failed to parse GPMF packet: " << ret << "\n";
//         return -1;
//     }
//     do
// 	{
//         for (uint32_t i = 0; i < GPMF_NestLevel(&gs_stream); i++) {
//             std::cerr << "\t";
//         }
//         fprintf(stderr, "GPMF key: %c%c%c%c\n", PRINTF_4CC(GPMF_Key(&gs_stream)));
// 		switch(GPMF_Key(&gs_stream)) {
//             // case STR2FOURCC("ACCL"):
//             //     ret = read_sample_data(gs_stream, GPMF_TYPE_DOUBLE, &samples, &elements, &temp_buffer, &temp_buffer_size);
//             //     if (ret != GPMF_OK) {
//             //         return ret;
//             //     }
//             //     if (elements != 3) {
//             //         std::cerr << "Unexpected number of elements for ACCL data: " << elements << "\n";
//             //         free(temp_buffer);
//             //         return -1;
//             //     }
//             //     std::cerr << "Found ACCL data with " << samples << " samples\n";
//             //     for (uint32_t sample = 0; sample < samples; sample++) {
//             //         est_timestamp = pkt_timestamp + pkt_duration * sample / samples;
//             //         std::cerr << "ACCL " << est_timestamp << ":";
//             //         for (uint32_t element = 0; element < elements; element++) {
//             //             std::cerr << ((double *) temp_buffer)[sample * elements + element] << ", ";
//             //         }
//             //         std::cerr << "\n";
//             //     }
//             //     free(temp_buffer);
//             //     break;

//             case STR2FOURCC("GYRO"):
//                 ret = read_sample_data(gs_stream, GPMF_TYPE_DOUBLE, &samples, &elements, &temp_buffer, &temp_buffer_size);
//                 if (ret != GPMF_OK) {
//                     return ret;
//                 }
//                 if (elements != 3) {
//                     std::cerr << "Unexpected number of elements for GYRO data: " << elements << "\n";
//                     free(temp_buffer);
//                     return -1;
//                 }
//                 std::cerr << "Found GYRO data with " << samples << " samples\n";
//                 for (uint32_t sample = 0; sample < samples; sample++) {
//                     gyro_frame.start_ts = pkt_timestamp + pkt_duration * sample / samples;
//                     gyro_frame.end_ts = gyro_frame.start_ts + pkt_duration / samples;
//                     gyro_frame.roll = ((double *) temp_buffer)[sample * elements + 0];
//                     gyro_frame.pitch = ((double *) temp_buffer)[sample * elements + 1];
//                     gyro_frame.yaw = ((double *) temp_buffer)[sample * elements + 2];
//                     // std::cerr << "GYRO " << gyro_frame.start_ts << " - " <<
//                     //     gyro_frame.end_ts << ": " << gyro_frame.yaw << ", " <<
//                     //     gyro_frame.pitch << ", " << gyro_frame.roll << "\n";
//                     ioContext->frames_ctx.gyro_frames.push_back(gyro_frame);
//                 }
//                 free(temp_buffer);
//                 break;
//         }
//         ret = GPMF_Next(&gs_stream, GPMF_RECURSE_LEVELS);
// 	} while (ret == GPMF_OK);

//     if (ret != GPMF_ERROR_BUFFER_END && ret != GPMF_ERROR_LAST) {
//         std::cerr << "Failed to parse GPMF node: " << ret << "\n";
//         return -1;
//     }
//     return 0;
// }