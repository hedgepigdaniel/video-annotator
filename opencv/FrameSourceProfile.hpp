#ifndef _FRAME_SOURCE_PROFILE_HPP_
#define _FRAME_SOURCE_PROFILE_HPP_

#include "FrameSource.hpp"

#include <string>
#include <memory>

#include <opencv2/core.hpp>

#include "Profiler.hpp"

class FrameSourceProfile: public FrameSource {
    std::shared_ptr<FrameSource> m_source;
    Profiler m_profiler;
  public:
    FrameSourceProfile(std::shared_ptr<FrameSource> source, std::string name);
    cv::UMat pull_frame();
    cv::UMat peek_frame();
};

#endif // _FRAME_SOURCE_PROFILE_HPP_