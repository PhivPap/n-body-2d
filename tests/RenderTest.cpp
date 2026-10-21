#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "Check.hpp"
#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"
#include "NamedBodies/NamedBodies.hpp"
#include "Render/BodyVertex.hpp"
#include "Render/Colors.hpp"
#include "Render/HdrRenderer.hpp"
#include "Render/RenderGroups.hpp"
#include "Render/ToneMath.hpp"

using Vector = sf::Vector2<double>;
using Style = Config::Graphics::GroupStyle;


namespace {
// Bodies with these ids, of unit mass at the origin, at rest
Bodies make_bodies(std::vector<std::string> ids) {
    const size_t n = ids.size();
    return Bodies(std::move(ids), std::vector<double>(n, 1.0), std::vector<Vector>(n),
            std::vector<Vector>(n));
}

// `count` ids `prefix` + number appended
void add_numbered(std::vector<std::string>& ids, const std::string& prefix, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        ids.push_back(prefix + std::to_string(i));
    }
}

RenderGroups make_groups(const Bodies& bodies, const RenderGroups::Styles& styles = {}) {
    using namespace Constants::Graphics;
    return RenderGroups(bodies, group_ids(bodies, NAMED_GROUP_MAX_SIZE, NAMED_SEQUENCE_MAX_SIZE),
            styles);
}

Style style(sf::Color color, float brightness = 1.f) {
    return {.color_str = "", .color = color, .brightness = brightness};
}

void test_srgb() {
    bool round_trips = true;  // every 8-bit value
    for (int c = 0; c < 256; c++) {
        round_trips &= std::lround(255.f * Colors::to_srgb(Colors::to_linear(c / 255.f))) == c;
    }
    CHECK(round_trips);
    CHECK(Colors::to_linear(0.f) == 0.f && Colors::to_srgb(0.f) == 0.f);
    CHECK_NEAR(Colors::to_linear(1.f), 1.0, 1e-6);
    CHECK_NEAR(Colors::to_srgb(1.f), 1.0, 1e-6);
    CHECK_NEAR(Colors::to_linear(0.5f), 0.214041, 1e-6);
    CHECK(Colors::to_srgb(-1.f) == 0.f && Colors::to_srgb(2.f) == Colors::to_srgb(1.f));
    const sf::Color color(12, 200, 255);
    CHECK(Colors::to_srgb(Colors::to_linear(color)) == color);
}

void test_unit_luminance() {
    const sf::Vector3f color(0.2f, 0.5f, 0.1f);
    const sf::Vector3f unit = Colors::unit_luminance(color);
    CHECK_NEAR(Colors::luminance(unit), 1.0, 1e-6);
    CHECK_NEAR(unit.x / unit.y, color.x / color.y, 1e-6);
    CHECK_NEAR(unit.z / unit.y, color.z / color.y, 1e-6);
    CHECK(Colors::unit_luminance({}) == sf::Vector3f());
    CHECK_NEAR(Colors::luminance(Colors::to_linear(sf::Color::White)), 1.0, 1e-6);
}

void test_display_hue() {
    const sf::Vector3f green = Colors::to_linear(sf::Color(0x19, 0x9e, 0x70));
    CHECK(Colors::display_hue(green) == sf::Color(0x2f, 0xff, 0xb7));  // brightened
    CHECK(Colors::display_hue(0.3f * green) == Colors::display_hue(green));
    const sf::Color blue(0x7f, 0xb2, 0xff);  // already at full brightness
    CHECK(Colors::display_hue(Colors::to_linear(blue)) == blue);
    CHECK(Colors::display_hue({}) == sf::Color::Black);
}

