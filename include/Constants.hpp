#pragma once

#include <inttypes.h>

namespace Constants {
    constexpr double G = 6.67430e-11;                       // (m^3 * kg^-1 * s^-2)
    constexpr double MIN_TIMESTEP = 1e-12;                  // picosecond (s)
    constexpr double MAX_TIMESTEP = 31'556'952'000'000'000; // billion years (s)
    constexpr uint16_t MAX_WINDOW_WIDTH = 7680;             // 8k width
    constexpr uint16_t MAX_WINDOW_HEIGHT = 4320;            // 8k height
    constexpr uint16_t MIN_FPS = 1;
    constexpr uint16_t MAX_FPS = 512;
    constexpr double MIN_PIXEL_RES = 1e-12;                 // picometer (m)
    constexpr double MAX_PIXEL_RES = 8.8e50;                // observable universe diameter (m)
    const char* const ALLOWED_ALGORITHMS[] = { "barnes-hut", "brute-force" };
}
