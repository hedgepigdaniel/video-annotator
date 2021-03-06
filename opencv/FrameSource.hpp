#ifndef _FRAME_SOURCE_HPP_
#define _FRAME_SOURCE_HPP_

#include <opencv2/core.hpp>

/**
 * A source of frames
 */
class FrameSource {
  public:
    /**
     * Attempt to read a frame
     * Raises an exception if no frames are ready
     */
    cv::Mat read_frame();
};

#endif // _FRAME_SOURCE_HPP_
