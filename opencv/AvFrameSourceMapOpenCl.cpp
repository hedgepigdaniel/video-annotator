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
    AVFrame *vaapi_frame = this->source->peek_frame();
    this->hw_frames_ref = av_hwframe_ctx_alloc(this->ocl_device_ctx.get());
    AVHWFramesContext * ocl_hw_frames_ctx = (AVHWFramesContext *)(this->hw_frames_ref->data);
    ocl_hw_frames_ctx->format = AV_PIX_FMT_OPENCL;
    ocl_hw_frames_ctx->sw_format = AV_PIX_FMT_NV12;
    ocl_hw_frames_ctx->width = vaapi_frame->width;
    ocl_hw_frames_ctx->height = vaapi_frame->height;
    ocl_hw_frames_ctx->initial_pool_size = 40;

    int err = av_hwframe_ctx_init(this->hw_frames_ref);
    if (err < 0) {
        cerr << "Failed init context OpenCL frame:" << errString(err) << "\n";
        av_buffer_unref(&this->hw_frames_ref);
        throw err;
    }
}

AVFrame* AvFrameSourceMapOpenCl::opencl_frame_from_vaapi_frame(AVFrame *vaapi_frame) {
    int err;
    AVFrame *tmp_frame = av_frame_alloc();
    if (tmp_frame == NULL) {
        cerr << "Failed to allocate temp frame\n";
    }

    err = av_hwframe_transfer_data(tmp_frame, vaapi_frame, 0);
    if (err) {
        cerr << "Failed to copy VAAPI frames to memory:" << errString(err) << "\n";
        throw err;
    }

    AVFrame *ocl_frame = av_frame_alloc();
    if (ocl_frame == NULL) {
        cerr << "Failed to allocate OpenCL frame\n";
    }

    err = av_hwframe_get_buffer(hw_frames_ref, ocl_frame, 0);
    if (err) {
        cerr << "Failed to get buffer for OpenCL frame:" << errString(err) << "\n";
        throw err;
    }
    if (!ocl_frame->hw_frames_ctx) {
        cerr << "Failed to get buffer for OpenCL frame\n";
        throw AVERROR(ENOMEM);
    }

    err = av_hwframe_transfer_data(ocl_frame, tmp_frame, 0);
    if (err) {
        cerr << "Failed to copy memory to OpenCL frames:" << errString(err) << "\n";
        throw err;
    }

    av_frame_free(&tmp_frame);
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
