#include "AvFrameSourceProfile.hpp"

#include <iostream>

using namespace std;
using namespace std::chrono;

AvFrameSourceProfile::AvFrameSourceProfile(AvFrameSource *source, string name) {
    this->source = source;
    this->name = name;
}

AVFrame* AvFrameSourceProfile::peek_frame() {
    return this->source->peek_frame();
}

AVFrame* AvFrameSourceProfile::pull_frame() {
    steady_clock::time_point start_time = steady_clock::now();
    AVFrame *result = this->source->pull_frame();
    steady_clock::duration time_taken = duration_cast<milliseconds>(
        steady_clock::now() - start_time
    );
    this->num_frames++;
    this->total_time += time_taken;

    steady_clock::duration average_duration = this->total_time / this->num_frames;
    cerr << "Milliseconds per frame: " <<
        duration_cast<microseconds>(average_duration).count() / 1000.0 <<
        " (" << this->name << ")\n";
    return result;
}
