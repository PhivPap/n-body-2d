#pragma once

#include <chrono>
#include <cstdint>
#include <iterator>
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
constexpr auto TRAILS_FADE_CHANGE_STEP = std::chrono::milliseconds(100);
constexpr uint8_t INIT_BODY_PIXEL_DIAMETER = 1;
constexpr Range<uint8_t> BODY_DIAMETER_PIXELS_RANGE = {1, 50};
constexpr sf::Color BG_COLOR(0, 0, 0, 255);
constexpr sf::Color GRID_COLOR(255, 255, 255, 64);
constexpr sf::Color SELECT_COLOR(255, 0, 0, 255);
// Selected bodies are shown in SELECT_COLOR at full brightness, over SELECT_COVERAGE_GAIN times
// their coverage of a pixel: a body 1 px wide covers at least a quarter of its brightest pixel
constexpr float SELECT_COVERAGE_GAIN = 4.f;
constexpr uint8_t FPS_CALC_BUFFER_LEN = 60;
constexpr Range<float> PANEL_UPDATE_HZ_RANGE = {0.1, 30};
// Exposure compensation of the bodies' light, and group brightness (0: drawn as hidden)
constexpr Range<double> EXPOSURE_EV_RANGE = {-10.0, 10.0};
constexpr Range<float> GROUP_BRIGHTNESS_RANGE = {0.f, 16.f};
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
// Bodies are hovered within HOVER_RADIUS_PX, or HOVER_MARGIN_PX around bigger ones and named
// bodies' markers. They are looked for in chunks of HOVER_CHUNK_SIZE: those near the cursor are
// searched again.
constexpr float HOVER_RADIUS_PX = 6.f;
constexpr float HOVER_MARGIN_PX = 3.f;
constexpr uint64_t HOVER_CHUNK_SIZE = 64;
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
// HDR rendering: the tone curve turns from linear to logarithmic around TONE_SOFTENING bodies per
// pixel, and the auto white point is the AUTO_WHITE_QUANTILE of the brightest pixels of the
// non-empty EXPOSURE_BLOCK_PX square blocks, at least AUTO_WHITE_MIN bodies per pixel. It comes
// from a histogram of EXPOSURE_HISTOGRAM_BINS bins, EXPOSURE_BINS_PER_OCTAVE per octave, starting
// at 2^EXPOSURE_LOG2_MIN, and the shown white point follows it over EXPOSURE_ADAPT_TIME
constexpr float TONE_SOFTENING = 2.f;
constexpr float HIGHLIGHT_DESATURATION = 0.25f;  // top share of the display range blended to white
constexpr float AUTO_WHITE_QUANTILE = 0.999f;
constexpr float AUTO_WHITE_MIN = 1.f;
constexpr uint32_t EXPOSURE_BLOCK_PX = 8;
constexpr uint32_t EXPOSURE_HISTOGRAM_BINS = 128;
constexpr float EXPOSURE_BINS_PER_OCTAVE = 4.f;
constexpr float EXPOSURE_LOG2_MIN = -8.f;
constexpr auto EXPOSURE_ADAPT_TIME = std::chrono::milliseconds(400);
constexpr double EXPOSURE_EV_STEP = 0.5;
// The stretch lifts faint light STRETCH_FACTOR times a step (the tone curve's softening divided
// by it), while the white point stays white: STRETCH_STEPS_RANGE steps from none
constexpr Range<int> STRETCH_STEPS_RANGE = {-2, 8};
constexpr float STRETCH_FACTOR = 2.f;
// Trail segments for bodies that moved SEGMENT_MIN_PX (less is covered by the body itself) to
// SEGMENT_MAX_PX since the last frame, both ends within SEGMENT_GUARD_PX of the window's origin
constexpr float SEGMENT_MIN_PX = 1.f;
constexpr float SEGMENT_MAX_PX = 4096.f;
constexpr float SEGMENT_GUARD_PX = 1e5f;
// A trail pixel given at least TRAIL_TAKEOVER_MIN of a body's light (half covered by a body of
// brightness 1) takes it if that body is shown at least as bright: a trail laid over another
// replaces it, also where it partly covers pixels
constexpr float TRAIL_TAKEOVER_MIN = 0.5f;
constexpr auto FRAME_TIME_MAX = std::chrono::milliseconds(250);
// Speed and Doppler colours span the VELOCITY_SCALE_QUANTILE of up to VELOCITY_SCALE_SAMPLES
// visible bodies on screen, refreshed every VELOCITY_SCALE_UPDATE_TIMER, followed over
// VELOCITY_SCALE_ADAPT_TIME
constexpr double VELOCITY_SCALE_QUANTILE = 0.95;
constexpr uint32_t VELOCITY_SCALE_SAMPLES = 4096;
constexpr uint32_t VELOCITY_SCALE_MIN_SAMPLES = 16;
constexpr auto VELOCITY_SCALE_UPDATE_TIMER = std::chrono::milliseconds(500);
constexpr auto VELOCITY_SCALE_ADAPT_TIME = std::chrono::milliseconds(1000);
constexpr double VELOCITY_SCALE_MIN = 1e-30;  // m/s
// Render groups: the legend's rows (populations, then "Other"), styled groups and the default
// group, each drawn alike. The shader's table holds MAX_RENDER_GROUPS, and the ramps up to
// MAX_RAMP_STOPS stops.
constexpr uint32_t MAX_RENDER_GROUPS = 128;
constexpr uint32_t LEGEND_GROUPS_MAX = 8;
constexpr uint32_t MAX_RAMP_STOPS = 8;
constexpr uint16_t LEGEND_PANEL_WIDTH = 340;
// Bloom: light above BLOOM_THRESHOLD of the white point (soft over BLOOM_KNEE), blurred over
// BLOOM_LEVELS halvings, added at BLOOM_STRENGTH
constexpr float BLOOM_THRESHOLD = 0.5f;
constexpr float BLOOM_KNEE = 0.5f;
constexpr float BLOOM_STRENGTH = 0.15f;
constexpr uint32_t BLOOM_LEVELS = 5;
// Automatic colours of the legend's groups, in this order (the dataviz palette validated for dark
// backgrounds): blue, orange, aqua, yellow, magenta, green, violet, red. More groups are "Other".
constexpr sf::Color GROUP_PALETTE[] = {{0x39, 0x87, 0xe5}, {0xd9, 0x59, 0x26}, {0x19, 0x9e, 0x70},
        {0xc9, 0x85, 0x00}, {0xd5, 0x51, 0x81}, {0x00, 0x83, 0x00}, {0x90, 0x85, 0xe9},
        {0xe6, 0x67, 0x67}};
