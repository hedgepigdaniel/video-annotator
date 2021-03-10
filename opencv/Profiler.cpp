#include "Profiler.hpp"

#include <iostream>

using namespace std;
using namespace std::chrono;

Profiler::Profiler(string name): m_name(name) {}

void Profiler::before_enter() {
    m_entrance_time = steady_clock::now();
}

void Profiler::after_exit() {
    steady_clock::time_point exit_time = steady_clock::now();
    steady_clock::duration call_time = duration_cast<milliseconds>(
        exit_time - m_entrance_time
    );
    m_num_frames++;
    m_inner_time += call_time;

    steady_clock::duration average_duration = m_inner_time / m_num_frames;
    steady_clock::duration average_external_duration = (exit_time - m_start_time) / m_num_frames;
    double average_duration_ms = duration_cast<microseconds>(average_duration).count() / 1000.0;
    fprintf(
        stderr,
        "%s: % 3.1f ms/frame (%3dfps). %3d%% of %3.1fms total/% 3dfps)\n",
        m_name.c_str(),
        average_duration_ms,
        (int) (average_duration_ms == 0 ? -1 : 1000 / average_duration_ms),
        (int) (100 * average_duration / average_external_duration),
        duration_cast<microseconds>(average_external_duration).count() / 1000.0,
        (int) (1000 / duration_cast<milliseconds>(average_external_duration).count())
    );
}
