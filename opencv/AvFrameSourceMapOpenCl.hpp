#ifndef _AV_FRAME_SOURCE_MAP_OPENCL_HPP_
#define _AV_FRAME_SOURCE_MAP_OPENCL_HPP_


#include "AvFrameSource.hpp"

#include <string>

/**
 * Reads frames from a video file
 */
class AvFrameSourceMapOpenCl: public AvFrameSource {
    AvFrameSource *source;
    AVBufferRef *ocl_device_ctx = NULL;
    AVFrame* opencl_frame_from_vaapi_frame(AVFrame *vaapi_frame);
  public:
    AvFrameSourceMapOpenCl(AvFrameSource *source, AVBufferRef *ocl_device_ctx);
    AVFrame* pull_frame();
    AVFrame* peek_frame();
};

#endif // _AV_FRAME_SOURCE_MAP_OPENCL_HPP_