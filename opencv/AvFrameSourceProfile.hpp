#ifndef _AV_FRAME_SOURCE_PROFILE_HPP_
#define _AV_FRAME_SOURCE_PROFILE_HPP_

#include "AvFrameSource.hpp"

#include <string>
#include <chrono>
#include <ratio>
#include <memory>

#include "Profiler.hpp"

class AvFrameSourceProfile: public AvFrameSource {
    std::shared_ptr<AvFrameSource> m_source;
    Profiler m_profiler;
  public:
    AvFrameSourceProfile(std::shared_ptr<AvFrameSource> source, std::string name);
    AVFrame* pull_frame();
    AVFrame* peek_frame();
};

#endif // _AV_FRAME_SOURCE_PROFILE_HPP_