void test_ramps() {
    using Constants::Graphics::DENSITY_STOPS, Constants::Graphics::SPEED_STOPS;
    const auto speed = [](float t) { return Colors::to_srgb(Colors::ramp_linear(SPEED_STOPS, t)); };
    CHECK(speed(0.f) == SPEED_STOPS[0] && speed(1.f) == SPEED_STOPS[3]);
    CHECK(speed(1.f / 3.f) == SPEED_STOPS[1] && speed(2.f / 3.f) == SPEED_STOPS[2]);
    CHECK(Colors::ramp_display(DENSITY_STOPS, 0.f) == DENSITY_STOPS[0]);
    CHECK(Colors::ramp_display(DENSITY_STOPS, 0.5f) == DENSITY_STOPS[8]);
    CHECK(Colors::ramp_display(DENSITY_STOPS, 1.f) == DENSITY_STOPS[16]);
    // Midway, in linear RGB or in sRGB values
    const std::array<sf::Color, 2> black_white = {sf::Color::Black, sf::Color::White};
    CHECK(Colors::ramp_linear(black_white, 0.5f) == sf::Vector3f(0.5f, 0.5f, 0.5f));
    CHECK(Colors::ramp_display(black_white, 0.5f) == sf::Color(128, 128, 128));
    CHECK(speed(-1.f) == speed(0.f) && speed(2.f) == speed(1.f));  // clamped
    CHECK(Colors::ramp_display(DENSITY_STOPS, 5.f) == DENSITY_STOPS[16]);
}

void test_body_vertex() {
    sf::Vertex vertex;
    CHECK(BodyVertex::group(vertex) == 255);  // why every body's group is written at startup
    BodyVertex::set_group(vertex, 7);
    CHECK(vertex.color == sf::Color(7, 0, 0, 255) && !BodyVertex::highlighted(vertex));
    BodyVertex::set_highlight(vertex, true);
    CHECK(BodyVertex::highlighted(vertex) && BodyVertex::group(vertex) == 7);
    BodyVertex::set_highlight(vertex, true);
    CHECK(BodyVertex::highlighted(vertex));
    BodyVertex::set_highlight(vertex, false);
    CHECK(!BodyVertex::highlighted(vertex) && vertex.color == sf::Color(7, 0, 0, 255));
}

void test_render_groups() {
    using namespace Constants::Graphics;
    std::vector<std::string> ids = {"Sun", "Earth"};
    add_numbered(ids, "c-", 100);
    add_numbered(ids, "a-", 200);
    add_numbered(ids, "b-", 100);
    add_numbered(ids, "moonlet-", 36);  // numbered in sequence: a population
    const Bodies bodies = make_bodies(ids);
    {
        // Largest first, then by name, coloured by row; everything else is "Named"
        const RenderGroups groups = make_groups(bodies);
        const auto& table = groups.groups();
        CHECK(groups.legend() == std::vector<uint32_t>({0, 1, 2, 3}));
        CHECK(table.size() == 5);
        CHECK(table[0].name == "a" && table[1].name == "b" && table[2].name == "c");
        CHECK(table[3].name == "moonlet" && table[4].name == "Named");
        CHECK(table[0].count == 200 && table[1].count == 100 && table[3].count == 36);
        CHECK(table[4].count == 2 && !table[4].in_legend && table[0].in_legend);
        for (uint32_t k = 0; k < 4; k++) {
            CHECK(table[k].color == GROUP_PALETTE[k] && table[k].brightness == 1.f);
        }
        CHECK(table[4].color == NAMED_BODY_COLOR);
        CHECK(groups.of_body()[0] == 4 && groups.of_body()[1] == 4);
        CHECK(groups.of_body()[2] == 2 && groups.of_body()[102] == 0 && groups.of_body()[302] == 1);
        CHECK(groups.of_body()[402] == 3);
        CHECK(groups.unmatched_styles().empty());
        CHECK(groups.visibility() == std::vector<uint8_t>(5, 1));
    }
    {
        // A styled row keeps its palette slot; an exact id beats its population's style; styles
        // of named bodies get groups of their own, in order of first appearance
        const RenderGroups::Styles styles = {{"b", style(sf::Color::Red, 0.5f)},
                {"a-7", style(sf::Color::Blue)}, {"Earth", style(sf::Color::Green, 2.f)},
                {"Sun", style(sf::Color::Yellow)}, {"Pluto", style(sf::Color::Cyan)},
                {"x-3", style(sf::Color::Cyan)}};
        const RenderGroups groups = make_groups(bodies, styles);
        const auto& table = groups.groups();
        CHECK(table.size() == 8);
        CHECK(table[1].name == "b" && table[1].color == sf::Color::Red);
        CHECK(table[1].brightness == 0.5f);
        CHECK(table[0].color == GROUP_PALETTE[0] && table[2].color == GROUP_PALETTE[2]);
        CHECK(table[5].name == "Sun" && table[6].name == "Earth" && table[7].name == "a-7");
        CHECK(table[6].color == sf::Color::Green && table[6].brightness == 2.f);
        CHECK(!table[5].in_legend && !table[7].in_legend);
        CHECK(table[0].count == 199 && table[7].count == 1 && table[4].count == 0);
        CHECK(groups.of_body()[0] == 5 && groups.of_body()[1] == 6);
        CHECK(groups.of_body()[102 + 7] == 7 && groups.of_body()[102 + 8] == 0);
        CHECK(groups.unmatched_styles() == std::vector<std::string>({"Pluto", "x-3"}));
    }
}

