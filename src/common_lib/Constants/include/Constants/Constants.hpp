#pragma once

#include <cstdint>
#include <chrono>

#include "SFML/Graphics/Color.hpp"

template <typename T>
using Range = std::pair<T, T>;

namespace Constants {
    namespace Simulation {
        constexpr double G = 6.67430e-11; // (m^3 * kg^-1 * s^-2)
        // [picosecond (s), billion years (s)]
        constexpr Range<double> TIMESTEP_RANGE = {1e-12, 31'556'952'000'000'000};
        const char* const ALLOWED_ALGORITHMS[] = { "barnes-hut", "naive" };
        constexpr Range<double> SOFTENING_FACTOR_RANGE = {0.0, 0.2};
        constexpr Range<uint16_t> THREADS_RANGE = {1, 256};
        constexpr double THETA = 0.7;
        constexpr auto STATS_UPDATE_TIMER = std::chrono::microseconds(50);
        constexpr uint64_t MAX_PAIRWISE_SOFTENING_COMPUTATIONS = 1'000'000;
    }
    namespace Graphics {
        constexpr Range<uint16_t> WINDOW_WIDTH_RANGE = {240, 7680};
        constexpr Range<uint16_t> WINDOW_HEIGHT_RANGE = {135, 4320};
        constexpr Range<uint16_t> FPS_RANGE = {1, 512};
        // [picometer (m), observable universe diameter (m)]
        constexpr Range<double> PIXEL_RES_RANGE = {1e-12, 8.8e50};
        constexpr double ZOOM_FACTOR = 0.99;
        constexpr double GRID_SPACING_FACTOR = 4;
        constexpr sf::Color BODY_COLOR(255, 255, 255, 120);
        constexpr uint8_t BODY_PIXEL_DIAMETER = 1;
        constexpr sf::Color BG_COLOR(0, 0, 0);
        constexpr sf::Color GRID_COLOR(255, 255, 255, 64);
        constexpr uint8_t FPS_CALC_BUFFER_LEN = 60;
        constexpr Range<float> PANEL_UPDATE_HZ_RANGE = {0.1, 30};
        constexpr sf::Vector2u PANEL_RES = {340, 480};
        constexpr auto STATS_UPDATE_TIMER = std::chrono::microseconds(50);
    }

    static_assert(Graphics::ZOOM_FACTOR > 0.0 && Graphics::ZOOM_FACTOR < 1.0);
    static_assert(Graphics::GRID_SPACING_FACTOR >= 2.0);
    static_assert(Graphics::BODY_PIXEL_DIAMETER >= 1 && Graphics::BODY_PIXEL_DIAMETER <= 50);
}
