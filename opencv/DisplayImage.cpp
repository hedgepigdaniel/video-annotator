#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/core/va_intel.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/buffer.h>
    #include <libavutil/hwcontext_opencl.h>
    #include <libavutil/pixdesc.h>
    #include <va/va_drm.h>
}

#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>

using namespace std;
using namespace cv;
using namespace std::chrono;

#define DRM_DEVICE_PATH "/dev/dri/renderD128"

#define ERR_STRING_BUF_SIZE 50
char err_string[ERR_STRING_BUF_SIZE];

char* errString(int errnum) {
    return av_make_error_string(err_string, ERR_STRING_BUF_SIZE, errnum);
}

void printOpenClInfo(void) {
    ocl::Context context;
    if (!context.create(ocl::Device::TYPE_ALL))
    {
        cout << "Failed creating the context..." << endl;
        //return;
    }

    cout << context.ndevices() << " CPU devices are detected." << endl; //This bit provides an overview of the OpenCL devices you have in your computer
    for (uint i = 0; i < context.ndevices(); i++)
    {
        ocl::Device device = context.device(i);
        cout << "name:              " << device.name() << endl;
        cout << "available:         " << device.available() << endl;
        cout << "imageSupport:      " << device.imageSupport() << endl;
        cout << "OpenCL_C_Version:  " << device.OpenCL_C_Version() << endl;
        cout << endl;
    }
}

void initOpenClFromVaapi () {
    int drm_device = -1;
    drm_device = open(DRM_DEVICE_PATH, O_RDWR|O_CLOEXEC);
    std::cout << "mtp:: drm_device= " << drm_device << std::endl;
    if(drm_device < 0)
    {
        std::cout << "mtp:: GPU device not found...Exiting" << std::endl;
        exit(-1);
    }
    VADisplay vaDisplay = vaGetDisplayDRM(drm_device);
    if(!vaDisplay)
       std::cout << "mtp:: Not a valid display" << std::endl;
    close(drm_device);
    if(!vaDisplay)
        std::cout << "mtp:: Not a valid display::2nd" << std::endl;
    va_intel::ocl::initializeContextFromVA (vaDisplay, true);
    printOpenClInfo();
}

typedef struct _IoContext {
    char *filename;
    AVBufferRef *vaapi_device_ctx;
    AVBufferRef *ocl_device_ctx;
    AVFormatContext *format_ctx;
    int video_stream;
    AVCodecContext *decoder_ctx;
} IoContext;

IoContext * ioContext_alloc() {
    IoContext *ctx = (IoContext *) malloc(sizeof (IoContext));
    ctx->filename = NULL;
    ctx->vaapi_device_ctx = NULL;
    ctx->ocl_device_ctx = NULL;
    ctx->format_ctx = NULL;
    ctx->video_stream = -1;
    ctx->decoder_ctx = NULL;
    return ctx;
}

static enum AVPixelFormat get_vaapi_format(
    AVCodecContext *ctx,
    const enum AVPixelFormat *pix_fmts
) {
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_VAAPI)
            return *p;
    }
    fprintf(stderr, "Unable to decode this file using VA-API.\n");
    return AV_PIX_FMT_NONE;
}

int open_input_file (IoContext *ctx) {
    int ret = 0;
    AVStream *video = NULL;
    AVCodec *decoder = NULL;

    ret = avformat_open_input(&ctx->format_ctx, ctx->filename, NULL, NULL);
    if (ret) {
        cout << "Failed to open input file!\n";
        return ret;
    }

    ret = avformat_find_stream_info(ctx->format_ctx, NULL);
    if (ret) {
        cout << "Failed to find input stream information!\n";
        return ret;
    }

    ret = av_find_best_stream(
        ctx->format_ctx,
        AVMEDIA_TYPE_VIDEO,
        -1,
        -1,
        &decoder,
        0
    );
    if (ret < 0) {
        cout << "Failed to find a video stream in the input file!\n";
        return ret;
    }

    ctx->video_stream = ret;
    ret = 0;

    ctx->decoder_ctx = avcodec_alloc_context3(decoder);
    if (!ctx->decoder_ctx) {
        return AVERROR(ENOMEM);
    }
    video = ctx->format_ctx->streams[ctx->video_stream];
    ret = avcodec_parameters_to_context(ctx->decoder_ctx, video->codecpar);
    if (ret < 0) {
        fprintf(
            stderr,
            "avcodec_parameters_to_context error. Error code: %d\n",
            ret
        );
        return ret;
    }
    ctx->decoder_ctx->hw_device_ctx = av_buffer_ref(ctx->vaapi_device_ctx);
    if (!ctx->decoder_ctx->hw_device_ctx) {
        fprintf(stderr, "A hardware device reference create failed.\n");
        return AVERROR(ENOMEM);
    }
    ctx->decoder_ctx->get_format = get_vaapi_format;
    ret = avcodec_open2(ctx->decoder_ctx, decoder, NULL);
    if (ret < 0) {
        fprintf(
            stderr,
            "Failed to open codec for decoding. Error code: %d\n",
            ret
        );
    }

    return ret;
}

