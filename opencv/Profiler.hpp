#ifndef _PROFILE_HPP_
#define _PROFILE_HPP_

#include <string>
#include <chrono>
#include <ratio>
#include <memory>

class Profiler {
    std::string m_name;
    int m_num_frames = 0;
    std::chrono::steady_clock::time_point m_entrance_time;
    std::chrono::steady_clock::time_point m_start_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::duration m_inner_time = std::chrono::steady_clock::duration::zero();
  public:
    Profiler(std::string name);
    void before_enter();
    void after_exit();
};

#endif // _PROFILE_HPP_