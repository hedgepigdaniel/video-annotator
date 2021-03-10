#include "AvFrameSourceProfile.hpp"

#include <iostream>

using namespace std;
using namespace std::chrono;

AvFrameSourceProfile::AvFrameSourceProfile(shared_ptr<AvFrameSource> source, string name):
    m_source(source), m_profiler(Profiler(name)) {}

AVFrame* AvFrameSourceProfile::peek_frame() {
    return m_source->peek_frame();
}

AVFrame* AvFrameSourceProfile::pull_frame() {
    m_profiler.before_enter();
    AVFrame *result = m_source->pull_frame();
    m_profiler.after_exit();
    return result;
}
