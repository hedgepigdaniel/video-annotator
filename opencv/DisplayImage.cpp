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
#include "AvFrameSourceOpenCl.hpp"
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

/*
// Convert OpenCL image2d_t memory to UMat
*/
int convert_ocl_images_to_nv12_umat(cl_mem cl_luma, cl_mem cl_chroma, UMat& dst)
{
    int ret = 0;

    cl_image_format luma_fmt = { 0, 0 };
    cl_image_format chroma_fmt = { 0, 0 };
    ret = clGetImageInfo(cl_luma, CL_IMAGE_FORMAT, sizeof(cl_image_format), &luma_fmt, 0);
    if (ret) {
        std::cerr << "Failed to get luma image format: " << ret << "\n";
        return ret;
    }
    ret = clGetImageInfo(cl_chroma, CL_IMAGE_FORMAT, sizeof(cl_image_format), &chroma_fmt, 0);
    if (ret) {
        std::cerr << "Failed to get chroma image format: " << ret << "\n";
        return ret;
    }
    if (luma_fmt.image_channel_data_type != CL_UNORM_INT8 ||
    chroma_fmt.image_channel_data_type != CL_UNORM_INT8) {
        std::cerr << "Wrong image format\n";
        return 1;
    }
    if (luma_fmt.image_channel_order != CL_R ||
    chroma_fmt.image_channel_order != CL_RG) {
        std::cerr << "Wrong image channel order\n";
        return 1;
    }

    size_t luma_w = 0;
    size_t luma_h = 0;
    size_t chroma_w = 0;
    size_t chroma_h = 0;

    ret |= clGetImageInfo(cl_luma, CL_IMAGE_WIDTH, sizeof(size_t), &luma_w, 0);
    ret |= clGetImageInfo(cl_luma, CL_IMAGE_HEIGHT, sizeof(size_t), &luma_h, 0);
    ret |= clGetImageInfo(cl_chroma, CL_IMAGE_WIDTH, sizeof(size_t), &chroma_w, 0);
    ret |= clGetImageInfo(cl_chroma, CL_IMAGE_HEIGHT, sizeof(size_t), &chroma_h, 0);
    if (ret) {
        std::cerr << "Failed to get image info: " << ret << "\n";
        return ret;
    }

    if (luma_w != 2 * chroma_w || luma_h != 2 *chroma_h ) {
        std::cerr << "Mismatched image dimensions\n";
        return 1;
    }

    dst.create(luma_h + chroma_h, luma_w, CV_8U);
    cl_mem dst_buffer = (cl_mem) dst.handle(ACCESS_READ);
    cl_command_queue queue = (cl_command_queue) ocl::Queue::getDefault().ptr();
    size_t src_origin[3] = { 0, 0, 0 };
    size_t luma_region[3] = { luma_w, luma_h, 1 };
    size_t chroma_region[3] = { chroma_w, chroma_h * 2, 1 };
    ret = clEnqueueCopyImageToBuffer(
        queue,
        cl_luma,
        dst_buffer,
        src_origin,
        luma_region,
        0,
        0,
        NULL,
        NULL
    );
    ret |= clEnqueueCopyImageToBuffer(
        queue,
        cl_chroma,
        dst_buffer,
        src_origin,
        chroma_region,
        luma_w * luma_h * 1,
        0,
        NULL,
        NULL
    );
    ret |= clFinish(queue);
    if (ret) {
        std::cerr << "Failed to enqueue image copy to buffer\n";
        return ret;
    }

    return ret;
}

// int process_frame(IoContext *ioContext, AVFrame *ocl_frame) {
//     int ret;
//     UMat frame_mat;
//     ret = convert_ocl_images_to_nv12_umat(
//         (cl_mem) ocl_frame->data[0],
//         (cl_mem) ocl_frame->data[1],
//         frame_mat
//     );
//     if (ret) {
//         std::cerr << "Failed to convert OpenCL images to opencv\n";
//         return ret;
//     }
//     ret = process_frame_mat(&ioContext->frames_ctx, frame_mat);
//     if (ret) {
//         std::cerr << "Failed to process frame matrix\n";
//         return ret;
//     }

//     av_frame_unref(ocl_frame);
//     return ret;
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

    if (!AvFrameSourceOpenCl::is_supported()) {
        cerr << "Error: FFmpeg was built without VAAPI or OpenCL support\n";
        return -1;
    }
    AvFrameSourceOpenCl av_frame_source = AvFrameSourceOpenCl(argv[1]);

    AVFrame *frame;
    while (true) {
        try {
            frame = av_frame_source.pull_frame();
            av_frame_free(&frame);
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
    return 0;
}