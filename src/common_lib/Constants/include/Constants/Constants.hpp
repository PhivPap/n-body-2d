#pragma once

#include <chrono>
#include <cstdint>
#include <utility>

#include "SFML/Graphics/Color.hpp"
#include "SFML/System/Vector2.hpp"


template <typename T>
using Range = std::pair<T, T>;

namespace Constants {
namespace Simulation {

constexpr double G = 6.67430e-11;  // (m^3 * kg^-1 * s^-2)
// [picosecond (s), billion years (s)]
constexpr Range<double> TIMESTEP_RANGE = {1e-9, 31'556'952'000'000'000};
constexpr Range<double> SOFTENING_FACTOR_RANGE = {0.0, 0.2};
constexpr Range<uint16_t> THREADS_RANGE = {1, 256};
constexpr Range<double> THETA_RANGE = {0.0, 100.0};
constexpr auto STATS_UPDATE_TIMER = std::chrono::milliseconds(8);
constexpr uint64_t MAX_PAIRWISE_SOFTENING_COMPUTATIONS = 1'000'000;
// Smallest squared softening length (m^2), far below any resolvable separation. Keeps the pull
// between coincident bodies finite (zero) without softening.
constexpr double MIN_EPSILON_SQUARED = 1e-100;
constexpr double TIMESTEP_CHANGE_FACTOR = 1.1;

static_assert(TIMESTEP_CHANGE_FACTOR > 1.0);
}  // namespace Simulation

namespace Graphics {
constexpr Range<uint16_t> WINDOW_WIDTH_RANGE = {240, 7680};
constexpr Range<uint16_t> WINDOW_HEIGHT_RANGE = {135, 4320};
constexpr Range<uint16_t> FPS_RANGE = {1, 512};
// [picometer (m), observable universe diameter (m)]
constexpr Range<double> PIXEL_RES_RANGE = {1e-12, 8.8e50};
constexpr double ZOOM_FACTOR = 1.08;
constexpr double GRID_SPACING_FACTOR = 4;
constexpr sf::Color BODY_COLOR(255, 255, 255, 120);
constexpr auto TRAILS_FADE_CHANGE_STEP = std::chrono::milliseconds(100);
constexpr uint8_t INIT_BODY_PIXEL_DIAMETER = 1;
constexpr Range<uint8_t> BODY_DIAMETER_PIXELS_RANGE = {1, 50};
constexpr sf::Color BG_COLOR(0, 0, 0, 255);
constexpr sf::Color GRID_COLOR(255, 255, 255, 64);
constexpr sf::Color SELECT_COLOR(255, 0, 0, 255);
constexpr uint8_t FPS_CALC_BUFFER_LEN = 60;
constexpr Range<float> PANEL_UPDATE_HZ_RANGE = {0.1, 30};
constexpr uint16_t CONFIG_PANEL_WIDTH = 340;
constexpr uint16_t STATS_PANEL_WIDTH = 340;
constexpr uint16_t COMMANDS_PANEL_WIDTH = 370;
constexpr uint16_t INSPECTOR_PANEL_WIDTH = 430;  // fits a position in km: 52 characters
constexpr auto STATS_UPDATE_TIMER = std::chrono::milliseconds(8);
// Named bodies: those whose id group (the id without a trailing number) has at most
// NAMED_GROUP_MAX_SIZE members, and at most NAMED_SEQUENCE_MAX_SIZE if they are numbered in
// sequence; only the heaviest NAMED_BODIES_MAX are kept
constexpr uint32_t NAMED_GROUP_MAX_SIZE = 64;
constexpr uint32_t NAMED_SEQUENCE_MAX_SIZE = 8;
constexpr uint32_t NAMED_BODIES_MAX = 1024;
// A named body's marker is base + MIN_EXTRA to base + MAX_EXTRA pixels wide, by log-mass over
// NAMED_MARKER_MASS_DECADES below the heaviest named body
constexpr float NAMED_MARKER_MIN_EXTRA_PX = 2.f;
constexpr float NAMED_MARKER_MAX_EXTRA_PX = 12.f;
constexpr double NAMED_MARKER_MASS_DECADES = 9.0;
constexpr sf::Color NAMED_BODY_COLOR(255, 255, 255, 255);
constexpr uint32_t LABELS_MAX = 128;
constexpr uint32_t LABEL_CHARACTER_SIZE = 13;
constexpr uint32_t LABEL_MAX_CHARS = 32;
constexpr float LABEL_GAP_PX = 4.f;      // between a label and its marker
constexpr float LABEL_SPACING_PX = 2.f;  // between labels
constexpr sf::Color LABEL_COLOR(230, 230, 230, 255);
constexpr sf::Color LABEL_OUTLINE_COLOR(0, 0, 0, 200);
constexpr float HOVER_RADIUS_PX = 6.f;  // any body
constexpr float HOVER_MARGIN_PX = 3.f;  // around a named body's marker
constexpr sf::Color HOVER_COLOR(255, 220, 0, 220);
constexpr float CLICK_MAX_DRAG_PX = 3.f;
constexpr auto PRIMARIES_UPDATE_TIMER = std::chrono::milliseconds(250);
constexpr uint32_t TOOLTIP_CHARACTER_SIZE = 14;
constexpr float TOOLTIP_OFFSET_PX = 16.f;
constexpr float TOOLTIP_PADDING_PX = 6.f;
constexpr sf::Color TOOLTIP_BACKGROUND_COLOR(40, 40, 40, 220);
constexpr sf::Color ORBIT_COLOR(100, 180, 255, 220);
constexpr sf::Color NAMED_ORBIT_COLOR(190, 190, 190, 150);  // every named body's orbit (A)
constexpr float ORBIT_DASH_PX = 8.f;
constexpr float ORBIT_GAP_PX = 6.f;
// Orbits are drawn within ORBIT_TOLERANCE_PX of the true conic, in ORBIT_INITIAL_SEGMENTS pieces
// split in halves at most ORBIT_MAX_DEPTH times, and clipped to the window grown by
// ORBIT_CLIP_MARGIN_PX
constexpr double ORBIT_TOLERANCE_PX = 0.5;
constexpr uint32_t ORBIT_INITIAL_SEGMENTS = 64;
constexpr uint32_t ORBIT_MAX_DEPTH = 48;
constexpr double ORBIT_CLIP_MARGIN_PX = 2.0;
// Open orbits are drawn out to max(OPEN_DISTANCE_FACTOR x the distance,
// OPEN_PERIAPSIS_FACTOR x the periapsis)
constexpr double ORBIT_OPEN_DISTANCE_FACTOR = 4.0;
constexpr double ORBIT_OPEN_PERIAPSIS_FACTOR = 20.0;
constexpr float ORBIT_MIN_SIZE_PX = 2.f;
// Periapsis and apoapsis markers, labelled "Pe" and "Ap"
constexpr float APSIS_MARKER_RADIUS_PX = 3.f;
constexpr uint32_t APSIS_CHARACTER_SIZE = 12;
constexpr sf::Vector2f APSIS_LABEL_OFFSET_PX(5.f, -8.f);

static_assert(ZOOM_FACTOR > 1.0);
static_assert(GRID_SPACING_FACTOR >= 2.0);
static_assert(TRAILS_FADE_CHANGE_STEP.count() > 0);
static_assert(INIT_BODY_PIXEL_DIAMETER >= BODY_DIAMETER_PIXELS_RANGE.first);
static_assert(INIT_BODY_PIXEL_DIAMETER <= BODY_DIAMETER_PIXELS_RANGE.second);
static_assert(NAMED_GROUP_MAX_SIZE >= 1 && NAMED_BODIES_MAX >= 1);
static_assert(NAMED_SEQUENCE_MAX_SIZE <= NAMED_GROUP_MAX_SIZE);
static_assert(NAMED_MARKER_MIN_EXTRA_PX > 0);
static_assert(NAMED_MARKER_MIN_EXTRA_PX <= NAMED_MARKER_MAX_EXTRA_PX);
static_assert(NAMED_MARKER_MASS_DECADES > 0);
static_assert(LABEL_MAX_CHARS > 3);  // room for the "..." of a truncated id
static_assert(ORBIT_DASH_PX > 0 && ORBIT_GAP_PX > 0);
static_assert(ORBIT_TOLERANCE_PX > 0 && ORBIT_CLIP_MARGIN_PX >= 0);
static_assert(ORBIT_INITIAL_SEGMENTS >= 4);
static_assert(ORBIT_OPEN_DISTANCE_FACTOR > 1 && ORBIT_OPEN_PERIAPSIS_FACTOR > 1);
}  // namespace Graphics
}  // namespace Constants
