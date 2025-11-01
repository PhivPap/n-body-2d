#pragma once

#include <inttypes.h>

#include "SFML/Graphics/Color.hpp"

namespace Constants {
    constexpr double G = 6.67430e-11;                       // (m^3 * kg^-1 * s^-2)
    // [picosecond (s), billion years (s)]
    constexpr std::pair<double, double> TIMESTEP_RANGE = {1e-12, 31'556'952'000'000'000};
    constexpr std::pair<uint16_t, uint16_t> WINDOW_WIDTH_RANGE = {240, 7680};
    constexpr std::pair<uint16_t, uint16_t> WINDOW_HEIGHT_RANGE = {135, 4320};
    constexpr std::pair<uint16_t, uint16_t> FPS_RANGE = {1, 512};
    // [picometer (m), observable universe diameter (m)]
    constexpr std::pair<double, double> PIXEL_RES_RANGE = {1e-12, 8.8e50};
    const char* const ALLOWED_ALGORITHMS[] = { "barnes-hut", "naive" };
    constexpr std::pair<uint16_t, uint16_t> THREADS_RANGE = {1, 256};
    constexpr double ZOOM_FACTOR = 0.9;
    constexpr double GRID_SPACING_FACTOR = 4;
    constexpr sf::Color BODY_COLOR(255, 255, 255, 120);
    constexpr uint8_t BODY_PIXEL_DIAMETER = 1;
    constexpr sf::Color BG_COLOR(0, 0, 0);
    constexpr sf::Color GRID_COLOR(255, 255, 255, 64);
    constexpr uint8_t FPS_CALC_BUFFER_LEN = 60;

    constexpr double SOFTENING_FACTOR = 1e25;
    constexpr double THETA = 0.7;

    static_assert(ZOOM_FACTOR > 0.0 && ZOOM_FACTOR < 1.0);
    static_assert(GRID_SPACING_FACTOR >= 2.0);
    static_assert(BODY_PIXEL_DIAMETER >= 1 && BODY_PIXEL_DIAMETER <= 50);
}
