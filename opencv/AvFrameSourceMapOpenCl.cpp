#include "AvFrameSourceMapOpenCl.hpp"

#include <iostream>

#include "utils.hpp"

using namespace std;

AvFrameSourceMapOpenCl::AvFrameSourceMapOpenCl(
    std::shared_ptr<AvFrameSource> source,
    std::shared_ptr<AVBufferRef> ocl_device_ctx
) {
    this->source = source;
    this->ocl_device_ctx = ocl_device_ctx;
}

AVFrame* AvFrameSourceMapOpenCl::opencl_frame_from_vaapi_frame(AVFrame *vaapi_frame) {
    int err;
    AVFrame *ocl_frame = av_frame_alloc();
    if (ocl_frame == NULL) {
        cerr << "Failed to allocate OpenCL frame\n";
    }
    AVBufferRef *ocl_hw_frames_ctx = NULL;

    err = av_hwframe_ctx_create_derived(
        &ocl_hw_frames_ctx,
        AV_PIX_FMT_OPENCL,
        this->ocl_device_ctx.get(),
        vaapi_frame->hw_frames_ctx,
        AV_HWFRAME_MAP_DIRECT
    );
    if (err) {
        cerr << "Failed to map hwframes context to OpenCL:" << errString(err) << "\n";
        throw err;
    }

    ocl_frame->hw_frames_ctx = ocl_hw_frames_ctx;
    ocl_frame->format = AV_PIX_FMT_OPENCL;

    err = av_hwframe_map(ocl_frame, vaapi_frame, AV_HWFRAME_MAP_READ);
    if (err) {
        cerr << "Failed to map hardware frames:" << errString(err) << "\n";
        throw err;
    }

    return ocl_frame;
}

AVFrame* AvFrameSourceMapOpenCl::peek_frame() {
    return this->opencl_frame_from_vaapi_frame(this->source->peek_frame());
}

AVFrame* AvFrameSourceMapOpenCl::pull_frame() {
    AVFrame *vaapi_frame = this->source->pull_frame();
    AVFrame *opencl_frame = this->opencl_frame_from_vaapi_frame(vaapi_frame);
    av_frame_free(&vaapi_frame);
    return opencl_frame;
}
