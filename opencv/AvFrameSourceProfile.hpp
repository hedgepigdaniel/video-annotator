#ifndef _AV_FRAME_SOURCE_PROFILE_HPP_
#define _AV_FRAME_SOURCE_PROFILE_HPP_

#include "AvFrameSource.hpp"

#include <string>
#include <chrono>
#include <ratio>
#include <memory>

class AvFrameSourceProfile: public AvFrameSource {
    std::shared_ptr<AvFrameSource> source;
    std::string name;
    int num_frames = 0;
    std::chrono::steady_clock::duration total_time = std::chrono::steady_clock::duration::zero();
  public:
    AvFrameSourceProfile(std::shared_ptr<AvFrameSource> source, std::string name);
    AVFrame* pull_frame();
    AVFrame* peek_frame();
};

#endif // _AV_FRAME_SOURCE_PROFILE_HPP_