constexpr sf::Color OTHER_GROUP_COLOR(0x89, 0x87, 0x81);
// Speed: pale (slow) to orange-red (fast); Doppler: blue (approaching), grey, red (receding);
// interpolated in linear RGB
constexpr sf::Color SPEED_STOPS[] = {{0xd9, 0xdd, 0xe5}, {0xff, 0xe0, 0x8a}, {0xff, 0xa0, 0x40},
        {0xff, 0x50, 0x30}};
constexpr sf::Color DOPPLER_STOPS[] = {{0x39, 0x87, 0xe5}, {0x8f, 0xb6, 0xee}, {0xd8, 0xd8, 0xd8},
        {0xee, 0x9a, 0x9a}, {0xe6, 0x40, 0x40}};
// Density: matplotlib's inferno at 17 even steps, interpolated in sRGB (display) values
constexpr sf::Color DENSITY_STOPS[] = {{0x00, 0x00, 0x04}, {0x0b, 0x07, 0x24},
        {0x21, 0x0c, 0x4a}, {0x3d, 0x09, 0x65}, {0x57, 0x10, 0x6e}, {0x71, 0x19, 0x6e},
        {0x8a, 0x22, 0x6a}, {0xa3, 0x2c, 0x61}, {0xbc, 0x37, 0x54}, {0xd2, 0x46, 0x44},
        {0xe4, 0x5a, 0x31}, {0xf1, 0x73, 0x1d}, {0xf9, 0x8e, 0x09}, {0xfc, 0xac, 0x11},
        {0xf9, 0xcb, 0x35}, {0xf2, 0xea, 0x69}, {0xfc, 0xff, 0xa4}};

