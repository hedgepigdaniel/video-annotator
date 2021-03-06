#include <CL/opencl.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/va_intel.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/video/tracking.hpp>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/buffer.h>
    #include <libavutil/pixdesc.h>
    #include <va/va_drm.h>
    #include <gpmf-parser/GPMF_parser.h>
}

#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <math.h>
#include <deque>

#include "utils.hpp"
#include "hw_init.hpp"
#include "AvFrameSourceFileVaapi.hpp"
#include "AvFrameSourceMapOpenCl.hpp"
#include "FrameSourceFfmpegOpenCl.hpp"
#include "Warper.hpp"

using namespace std;
using namespace cv;
using namespace std::chrono;

#define DRM_DEVICE_PATH "/dev/dri/renderD128"


// void close_input_file(IoContext *ctx) {
//     avformat_close_input(&ctx->format_ctx);
//     avcodec_free_context(&ctx->decoder_ctx);
//     av_buffer_unref(&ctx->vaapi_device_ctx);
// }

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

int main (int argc, char* argv[])
{
    if (argc < 2) {
        std::cout << "\n\tUsage: " << argv[0] << " <filename>\n\n";
        return 1;
    }

    if (!is_vaapi_and_opencl_supported()) {
        cerr << "Error: FFmpeg was built without VAAPI or OpenCL support\n";
        return 2;
    }

    // Set up compatible hardware contexts
    AVBufferRef *vaapi_device_ctx = create_vaapi_context();
    AVBufferRef *opencl_device_ctx = create_opencl_context_from_vaapi(vaapi_device_ctx);
    init_opencv_from_opencl_context(opencl_device_ctx);

    AvFrameSource *vaapi_source = new AvFrameSourceFileVaapi(argv[1], vaapi_device_ctx);
    AvFrameSource *opencl_source = new AvFrameSourceMapOpenCl(vaapi_source, opencl_device_ctx);
    FrameSource *umat_source = new FrameSourceFfmpegOpenCl(opencl_source);

    UMat frame;
    while (true) {
        try {
            cerr << "read frame\n";
            frame = umat_source->pull_frame();
            imshow("fast", frame);
            waitKey(1);
        } catch (int err) {
            if (err == EOF) {
                break;
            }
            throw err;
        }
    }

    // ret = init_opencv_opencl_from_hwctx(ioContext->ocl_device_ctx);
    // if (ret) {
    //     std::cerr << "Failed to initialise OpenCV OpenCL from libavcodec hw ctx\n";
    //     return ret;
    // }


    // ReadState state = AWAITING_INPUT_PACKETS;
    // AVFrame *frame;
    // int n_decoded_frames = 0;
    // int n_demuxed_frames = 0;
    // int frames_in_interval = 0;
    // steady_clock::time_point fps_interval_start = steady_clock::now();

    // while (state != BREAK) {
    //     switch (state) {
    //         case ERROR: {
    //             fprintf(stderr, "Error %d\n", ret);
    //             state = BREAK;
    //             break;
    //         }
    //         case INPUT_ENDED: {
    //             fprintf(stderr, "Processing complete!\n");
    //             state = BREAK;
    //             break;
    //         }
    //         case AWAITING_INPUT_PACKETS: {
    //             ret = av_read_frame(ioContext->format_ctx, &packet);
    //             if (ret < 0) {
    //                 state = INPUT_ENDED;
    //                 break;
    //             }

    //             AVStream *stream = ioContext->format_ctx->streams[packet.stream_index];
    //             if (packet.stream_index == ioContext->video_stream) {
    //                 std::cerr << "Video frame number: " << n_demuxed_frames << "\n";
    //                 n_demuxed_frames++;
    //                 std::cerr << "Video frame presentation: " <<
    //                     1.0 * packet.pts * stream->time_base.num / stream->time_base.den <<
    //                    " (" << packet.pts << " * " << stream->time_base.num <<
    //                     " / " << stream->time_base.den << ")\n";
    //                 std::cerr << "Video frame duration: " <<
    //                     1.0 * packet.duration * stream->time_base.num / stream->time_base.den <<
    //                    " (" << packet.duration << " * " << stream->time_base.num <<
    //                     " / " << stream->time_base.den << ")\n";
    //                 // fprintf(
    //                 //     stderr,
    //                 //     "Read video packet at %lf seconds\n",
    //                 //     1.0 * packet.pts * stream->time_base.num / stream->time_base.den
    //                 // );
    //                 // ret = avcodec_send_packet(ioContext->decoder_ctx, &packet);
    //                 // if (ret) {
    //                 //     state = ERROR;
    //                 // }
    //             } else if (packet.stream_index == ioContext->gpmf_stream) {
    //                 double pt_timestamp = 1.0 * packet.pts * stream->time_base.num / stream->time_base.den;
    //                 double pt_duration = 1.0 * packet.duration * stream->time_base.num / stream->time_base.den;
    //                 std::cerr << "GPMF frame presentation: " <<
    //                     1.0 * packet.pts * stream->time_base.num / stream->time_base.den <<
    //                     " (" << packet.pts << " * " << stream->time_base.num <<
    //                     " / " << stream->time_base.den << ")\n";
    //                 std::cerr << "GPMF frame duration: " <<
    //                     1.0 * packet.duration * stream->time_base.num / stream->time_base.den <<
    //                    " (" << packet.duration << " * " << stream->time_base.num <<
    //                     " / " << stream->time_base.den << ")\n";
    //                 ret = process_sensor_data(
    //                     ioContext,
    //                     (uint32_t *) packet.data,
    //                     packet.size,
    //                     pt_timestamp,
    //                     pt_duration
    //                 );
    //                 if (ret) {
    //                     state = ERROR;
    //                 }
    //             } else {
    //                 // Ignore audio packets, etc
    //             }

    //             av_packet_unref(&packet);
    //             if (state == AWAITING_INPUT_PACKETS) {
    //                 state = AWAITING_INPUT_FRAMES;
    //             }
    //             break;
    //         }
    //         case AWAITING_INPUT_FRAMES: {
    //             frame = av_frame_alloc();
    //             ret = avcodec_receive_frame(ioContext->decoder_ctx, frame);
    //             if (!ret) {
    //                 ret = process_frame(ioContext, frame);
    //                 if (ret) {
    //                     fprintf(stderr, "Failed to process frame\n");
    //                     state = ERROR;
    //                     break;
    //                 }

    //                 // success
    //                 n_decoded_frames++;
    //                 frames_in_interval++;
    //                 milliseconds ms_in_interval = duration_cast<milliseconds>(
    //                     steady_clock::now() - fps_interval_start
    //                 );
    //                 if (ms_in_interval.count() > 1000) {
    //                     fprintf(
    //                         stderr,
    //                         "Decoded %d frames (%.0lffps)\n",
    //                         n_decoded_frames,
    //                         frames_in_interval * 1000.0 / ms_in_interval.count()
    //                     );
    //                     frames_in_interval = 0;
    //                     fps_interval_start = steady_clock::now();
    //                 }
    //             } else if (ret == AVERROR(EAGAIN)) {
    //                 state = AWAITING_INPUT_PACKETS;
    //             } else if (state == AVERROR_EOF) {
    //                 state = INPUT_ENDED;
    //             } else if (ret) {
    //                 state = ERROR;
    //             }
    //             av_frame_free(&frame);
    //             break;
    //         }
    //         case BREAK: {
    //             break;
    //         }
    //     }
    // }

    // close_input_file(ioContext);

    av_buffer_unref(&opencl_device_ctx);
    av_buffer_unref(&vaapi_device_ctx);
    return 0;
}