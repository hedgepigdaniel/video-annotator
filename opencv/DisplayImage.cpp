#include <CL/opencl.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

#include <iostream>
#include <math.h>

#include "hw_init.hpp"
#include "AvFrameSourceFileVaapi.hpp"
#include "AvFrameSourceMapOpenCl.hpp"
#include "FrameSourceFfmpegOpenCl.hpp"
#include "FrameSourceWarp.hpp"

using namespace std;
using namespace cv;
using namespace std::chrono;

#define DRM_DEVICE_PATH "/dev/dri/renderD128"

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
    FrameSource *ffmpeg_source = new FrameSourceFfmpegOpenCl(opencl_source);

    // Known field of view of the camera
    // int v_fov_s = 94.4 * M_PI / 180;
    // int h_fov_s = 122.6 * M_PI / 180;
    int d_fov = 149.2 * M_PI / 180;
    FrameSource *warped_source = new FrameSourceWarp(ffmpeg_source, d_fov);

    UMat frame;
    while (true) {
        try {
            cerr << "read frame\n";
            frame = warped_source->pull_frame();
            imshow("fast", frame);
            waitKey(1);
        } catch (int err) {
            if (err == EOF) {
                break;
            }
            throw err;
        }
    }

    delete warped_source;
    delete ffmpeg_source;
    delete opencl_source;
    delete vaapi_source;
    av_buffer_unref(&opencl_device_ctx);
    av_buffer_unref(&vaapi_device_ctx);
    return 0;
}