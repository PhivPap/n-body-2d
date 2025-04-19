#pragma once

#include <chrono>
#include <deque>

#include "StopWatch/StopWatch.hpp"

class FPSCounter {
private:
    StopWatch sw;
    std::deque<double> frame_timestamps; // seconds
public:
    void register_frame();
    double get_fps();
};