void close_input_file(IoContext *ctx) {
    avformat_close_input(&ctx->format_ctx);
    avcodec_free_context(&ctx->decoder_ctx);
    av_buffer_unref(&ctx->vaapi_device_ctx);
}

enum ReadState {
    BREAK,
    ERROR,
    INPUT_ENDED,
    AWAITING_INPUT_PACKETS,
    AWAITING_INPUT_FRAMES,
};

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

int process_frame(IoContext *ioContext, AVFrame *frame) {
    int ret = 0;
    AVFrame *ocl_frame = av_frame_alloc();
    AVBufferRef *ocl_hw_frames_ctx = NULL;
    if (ocl_frame == NULL) {
        return AVERROR(ENOMEM);
    }
    ret = av_hwframe_ctx_create_derived(
        &ocl_hw_frames_ctx,
        AV_PIX_FMT_OPENCL,
        ioContext->ocl_device_ctx,
        frame->hw_frames_ctx,
        AV_HWFRAME_MAP_DIRECT
    );
    if (ret) {
        fprintf(
            stderr,
            "Failed to map hwframes context to OpenCL: %s\n",
            errString(ret)
        );
        return ret;
    }
    ocl_frame->hw_frames_ctx = av_buffer_ref(ocl_hw_frames_ctx);
    ocl_frame->format = AV_PIX_FMT_OPENCL;
    if (ocl_frame->hw_frames_ctx == NULL) {
        return AVERROR(ENOMEM);
    }
    ret = av_hwframe_map(ocl_frame, frame, AV_HWFRAME_MAP_READ);
    if (ret) {
        fprintf(stderr, "Failed to map hardware frames: %s\n", errString(ret));
    }

    // fprintf(stderr, "frame format: %s\n", av_get_pix_fmt_name((AVPixelFormat) frame->format));
    // fprintf(stderr, "ocl_frame format: %s\n", av_get_pix_fmt_name((AVPixelFormat) ocl_frame->format));
    // fprintf(stderr, "stack: %p\n", &ret);
    // fprintf(stderr, "data: %p %p %p\n", ocl_frame->data[0], ocl_frame->data[1], ocl_frame->data[2]);

    UMat frame_mat;
    UMat frame_bgr;
    ret = convert_ocl_images_to_nv12_umat(
        (cl_mem) ocl_frame->data[0],
        (cl_mem) ocl_frame->data[1],
        frame_mat
    );
    cvtColor(frame_mat, frame_bgr, COLOR_YUV2BGR_NV12);
    if (ret) {
        std::cerr << "Failed to convert OpenCL images to opencv\n";
        return ret;
    }
    // imshow("fast", frame_bgr);
    // waitKey(1);

    av_frame_unref(ocl_frame);
    return ret;
}

int init_opencv_opencl_from_hwctx(AVBufferRef *ocl_device_ctx) {
    int ret = 0;
    AVHWDeviceContext *ocl_hw_device_ctx;
    AVOpenCLDeviceContext *ocl_device_ocl_ctx;
    ocl_hw_device_ctx = (AVHWDeviceContext *) ocl_device_ctx->data;
    ocl_device_ocl_ctx = (AVOpenCLDeviceContext *) ocl_hw_device_ctx->hwctx;
    cl_context_properties *props = NULL;
    cl_platform_id platform = NULL;
    char *platform_name = NULL;
    size_t param_value_size = 0;
    ret = clGetContextInfo(
        ocl_device_ocl_ctx->context,
        CL_CONTEXT_PROPERTIES,
        0,
        NULL,
        &param_value_size
    );
    if (ret != CL_SUCCESS) {
        std::cerr << "clGetContextInfo failed to get props size\n";
        return ret;
    }
    if (param_value_size == 0) {
        std::cerr << "clGetContextInfo returned size 0\n";
        return 1;
    }
    props = (cl_context_properties *) malloc(param_value_size);
    if (props == NULL) {
        std::cerr << "Failed to alloc props 0\n";
        return AVERROR(ENOMEM);
    }
    ret = clGetContextInfo(
        ocl_device_ocl_ctx->context,
        CL_CONTEXT_PROPERTIES,
        param_value_size,
        props,
        NULL
    );
    if (ret != CL_SUCCESS) {
        std::cerr << "clGetContextInfo failed\n";
        return ret;
    }
    for (int i = 0; props[i] != 0; i = i + 2) {
        if (props[i] == CL_CONTEXT_PLATFORM) {
            platform = (cl_platform_id) props[i + 1];
        }
    }
    if (platform == NULL) {
        std::cerr << "Failed to find platform in cl context props\n";
        return 1;
    }

    ret = clGetPlatformInfo(
        platform,
        CL_PLATFORM_NAME,
        0,
        NULL,
        &param_value_size
    );

    if (ret != CL_SUCCESS) {
        std::cerr << "clGetPlatformInfo failed to get platform name size\n";
        return ret;
    }
    if (param_value_size == 0) {
        std::cerr << "clGetPlatformInfo returned 0 size for name\n";
        return 1;
    }
    platform_name = (char *) malloc(param_value_size);
    if (platform_name == NULL) {
        std::cerr << "Failed to malloc platform_name\n";
        return AVERROR(ENOMEM);
    }
    ret = clGetPlatformInfo(
        platform,
        CL_PLATFORM_NAME,
        param_value_size,
        platform_name,
        NULL
    );
    if (ret != CL_SUCCESS) {
        std::cerr << "clGetPlatformInfo failed\n";
        return ret;
    }

    std::cerr << "Initialising OpenCV OpenCL context with platform \"" <<
        platform_name <<
        "\"\n";

    ocl::Context::getDefault(false);
    ocl::attachContext(
        platform_name,
        platform,
        ocl_device_ocl_ctx->context,
        ocl_device_ocl_ctx->device_id
    );
    return 0;
}

