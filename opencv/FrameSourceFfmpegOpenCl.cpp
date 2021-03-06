#include "FrameSourceFfmpegOpenCl.hpp"

#include <iostream>
#include <opencv2/core/ocl.hpp>
#include <CL/opencl.hpp>

#include "utils.hpp"

using namespace cv;
using namespace std;

int convert_ocl_images_to_nv12_umat(cl_mem cl_luma, cl_mem cl_chroma, UMat& dst)
{
    int ret = 0;

    cl_image_format luma_fmt = { 0, 0 };
    cl_image_format chroma_fmt = { 0, 0 };
    ret = clGetImageInfo(cl_luma, CL_IMAGE_FORMAT, sizeof(cl_image_format), &luma_fmt, 0);
    if (ret) {
        cerr << "Failed to get luma image format: " << ret << "\n";
        return ret;
    }
    ret = clGetImageInfo(cl_chroma, CL_IMAGE_FORMAT, sizeof(cl_image_format), &chroma_fmt, 0);
    if (ret) {
        cerr << "Failed to get chroma image format: " << ret << "\n";
        return ret;
    }
    if (luma_fmt.image_channel_data_type != CL_UNORM_INT8 ||
    chroma_fmt.image_channel_data_type != CL_UNORM_INT8) {
        cerr << "Wrong image format\n";
        return 1;
    }
    if (luma_fmt.image_channel_order != CL_R ||
    chroma_fmt.image_channel_order != CL_RG) {
        cerr << "Wrong image channel order\n";
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
        cerr << "Failed to get image info: " << ret << "\n";
        return ret;
    }

    if (luma_w != 2 * chroma_w || luma_h != 2 *chroma_h ) {
        cerr << "Mismatched image dimensions\n";
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
        cerr << "Failed to enqueue image copy to buffer\n";
        return ret;
    }

    return ret;
}

FrameSourceFfmpegOpenCl::FrameSourceFfmpegOpenCl(AvFrameSource *source) {
    this->source = source;
}

UMat FrameSourceFfmpegOpenCl::peek_frame() {
    if (!this->next_frame.empty()) {
        return this->next_frame;
    }
    int err;
    AVFrame *av_frame = this->source->pull_frame();
    UMat frame;

    err = convert_ocl_images_to_nv12_umat(
        (cl_mem) av_frame->data[0],
        (cl_mem) av_frame->data[1],
        frame
    );
    if (err) {
        cerr << "Failed to convert OpenCL AVFrame to opencv:"<< errString(err) << "\n";
        throw err;
    }

    av_frame_free(&av_frame);
    this->next_frame = frame;
    return this->next_frame;
}

UMat FrameSourceFfmpegOpenCl::pull_frame() {
    UMat frame = this->peek_frame();
    this->next_frame = UMat();
    return frame;
}