static_assert(ZOOM_FACTOR > 1.0);
static_assert(GRID_SPACING_FACTOR >= 2.0);
static_assert(TRAILS_FADE_CHANGE_STEP.count() > 0);
static_assert(INIT_BODY_PIXEL_DIAMETER >= BODY_DIAMETER_PIXELS_RANGE.first);
static_assert(INIT_BODY_PIXEL_DIAMETER <= BODY_DIAMETER_PIXELS_RANGE.second);
static_assert(SELECT_COVERAGE_GAIN >= 1);
static_assert(NAMED_GROUP_MAX_SIZE >= 1 && NAMED_BODIES_MAX >= 1);
static_assert(NAMED_SEQUENCE_MAX_SIZE <= NAMED_GROUP_MAX_SIZE);
static_assert(NAMED_MARKER_MIN_EXTRA_PX > 0);
static_assert(NAMED_MARKER_MIN_EXTRA_PX <= NAMED_MARKER_MAX_EXTRA_PX);
static_assert(NAMED_MARKER_MASS_DECADES > 0);
static_assert(HOVER_CHUNK_SIZE > 0);
static_assert(LABEL_MAX_CHARS > 3);  // room for the "..." of a truncated id
static_assert(ORBIT_DASH_PX > 0 && ORBIT_GAP_PX > 0);
static_assert(ORBIT_TOLERANCE_PX > 0 && ORBIT_CLIP_MARGIN_PX >= 0);
static_assert(ORBIT_INITIAL_SEGMENTS >= 4);
static_assert(ORBIT_OPEN_DISTANCE_FACTOR > 1 && ORBIT_OPEN_PERIAPSIS_FACTOR > 1);
static_assert(EXPOSURE_EV_RANGE.first < 0 && EXPOSURE_EV_RANGE.second > 0);
static_assert(GROUP_BRIGHTNESS_RANGE.first == 0 && GROUP_BRIGHTNESS_RANGE.second > 0);
static_assert(TONE_SOFTENING > 0 && AUTO_WHITE_MIN > 0);
static_assert(HIGHLIGHT_DESATURATION > 0 && HIGHLIGHT_DESATURATION <= 1);
static_assert(AUTO_WHITE_QUANTILE > 0 && AUTO_WHITE_QUANTILE < 1);
static_assert(EXPOSURE_BLOCK_PX >= 1 && EXPOSURE_HISTOGRAM_BINS >= 2);
static_assert(EXPOSURE_BINS_PER_OCTAVE > 0 && EXPOSURE_EV_STEP > 0);
static_assert(STRETCH_STEPS_RANGE.first <= 0 && STRETCH_STEPS_RANGE.second >= 0);
static_assert(STRETCH_FACTOR > 1);
static_assert(SEGMENT_MIN_PX > 0 && SEGMENT_MIN_PX < SEGMENT_MAX_PX);
static_assert(SEGMENT_MAX_PX < SEGMENT_GUARD_PX);
static_assert(TRAIL_TAKEOVER_MIN > 0 && TRAIL_TAKEOVER_MIN <= 1);
static_assert(VELOCITY_SCALE_QUANTILE > 0 && VELOCITY_SCALE_QUANTILE < 1);
static_assert(VELOCITY_SCALE_MIN_SAMPLES >= 1);
static_assert(VELOCITY_SCALE_MIN_SAMPLES <= VELOCITY_SCALE_SAMPLES);
// Group indices are 8-bit, and the shader's table takes 512 of the 1024 vertex uniform
// components GL 3.0 guarantees: the legend's rows, "Other" and the default group fit
static_assert(MAX_RENDER_GROUPS <= 255 && MAX_RENDER_GROUPS * 4 <= 512);
static_assert(LEGEND_GROUPS_MAX + 2 <= MAX_RENDER_GROUPS);
static_assert(LEGEND_GROUPS_MAX <= 9);  // number keys 1-9
static_assert(std::size(GROUP_PALETTE) == LEGEND_GROUPS_MAX);
static_assert(std::size(SPEED_STOPS) >= 2 && std::size(SPEED_STOPS) <= MAX_RAMP_STOPS);
static_assert(std::size(DOPPLER_STOPS) >= 2 && std::size(DOPPLER_STOPS) <= MAX_RAMP_STOPS);
static_assert(std::size(DENSITY_STOPS) >= 2);
static_assert(BLOOM_THRESHOLD >= 0 && BLOOM_KNEE > 0 && BLOOM_STRENGTH >= 0);
static_assert(BLOOM_LEVELS >= 1);
}  // namespace Graphics
}  // namespace Constants