int main (int argc, char* argv[])
{
    if (argc < 2) {
        std::cout << "\n\tUsage: " << argv[0] << " <filename>\n\n";
        return 1;
    }
    int ret = 0;
    IoContext *ioContext = ioContext_alloc();
    AVPacket packet;
    ioContext->filename = argv[1];

    ret = av_hwdevice_ctx_create(
        &ioContext->vaapi_device_ctx,
        AV_HWDEVICE_TYPE_VAAPI,
        NULL,
        NULL,
        0
    );
    if (ret < 0) {
        fprintf(stderr, "Failed to create a VAAPI device. Error code: %d\n", ret);
        return ret;
    }

    ret = av_hwdevice_ctx_create_derived(
        &ioContext->ocl_device_ctx,
        AV_HWDEVICE_TYPE_OPENCL,
        ioContext->vaapi_device_ctx,
        0
    );
    if (ret < 0) {
        fprintf(
            stderr,
            "Failed to map VAAPI device to OpenCL device. Error code: %s\n",
            errString(ret)
        );
        return ret;
    }

    ret = init_opencv_opencl_from_hwctx(ioContext->ocl_device_ctx);
    if (ret) {
        std::cerr << "Failed to initialise OpenCV OpenCL from libavcodec hw ctx\n";
        return ret;
    }

    ret = open_input_file (ioContext);
    if (ret) {
        cout << "Failed to open input file\n";
        return ret;
    }

    ReadState state = AWAITING_INPUT_PACKETS;
    AVFrame *frame;
    int n_decoded_frames = 0;
    int frames_in_interval = 0;
    steady_clock::time_point fps_interval_start = steady_clock::now();

    while (state != BREAK) {
        switch (state) {
            case ERROR: {
                fprintf(stderr, "Error %d\n", ret);
                state = BREAK;
                break;
            }
            case INPUT_ENDED: {
                fprintf(stderr, "Processing complete!\n");
                state = BREAK;
                break;
            }
            case AWAITING_INPUT_PACKETS: {
                ret = av_read_frame(ioContext->format_ctx, &packet);
                if (ret < 0) {
                    state = INPUT_ENDED;
                    break;
                }

                // AVStream *stream = ioContext->format_ctx->streams[packet.stream_index];
                if (packet.stream_index == ioContext->video_stream) {
                    // fprintf(
                    //     stderr,
                    //     "Read video packet at %lf seconds\n",
                    //     1.0 * packet.pts * stream->time_base.num / stream->time_base.den
                    // );
                    ret = avcodec_send_packet(ioContext->decoder_ctx, &packet);
                    if (ret) {
                        state = ERROR;
                        break;
                    }
                    av_packet_unref(&packet);
                } else {
                    // fprintf(
                    //     stderr,
                    //     "Ignoring non-video packet at %lf seconds\n",
                    //     1.0 * packet.pts * stream->time_base.den / stream->time_base.num
                    // );
                }
                state = AWAITING_INPUT_FRAMES;
                break;
            }
            case AWAITING_INPUT_FRAMES: {
                frame = av_frame_alloc();
                ret = avcodec_receive_frame(ioContext->decoder_ctx, frame);
                if (!ret) {
                    ret = process_frame(ioContext, frame);
                    if (ret) {
                        fprintf(stderr, "Failed to process frame\n");
                        state = ERROR;
                        break;
                    }

                    // success
                    n_decoded_frames++;
                    frames_in_interval++;
                    milliseconds ms_in_interval = duration_cast<milliseconds>(
                        steady_clock::now() - fps_interval_start
                    );
                    if (ms_in_interval.count() > 1000) {
                        fprintf(
                            stderr,
                            "Decoded %d frames (%.0lffps)\n",
                            n_decoded_frames,
                            frames_in_interval * 1000.0 / ms_in_interval.count()
                        );
                        frames_in_interval = 0;
                        fps_interval_start = steady_clock::now();
                    }
                } else if (ret == AVERROR(EAGAIN)) {
                    state = AWAITING_INPUT_PACKETS;
                } else if (state == AVERROR_EOF) {
                    state = INPUT_ENDED;
                } else if (ret) {
                    state = ERROR;
                }
                av_frame_free(&frame);
                break;
            }
            case BREAK: {
                break;
            }
        }
    }

    close_input_file(ioContext);
    return 0;
}