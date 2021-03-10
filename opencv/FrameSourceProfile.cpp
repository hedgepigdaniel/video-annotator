#include "FrameSourceProfile.hpp"

#include <iostream>

using namespace cv;
using namespace std;

FrameSourceProfile::FrameSourceProfile(shared_ptr<FrameSource> source, string name):
    m_source(source), m_profiler(Profiler(name)) {}

UMat FrameSourceProfile::peek_frame() {
    return m_source->peek_frame();
}

UMat FrameSourceProfile::pull_frame() {
    m_profiler.before_enter();
    UMat result = m_source->pull_frame();
    m_profiler.after_exit();
    return result;
}
