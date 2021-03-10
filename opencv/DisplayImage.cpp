#include <CL/opencl.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

#include <iostream>
#include <math.h>

#include "hw_init.hpp"
#include "AvFrameSourceProfile.hpp"
#include "AvFrameSourceFileVaapi.hpp"
#include "AvFrameSourceMapOpenCl.hpp"
#include "FrameSourceProfile.hpp"
#include "FrameSourceFfmpegOpenCl.hpp"
#include "FrameSourceWarp.hpp"

using namespace std;
using namespace cv;

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
    auto av_buffer_deleter = [](AVBufferRef *ref) { av_buffer_unref(&ref); };
    auto vaapi_device_ctx = shared_ptr<AVBufferRef>(create_vaapi_context(), av_buffer_deleter);
    auto opencl_device_ctx = shared_ptr<AVBufferRef>(
        create_opencl_context_from_vaapi(vaapi_device_ctx.get()),
        av_buffer_deleter
    );
    init_opencv_from_opencl_context(opencl_device_ctx.get());

    auto vaapi_source = make_shared<AvFrameSourceProfile>(
        make_unique<AvFrameSourceFileVaapi>(argv[1], vaapi_device_ctx),
        "ffmpeg-vaapi"
    );
    auto opencl_source = make_shared<AvFrameSourceProfile>(
        make_unique<AvFrameSourceMapOpenCl>(vaapi_source, opencl_device_ctx),
        "ffmpeg-opencl"
    );
    auto ffmpeg_source = make_shared<FrameSourceProfile>(
        std::make_unique<FrameSourceFfmpegOpenCl>(opencl_source),
        "opencv-mapped"
    );
    auto warped_source = make_shared<FrameSourceProfile>(
        make_unique<FrameSourceWarp>(ffmpeg_source, GOPRO_H4B_WIDE169_MEASURED),
        "opencv-warped"
    );

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

    return 0;
}