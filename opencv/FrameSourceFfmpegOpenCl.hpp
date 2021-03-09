#ifndef _FRAME_SOURCE_FFMPEG_OPENCL_HPP_
#define _FRAME_SOURCE_FFMPEG_OPENCL_HPP_

#include <memory>

#include "FrameSource.hpp"
#include "AvFrameSource.hpp"

/**
 * A source of frames
 */
class FrameSourceFfmpegOpenCl: public FrameSource {
    std::shared_ptr<AvFrameSource> source;
    cv::UMat next_frame;
  public:
    cv::UMat pull_frame();
    cv::UMat peek_frame();
    FrameSourceFfmpegOpenCl(std::shared_ptr<AvFrameSource> source);
};

#endif // _FRAME_SOURCE_FFMPEG_OPENCL_HPP_
