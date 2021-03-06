#ifndef _FRAME_SOURCE_HPP_
#define _FRAME_SOURCE_HPP_

#include <opencv2/core.hpp>

/**
 * A source of frames
 */
class FrameSource {
  public:
    /**
     * Return the next frame and advance the current position
     * Raises an exception if no frames are ready
     */
    virtual cv::UMat pull_frame() = 0;

    virtual ~FrameSource() = default;
};

#endif // _FRAME_SOURCE_HPP_
