#ifndef _FRAME_SOURCE_PROFILE_HPP_
#define _FRAME_SOURCE_PROFILE_HPP_

#include "FrameSource.hpp"

#include <string>
#include <chrono>
#include <ratio>

#include <opencv2/core.hpp>

class FrameSourceProfile: public FrameSource {
    FrameSource *source;
    std::string name;
    int num_frames = 0;
    std::chrono::steady_clock::duration total_time = std::chrono::steady_clock::duration::zero();
  public:
    FrameSourceProfile(FrameSource *source, std::string name);
    cv::UMat pull_frame();
    cv::UMat peek_frame();
};

#endif // _FRAME_SOURCE_PROFILE_HPP_