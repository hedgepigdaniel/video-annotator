#ifndef _FRAME_SOURCE_FFMPEG_OPENCL_HPP_
#define _FRAME_SOURCE_FFMPEG_OPENCL_HPP_

#include "FrameSource.hpp"
#include "AvFrameSource.hpp"

/**
 * A source of frames
 */
class FrameSourceFfmpegOpenCl: public FrameSource {
    AvFrameSource *source;
  public:
    /**
     * Return the next frame and advance the current position
     * Raises an exception if no frames are ready
     */
    cv::UMat pull_frame();

    FrameSourceFfmpegOpenCl(AvFrameSource *source);
};

#endif // _FRAME_SOURCE_FFMPEG_OPENCL_HPP_