void test_other_group() {
    using namespace Constants::Graphics;
    std::vector<std::string> ids;
    for (uint32_t k = 0; k < 10; k++) {
        add_numbered(ids, "p" + std::to_string(k) + "_", 100 + 10 * k);
    }
    ids.push_back("Sun");
    const Bodies bodies = make_bodies(ids);
    const RenderGroups groups =
            make_groups(bodies, {{"p0", style(sf::Color::Red)}, {"p9", style(sf::Color::Blue)}});
    const auto& table = groups.groups();
    // p9 (the largest) to p2 have rows of their own, p1 and p0 are "Other", whose style is lost
    CHECK(groups.legend().size() == LEGEND_GROUPS_MAX + 1 && table.size() == 10);
    CHECK(table[0].name == "p9" && table[0].color == sf::Color::Blue);
    CHECK(table[7].name == "p2" && table[7].color == GROUP_PALETTE[7]);
    CHECK(table[8].name == "Other" && table[8].color == OTHER_GROUP_COLOR);
    CHECK(table[8].count == 100 + 110 && table[8].in_legend);
    CHECK(table[9].name == "Named" && table[9].count == 1);
    CHECK(groups.of_body()[0] == 8 && groups.of_body()[100] == 8 && groups.of_body()[210] == 7);
    CHECK(groups.unmatched_styles().empty());
}

// Styles beyond the table's room fall back to the default group
void test_group_capacity() {
    using namespace Constants::Graphics;
    std::vector<std::string> ids;
    RenderGroups::Styles styles;
    for (uint32_t k = 0; k < MAX_RENDER_GROUPS + 2; k++) {
        ids.push_back("star" + std::to_string(k) + "-1");  // each in an id group of its own
        styles.emplace(ids.back(), style(sf::Color::Red));
    }
    const Bodies bodies = make_bodies(ids);
    const RenderGroups groups = make_groups(bodies, styles);
    CHECK(groups.groups().size() == MAX_RENDER_GROUPS);
    CHECK(groups.legend().empty() && groups.groups()[0].name == "Named");
    const auto& of_body = groups.of_body();
    CHECK(of_body[0] == 1 && of_body[MAX_RENDER_GROUPS - 2] == MAX_RENDER_GROUPS - 1);
    CHECK(of_body[MAX_RENDER_GROUPS - 1] == 0 && of_body[MAX_RENDER_GROUPS] == 0);
    CHECK(groups.groups()[0].count == 3);
    CHECK(groups.unmatched_styles().empty());
}

