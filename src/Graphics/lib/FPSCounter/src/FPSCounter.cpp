#include "FPSCounter/FPSCounter.hpp"

#include "Constants/Constants.hpp"

void FPSCounter::register_frame() {
    frame_timestamps.push_back(sw.elapsed<std::chrono::seconds, 6>());
    if (frame_timestamps.size() > Constants::FPS_CALC_BUFFER_LEN) {
        frame_timestamps.pop_front();
    }
}

double FPSCounter::get_fps() {
    if (frame_timestamps.size() < 2) {
        return 0.0;
    }
    return (frame_timestamps.size() - 1) / 
            (frame_timestamps.back() - frame_timestamps.front());
}
