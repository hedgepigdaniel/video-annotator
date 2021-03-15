#ifndef _AV_FRAME_SOURCE_MAP_OPENCL_HPP_
#define _AV_FRAME_SOURCE_MAP_OPENCL_HPP_


#include "AvFrameSource.hpp"

#include <string>
#include <memory>

/**
 * Maps VAAPI backed `AVFrame`s to OpenCL
 */
class AvFrameSourceMapOpenCl: public AvFrameSource {
    std::shared_ptr<AvFrameSource> source;
    std::shared_ptr<AVBufferRef> ocl_device_ctx = NULL;
    AVFrame* opencl_frame_from_vaapi_frame(AVFrame *vaapi_frame);
    AVBufferRef *hw_frames_ref;
  public:
    AvFrameSourceMapOpenCl(
      std::shared_ptr<AvFrameSource> source,
      std::shared_ptr<AVBufferRef> ocl_device_ctx
    );
    AVFrame* pull_frame();
    AVFrame* peek_frame();
};

#endif // _AV_FRAME_SOURCE_MAP_OPENCL_HPP_