void test_visibility() {
    std::vector<std::string> ids = {"Sun"};
    add_numbered(ids, "a-", 300);
    add_numbered(ids, "b-", 200);
    add_numbered(ids, "c-", 100);
    const Bodies bodies = make_bodies(ids);
    RenderGroups groups = make_groups(bodies, {{"c", style(sf::Color::Red, 0.f)}});
    using V = std::vector<uint8_t>;
    CHECK(groups.visibility() == V({1, 1, 0, 1}));  // brightness 0: hidden
    CHECK(groups.toggle(0) && !groups.groups()[0].visible);
    CHECK(groups.visibility() == V({0, 1, 0, 1}));
    CHECK(groups.toggle(0) && groups.visibility() == V({1, 1, 0, 1}));
    CHECK(!groups.toggle(3) && !groups.toggle(99));  // not legend rows
    CHECK(groups.solo(1) && groups.visibility() == V({0, 1, 0, 1}));
    CHECK(!groups.groups()[2].visible && groups.groups()[3].visible);
    CHECK(!groups.solo(1) && !groups.solo(5));
    CHECK(groups.show_all() && groups.visibility() == V({1, 1, 0, 1}));
    CHECK(!groups.show_all());

    // The shader's table: unit luminance, 0 brightness while hidden, zeros past the groups
    groups.toggle(1);
    const auto table = style_table(groups.groups());
    CHECK(table.size() == Constants::Graphics::MAX_RENDER_GROUPS);
    const auto luminance = [](sf::Glsl::Vec4 v) {
        return Colors::luminance({v.x, v.y, v.z});
    };
    CHECK_NEAR(luminance(table[0]), 1.0, 1e-6);
    CHECK_NEAR(luminance(table[2]), 1.0, 1e-6);
    CHECK(table[0].w == 1.f && table[1].w == 0.f && table[2].w == 0.f && table[3].w == 1.f);
    CHECK(std::all_of(table.begin() + 4, table.end(), [](sf::Glsl::Vec4 v) {
        return v.x == 0.f && v.y == 0.f && v.z == 0.f && v.w == 0.f;
    }));
}

