#ifndef _FRAME_SOURCE_PROFILE_HPP_
#define _FRAME_SOURCE_PROFILE_HPP_

#include "FrameSource.hpp"

#include <string>
#include <chrono>
#include <ratio>
#include <memory>

#include <opencv2/core.hpp>

class FrameSourceProfile: public FrameSource {
    std::shared_ptr<FrameSource> source;
    std::string name;
    int num_frames = 0;
    std::chrono::steady_clock::duration total_time = std::chrono::steady_clock::duration::zero();
  public:
    FrameSourceProfile(std::shared_ptr<FrameSource> source, std::string name);
    cv::UMat pull_frame();
    cv::UMat peek_frame();
};

#endif // _FRAME_SOURCE_PROFILE_HPP_