void test_trail_segments() {
    std::array<uint8_t, 256> visible{};
    visible[0] = 1;  // group 1 hidden
    // Where each body was, where it is, its group, and whether it draws a segment
    struct Move {
        sf::Vector2f from, to;
        uint8_t group;
        bool segment;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<Move> moves = {{{10.f, 10.f}, {10.5f, 10.f}, 0, false},  // under 1 px
            {{10.f, 10.f}, {11.f, 10.f}, 0, true},
            {{0.f, 0.f}, {4000.f, 500.f}, 0, true},  // 4031 px
            {{0.f, 0.f}, {4096.f, 100.f}, 0, false},  // over 4096 px
            {{-2e5f, 0.f}, {-2e5f + 10.f, 0.f}, 0, false},  // outside the guard box
            {{nan, 0.f}, {5.f, 5.f}, 0, false},
            {{0.f, 0.f}, {5.f, std::numeric_limits<float>::infinity()}, 0, false},
            {{0.f, 0.f}, {50.f, 50.f}, 1, false},  // hidden
            {{100.f, 100.f}, {90.f, 120.f}, 0, true}};
    sf::VertexArray bodies(sf::PrimitiveType::Points, moves.size());
    std::vector<sf::Vector2f> previous;
    for (size_t i = 0; i < moves.size(); i++) {
        bodies[i].position = moves[i].to;
        bodies[i].texCoords = {3.f * i, -4.f};
        BodyVertex::set_group(bodies[i], moves[i].group);
        previous.push_back(moves[i].from);
    }
    BodyVertex::set_highlight(bodies[8], true);
    sf::VertexArray segments(sf::PrimitiveType::Lines);
    segments.append({});  // appended to
    append_trail_segments(bodies, visible, previous, segments);
    CHECK(segments.getVertexCount() == 7);
    const auto is_segment = [&](size_t s, size_t i) {
        const sf::Vertex& a = segments[1 + 2 * s];
        const sf::Vertex& b = segments[2 + 2 * s];
        return a.position == moves[i].from && b.position == moves[i].to
               && a.color == bodies[i].color && b.color == bodies[i].color
               && a.texCoords == bodies[i].texCoords && b.texCoords == bodies[i].texCoords;
    };
    CHECK(is_segment(0, 1) && is_segment(1, 2) && is_segment(2, 8));
    CHECK(BodyVertex::highlighted(segments[5]) && BodyVertex::highlighted(segments[6]));
    // Every body's position is kept, hidden ones' included
    bool kept = true;
    for (size_t i = 0; i < moves.size(); i++) {
        kept &= previous[i] == moves[i].to;
    }
    CHECK(kept);
}

// The velocity scale's quantile, of drawn bodies on screen only
void test_velocity_quantile() {
    const sf::Vector2f window(100.f, 50.f);
    const std::vector<uint8_t> visible = {1, 0};
    sf::VertexArray bodies(sf::PrimitiveType::Points);
    std::vector<float> samples;
    for (int k = 1; k <= 100; k++) {
        sf::Vertex vertex({10.f, 10.f}, sf::Color(), {3.f * k, -4.f * k});  // speed 5 k
        BodyVertex::set_group(vertex, 0);
        bodies.append(vertex);
    }
    CHECK(velocity_quantile(bodies, visible, window, false, samples) == 5.f * 95.f);
    CHECK(velocity_quantile(bodies, visible, window, true, samples) == 4.f * 95.f);
    // Hidden or off-screen fast bodies don't count
    for (const sf::Vector2f pos : {sf::Vector2f(10.f, 10.f), {-1.f, 10.f}, {10.f, 50.f},
                 {100.f, 10.f}}) {
        for (int k = 0; k < 100; k++) {
            sf::Vertex vertex(pos, sf::Color(), {1e6f, 1e6f});
            BodyVertex::set_group(vertex, pos == sf::Vector2f(10.f, 10.f) ? 1 : 0);
            bodies.append(vertex);
        }
    }
    const float n = static_cast<float>(bodies.getVertexCount());
    CHECK(velocity_quantile(bodies, visible, window, false, samples) == 5.f * 95.f);
    CHECK(n == 500.f && samples.size() == 100);
    // Too few on screen: none
    sf::VertexArray few(sf::PrimitiveType::Points);
    for (int k = 0; k < 15; k++) {
        few.append(bodies[k]);
    }
    CHECK(!velocity_quantile(few, visible, window, false, samples));
}

void test_tone_curve() {
    using ToneMath::level;
    const float white = 37.f, beta = 2.f;
    CHECK_NEAR(level(white, white, beta), 1.0, 1e-6);
    CHECK(level(0.f, white, beta) == 0.f);
    bool monotonic = true;
    for (float intensity = 0.01f; intensity < 1e4f; intensity *= 1.1f) {
        monotonic &= level(intensity * 1.1f, white, beta) > level(intensity, white, beta);
    }
    CHECK(monotonic);
    CHECK(level(2.f * white, white, beta) > 1.f);
}

// A trail pixel fades to black in the fade time, whatever its brightness, linearly in display
// level; light takes it if shown at least as bright, or if it holds half a body that is, so that
// a newer trail replaces an older one; the selection's light takes it at full brightness
void test_trails() {
    using ToneMath::TrailPixel, ToneMath::trail_level, ToneMath::trail_step;
    const float white = 40.f, beta = 2.f, fade = 10.f;
    const sf::Vector3f color = Colors::unit_luminance({1.f, 0.5f, 0.2f});
    for (const float intensity : {40.f, 1.f}) {
        TrailPixel pixel = trail_step({}, intensity * color, 0.f, 0.01f, fade, white, beta);
        TrailPixel expected{intensity * color, 0.f, false};
        CHECK(pixel == expected);
        const float start = trail_level(pixel, fade, white, beta);
        for (int k = 0; k < 3; k++) {
            pixel = trail_step(pixel, {}, 0.f, 1.f, fade, white, beta);
        }
        expected.age = 3.f;
        CHECK(pixel == expected);
        CHECK_NEAR(trail_level(pixel, fade, white, beta), 0.7f * start, 1e-6);
        pixel = trail_step(pixel, {}, 0.f, 6.5f, fade, white, beta);
        CHECK(trail_level(pixel, fade, white, beta) > 0.f);
        CHECK(trail_step(pixel, {}, 0.f, 0.5f, fade, white, beta) == TrailPixel{});
    }
    const TrailPixel lit{color, 2.f, false};
    CHECK(trail_step(lit, {}, 0.f, 0.f, fade, white, beta) == lit);  // paused
    // A body's light at 0.9 of its level, then: over half of a body takes it, under half doesn't
    const TrailPixel old{color, 0.f, false};
    const TrailPixel taken{0.6f * color, 0.f, false};
    const TrailPixel kept{color, 1.f, false};
    CHECK(trail_step(old, 0.6f * color, 0.f, 1.f, fade, white, beta) == taken);
    CHECK(trail_step(old, 0.4f * color, 0.f, 1.f, fade, white, beta) == kept);
    // The selection, a quarter of a pixel of it: full brightness, until brighter light comes
    const sf::Vector3f select = 0.25f * color;
    const TrailPixel selected = trail_step(old, select, 0.25f, 1.f, fade, white, beta);
    const TrailPixel expected_selected{select, 0.f, true};
    CHECK(selected == expected_selected);
    CHECK_NEAR(trail_level(trail_step(selected, {}, 0.f, 1.f, fade, white, beta), fade, white,
                       beta),
            0.9, 1e-6);
    CHECK(trail_step(selected, 30.f * color, 0.f, 1.f, fade, white, beta).light == 30.f * color);
    CHECK(trail_step(selected, 20.f * color, 0.f, 1.f, fade, white, beta).selected);
}

void test_white_from_histogram() {
    using namespace Constants::Graphics;
    const auto white = [](const std::vector<float>& histogram,
                               float quantile = AUTO_WHITE_QUANTILE, float min_white = 0.f) {
        return ToneMath::white_from_histogram(histogram, quantile, min_white, EXPOSURE_LOG2_MIN,
                EXPOSURE_BINS_PER_OCTAVE);
    };
    const auto bin_value = [](float bin) {  // where a bin, or a point in it, starts
        return std::exp2(EXPOSURE_LOG2_MIN + bin / EXPOSURE_BINS_PER_OCTAVE);
    };
    std::vector<float> histogram(EXPOSURE_HISTOGRAM_BINS, 0.f);
    CHECK(!white(histogram));
    // A single bin: near its top, 0.1% of its blocks above
    histogram[40] = 100.f;
    CHECK_NEAR(*white(histogram), bin_value(41.f - 0.001f), 1e-5);
    // Uniform
    std::fill(histogram.begin(), histogram.end(), 1.f);
    CHECK_NEAR(*white(histogram) / bin_value(128.f - 0.128f), 1.0, 1e-5);
    // Right at a bin edge (exactly, in floats): the top of the bin below it
    std::fill(histogram.begin(), histogram.end(), 0.f);
    histogram[10] = 3.f;
    histogram[50] = 1.f;
    CHECK_NEAR(*white(histogram, 0.75f), bin_value(11.f), 1e-6);
    // The floor
    std::fill(histogram.begin(), histogram.end(), 0.f);
    histogram[0] = 5.f;
    CHECK(*white(histogram, AUTO_WHITE_QUANTILE, 1.f) == 1.f);
    CHECK(*white(histogram) < 1.f);
}

void test_quantile_and_smoothing() {
    std::vector<float> values(100);
    std::iota(values.begin(), values.end(), 1.f);
    std::shuffle(values.begin(), values.end(), std::mt19937(7));
    CHECK(ToneMath::quantile(values, 0.95) == 95.f);
    CHECK(ToneMath::quantile(values, 0.0) == 1.f && ToneMath::quantile(values, 1.0) == 100.f);
    std::vector<float> one = {3.f};
    CHECK(ToneMath::quantile(one, 0.95) == 3.f);
    // Halfway in log space after ln 2 time constants, and nearly there after many
    CHECK_NEAR(ToneMath::smoothed(1.0, 4.0, std::log(2.0), 1.0), 2.0, 1e-12);
    CHECK_NEAR(ToneMath::smoothed(1.0, 4.0, 100.0, 1.0), 4.0, 1e-9);
    CHECK_NEAR(ToneMath::smoothed(5.0, 5.0, 0.5, 1.0), 5.0, 1e-12);
}
}  // namespace


int main() {
    Log::verbosity = Log::Verbosity::ERROR;
    test_srgb();
    test_unit_luminance();
    test_display_hue();
    test_ramps();
    test_body_vertex();
    test_render_groups();
    test_other_group();
    test_group_capacity();
    test_visibility();
    test_trail_segments();
    test_velocity_quantile();
    test_tone_curve();
    test_trails();
    test_white_from_histogram();
    test_quantile_and_smoothing();
    return Check::result();
}
