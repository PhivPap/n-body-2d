#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <optional>
#include <random>
#include <vector>

#include "Check.hpp"
#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"
#include "Render/BodyVertex.hpp"
#include "Render/Colors.hpp"
#include "Render/FloatTarget.hpp"
#include "Render/HdrRenderer.hpp"
#include "Render/RenderGroups.hpp"
#include "Render/ToneMath.hpp"
#include "SFML/Window/Context.hpp"

using ColorMode = Config::Graphics::ColorMode;


namespace {
constexpr sf::Vector2u SIZE(256, 192);  // not a multiple of the exposure blocks' 8 px

// A body to draw: its window position, render group, highlight and velocity (m/s)
struct Body {
    sf::Vector2f pos;
    uint8_t group = 0;
    bool highlight = false;
    sf::Vector2f velocity{};
};

sf::VertexArray vertices(const std::vector<Body>& bodies) {
    sf::VertexArray array(sf::PrimitiveType::Points, bodies.size());
    for (size_t i = 0; i < bodies.size(); i++) {
        array[i].position = bodies[i].pos;
        array[i].texCoords = bodies[i].velocity;
        BodyVertex::set_group(array[i], bodies[i].group);
        BodyVertex::set_highlight(array[i], bodies[i].highlight);
    }
    return array;
}

// `count` bodies at the centre of pixel (x, y), where a tent splat puts all of its light
std::vector<Body> stack(uint32_t x, uint32_t y, uint32_t count, uint8_t group = 0) {
    return std::vector<Body>(count, Body{.pos = {x + 0.5f, y + 0.5f}, .group = group});
}

HdrRenderer::Frame frame(ColorMode mode = ColorMode::GROUPS) {
    return {.color_mode = mode, .body_diameter_px = 1, .trails_fade_s = 0.0, .paused = false,
            .frame_time_s = 0.01, .velocity_scale = 1.f, .exposure_ev = 0.0,
            .locked_white = std::nullopt, .stretch = 1.f, .bloom = false};
}

RenderGroup group(sf::Color color, float brightness = 1.f, bool visible = true) {
    return {.name = "", .count = 1, .color = color, .brightness = brightness, .visible = visible,
            .in_legend = true};
}

// A pixel of the HDR image the last frame showed
sf::Vector3f pixel(HdrRenderer& renderer, uint32_t x, uint32_t y) {
    const std::vector<float> rgba =
            renderer.read_image({{static_cast<int>(x), static_cast<int>(y)}, {1, 1}});
    return {rgba[0], rgba[1], rgba[2]};
}

// A pixel's alpha in the HDR image the last frame showed
float alpha(HdrRenderer& renderer, uint32_t x, uint32_t y) {
    return renderer.read_image({{static_cast<int>(x), static_cast<int>(y)}, {1, 1}})[3];
}

// The image's luminance summed over a square around (x, y)
float luminance_around(HdrRenderer& renderer, uint32_t x, uint32_t y, int radius) {
    const std::vector<float> rgba = renderer.read_image({{static_cast<int>(x) - radius,
            static_cast<int>(y) - radius}, {2 * radius + 1, 2 * radius + 1}});
    float sum = 0.f;
    for (size_t k = 0; k < rgba.size(); k += 4) {
        sum += Colors::luminance({rgba[k], rgba[k + 1], rgba[k + 2]});
    }
    return sum;
}

bool near(sf::Vector3f a, sf::Vector3f b, float relative) {
    const float scale = std::max({std::abs(b.x), std::abs(b.y), std::abs(b.z), 1e-6f});
    return std::abs(a.x - b.x) <= relative * scale && std::abs(a.y - b.y) <= relative * scale
           && std::abs(a.z - b.z) <= relative * scale;
}

void test_float_targets(HdrRenderer& renderer, sf::RenderTexture& window) {
    sf::RenderTexture target;
    resize_float_target(target, {4, 4});
    const std::vector<float> zero = read_pixels(target, {{0, 0}, {4, 4}});
    CHECK(std::all_of(zero.begin(), zero.end(), [](float v) { return v == 0.f; }));
    // 5000 bodies add up exactly: no 8-bit or half-float rounding
    renderer.set_groups(std::vector{group(sf::Color::White)});
    renderer.draw(window, vertices(stack(10, 20, 5000)), frame(ColorMode::WHITE));
    CHECK(pixel(renderer, 10, 20) == sf::Vector3f(5000.f, 5000.f, 5000.f));
    CHECK(pixel(renderer, 11, 20) == sf::Vector3f());
}

void test_deposits(HdrRenderer& renderer, sf::RenderTexture& window) {
    using namespace Constants::Graphics;
    const sf::Color blue(0xc4, 0xd6, 0xff);
    const sf::Vector3f unit_blue = Colors::unit_luminance(Colors::to_linear(blue));
    std::vector<RenderGroup> groups(MAX_RENDER_GROUPS, group(sf::Color::White, 1.f, false));
    groups[0] = group(blue, 2.f);
    groups[1] = group(blue, 1.f, false);
    renderer.set_groups(std::vector(groups.begin(), groups.begin() + 2));
    std::vector<Body> bodies = {{.pos = {5.5f, 5.5f}, .group = 0},
            {.pos = {7.5f, 5.5f}, .group = 1},
            {.pos = {9.5f, 5.5f}, .group = 0, .highlight = true},
            {.pos = {11.5f, 5.5f}, .group = 255}};
    renderer.draw(window, vertices(bodies), frame());
    CHECK(near(pixel(renderer, 5, 5), 2.f * unit_blue, 1e-5f));
    CHECK(pixel(renderer, 7, 5) == sf::Vector3f());  // hidden
    const sf::Vector3f select = Colors::unit_luminance(Colors::to_linear(SELECT_COLOR));
    CHECK(near(pixel(renderer, 9, 5), 2.f * select, 1e-5f));
    CHECK(alpha(renderer, 9, 5) == 1.f && alpha(renderer, 5, 5) == 0.f);  // the selection's
    CHECK(pixel(renderer, 11, 5) == sf::Vector3f());  // group 255 reads the last, unused, entry
    groups[MAX_RENDER_GROUPS - 1] = group(sf::Color::Red, 3.f);
    renderer.set_groups(groups);
    renderer.draw(window, vertices(bodies), frame());
    const sf::Vector3f unit_red = Colors::unit_luminance(Colors::to_linear(sf::Color::Red));
    CHECK(near(pixel(renderer, 11, 5), 3.f * unit_red, 1e-5f));
    // White mode: every body alike, the highlight aside
    renderer.draw(window, vertices(bodies), frame(ColorMode::WHITE));
    CHECK(near(pixel(renderer, 5, 5), {1.f, 1.f, 1.f}, 1e-5f));
    CHECK(pixel(renderer, 7, 5) == sf::Vector3f());
    CHECK(near(pixel(renderer, 9, 5), select, 1e-5f));

    // A tent splat's weights sum to 1 wherever the body is
    renderer.set_groups(std::vector{group(sf::Color::White)});
    std::mt19937 random(5);
    std::uniform_real_distribution<float> offset(0.f, 1.f);
    float worst = 0.f;
    for (int k = 0; k < 20; k++) {
        const sf::Vector2f pos(100.f + offset(random), 80.f + offset(random));
        renderer.draw(window, vertices({{.pos = pos}}), frame(ColorMode::WHITE));
        worst = std::max(worst, std::abs(luminance_around(renderer, 100, 80, 2) - 1.f));
    }
    CHECK(worst < 1e-5f);
    // A disc: its area in pixels, each fully lit inside
    HdrRenderer::Frame disc = frame(ColorMode::WHITE);
    disc.body_diameter_px = 9;
    renderer.draw(window, vertices({{.pos = {100.3f, 80.6f}}}), disc);
    const float area = std::numbers::pi_v<float> * 4.5f * 4.5f;
    CHECK(std::abs(luminance_around(renderer, 100, 80, 7) / area - 1.f) < 0.03f);
    CHECK(near(pixel(renderer, 100, 80), {1.f, 1.f, 1.f}, 1e-5f));
}

// A trail pixel of row 10 as the trail pass keeps it: alpha is the age, -1 - age for the selection
ToneMath::TrailPixel trail(HdrRenderer& renderer, uint32_t x) {
    const std::vector<float> rgba = renderer.read_image({{static_cast<int>(x), 10}, {1, 1}});
    const bool selected = rgba[3] < 0.f;
    return {{rgba[0], rgba[1], rgba[2]}, selected ? -1.f - rgba[3] : rgba[3], selected};
}

// The trails age and take new light as ToneMath models them, freeze while paused, are gone at the
// fade time however bright, and a reset clears them
void test_trails(HdrRenderer& renderer, sf::RenderTexture& window) {
    const float white = 40.f, beta = Constants::Graphics::TONE_SOFTENING;
    renderer.set_groups(std::vector{group(sf::Color(0xff, 0xa0, 0x40))});
    HdrRenderer::Frame trails = frame();
    trails.trails_fade_s = 2.0;
    trails.locked_white = white;
    // Pixels of 40 (at white), 20, 5 and 1 bodies, one that gets new light next frame, and a
    // selected body
    std::vector<Body> first;
    for (const auto [x, count] :
            {std::pair{10u, 40u}, {12u, 20u}, {14u, 5u}, {16u, 1u}, {18u, 3u}}) {
        const auto bodies = stack(x, 10, count);
        first.insert(first.end(), bodies.begin(), bodies.end());
    }
    first.push_back({.pos = {20.5f, 10.5f}, .highlight = true});
    renderer.reset_trails();
    renderer.draw(window, vertices(first), trails);
    std::vector<ToneMath::TrailPixel> before;
    for (uint32_t x = 10; x <= 20; x += 2) {
        before.push_back(trail(renderer, x));
    }
    CHECK(before[0].age == 0.f && !before[0].selected && before[5].selected);
    // A fifth of the fade time
    trails.frame_time_s = 0.4;
    renderer.draw(window, vertices(stack(18, 10, 30)), trails);
    bool matches = true;
    for (uint32_t k = 0; k < before.size(); k++) {
        const sf::Vector3f light = k == 4 ? 10.f * before[4].light : sf::Vector3f();
        const ToneMath::TrailPixel expected =
                ToneMath::trail_step(before[k], light, 0.f, 0.4f, 2.f, white, beta);
        const ToneMath::TrailPixel shown = trail(renderer, 10 + 2 * k);
        matches &= near(shown.light, expected.light, 1e-5f)
                   && std::abs(shown.age - expected.age) < 1e-6f
                   && shown.selected == expected.selected;
    }
    CHECK(matches && trail(renderer, 18).age == 0.f && trail(renderer, 10).age > 0.f);
    // Paused: frozen, exactly
    std::vector<float> frozen = renderer.read_image({{10, 10}, {11, 1}});
    trails.paused = true;
    for (int k = 0; k < 3; k++) {
        renderer.draw(window, vertices({}), trails);
    }
    CHECK(renderer.read_image({{10, 10}, {11, 1}}) == frozen);
    // At the fade time, black however bright; the newer light lasts its own fade time
    trails.paused = false;
    trails.frame_time_s = 1.6;
    renderer.draw(window, vertices({}), trails);
    CHECK(trail(renderer, 10) == ToneMath::TrailPixel{} && trail(renderer, 20).light == sf::Vector3f()
            && trail(renderer, 18).age > 0.f);
    trails.frame_time_s = 0.4;
    renderer.draw(window, vertices({}), trails);
    CHECK(trail(renderer, 18) == ToneMath::TrailPixel{});
    // Over half of a body of another colour takes a pixel its body outshines, the same colour
    // doesn't: a trail overlapping itself keeps its brightest light
    const sf::Color blue(0x40, 0x80, 0xff);
    renderer.set_groups(std::vector{group(sf::Color(0xff, 0xa0, 0x40)), group(blue)});
    trails.frame_time_s = 0.01;
    renderer.reset_trails();
    renderer.draw(window, vertices({{.pos = {40.5f, 10.5f}}, {.pos = {44.5f, 10.5f}}}), trails);
    const ToneMath::TrailPixel kept = trail(renderer, 44);
    renderer.draw(window, vertices({{.pos = {40.8f, 10.5f}, .group = 1}, {.pos = {44.8f, 10.5f}}}),
            trails);
    // Laid over the orange as far as it leaves it uncovered: a body's light all in all
    const ToneMath::TrailPixel taken = trail(renderer, 40);
    CHECK(taken.age == 0.f && taken.light.z > taken.light.x
            && std::abs(Colors::luminance(taken.light) - 1.f) < 0.01f);
    CHECK(trail(renderer, 44).light == kept.light && trail(renderer, 44).age > 0.f);
    // A reset clears everything
    renderer.draw(window, vertices(stack(30, 10, 40)), trails);
    renderer.reset_trails();
    trails.frame_time_s = 0.01;
    renderer.draw(window, vertices({}), trails);
    CHECK(pixel(renderer, 30, 10) == sf::Vector3f());
}

// A trail segment is the body swept from where it was: its profile across the line, as bright as
// the body all along and at its head (not twice), keeping the brightest light where segments
// cross; none on the first frame of trails, after a reset or while paused
void test_segments(HdrRenderer& renderer, sf::RenderTexture& window) {
    renderer.set_groups(std::vector{group(sf::Color::White)});
    HdrRenderer::Frame trails = frame(ColorMode::WHITE);
    trails.trails_fade_s = 1.0;
    trails.frame_time_s = 0.0;  // no fading
    trails.locked_white = 1.f;
    // Segments' ends go through clip space: their light is exact to about 1e-5
    const auto row = [&renderer](uint32_t x0, uint32_t x1, uint32_t y, float light) {
        bool all = true;
        for (uint32_t x = x0; x <= x1; x++) {
            const sf::Vector3f p = pixel(renderer, x, y);
            all &= std::max({std::abs(p.x - light), std::abs(p.y - light), std::abs(p.z - light)})
                   < 1e-4f;
        }
        return all;
    };
    const auto moved = [&](std::vector<Body> from, std::vector<Body> to) {
        renderer.reset_trails();
        renderer.draw(window, vertices(from), trails);
        renderer.draw(window, vertices(to), trails);
    };
    renderer.reset_trails();
    renderer.draw(window, vertices({{.pos = {20.5f, 60.5f}}}), trails);
    CHECK(std::abs(luminance_around(renderer, 40, 60, 25) - 1.f) < 1e-5f);  // the dot alone
    // Through pixel centres: 1 from end to end, its head included, and nothing beside
    renderer.draw(window, vertices({{.pos = {60.5f, 60.5f}}}), trails);
    CHECK(row(20, 60, 60, 1.f) && row(20, 60, 59, 0.f) && row(20, 60, 61, 0.f));
    // Between two rows: half the light in each, all along
    moved({{.pos = {20.5f, 100.f}}}, {{.pos = {60.5f, 100.f}}});
    CHECK(row(20, 60, 99, 0.5f) && row(20, 60, 100, 0.5f) && row(20, 60, 101, 0.f));
    // Bodies 5 px wide: a band 5 px wide, fully lit
    trails.body_diameter_px = 5;
    moved({{.pos = {20.5f, 140.5f}}}, {{.pos = {60.5f, 140.5f}}});
    bool band = row(22, 58, 137, 0.f) && row(22, 58, 143, 0.f);
    for (uint32_t y = 138; y <= 142; y++) {
        band &= row(20, 60, y, 1.f);
    }
    CHECK(band);
    // Two bodies moving together leave their light added up all along, as at their heads; a body
    // entering a crowd brings none of the crowd's light out of it
    moved({{.pos = {20.5f, 140.5f}}, {.pos = {20.5f, 140.5f}}},
            {{.pos = {60.5f, 140.5f}}, {.pos = {60.5f, 140.5f}}});
    CHECK(row(20, 60, 140, 2.f));
    std::vector<Body> crowd = stack(100, 140, 10);
    crowd.push_back({.pos = {60.5f, 140.5f}});
    std::vector<Body> entered = crowd;
    entered.back().pos = {100.5f, 140.5f};
    moved(crowd, entered);
    CHECK(row(60, 95, 140, 1.f) && near(pixel(renderer, 100, 140), {11.f, 11.f, 11.f}, 1e-5f));
    trails.body_diameter_px = 1;
    // Diagonal: the line's pixels as bright as the body, also where another segment crosses it;
    // a selected body's segment shows the selection
    moved({{.pos = {100.5f, 100.5f}}, {.pos = {120.5f, 100.5f}},
                  {.pos = {100.5f, 170.5f}, .highlight = true}},
            {{.pos = {140.5f, 140.5f}}, {.pos = {120.5f, 140.5f}},
                    {.pos = {140.5f, 170.5f}, .highlight = true}});
    bool diagonal = true;
    for (uint32_t k = 0; k <= 40; k++) {
        diagonal &= near(pixel(renderer, 100 + k, 100 + k), {1.f, 1.f, 1.f}, 1e-4f);
    }
    CHECK(diagonal && alpha(renderer, 120, 170) == -1.f && alpha(renderer, 110, 110) == 0.f);
    // Paused: no segment
    trails.paused = true;
    renderer.draw(window, vertices({{.pos = {60.5f, 180.5f}}}), trails);
    CHECK(pixel(renderer, 60, 150) == sf::Vector3f());
    // After a reset, the first frame has none either
    trails.paused = false;
    renderer.reset_trails();
    renderer.draw(window, vertices({{.pos = {60.5f, 60.5f}}}), trails);
    CHECK(pixel(renderer, 60, 120) == sf::Vector3f() && pixel(renderer, 40, 60) == sf::Vector3f());
}

// Speed and Doppler colours match the CPU ramps; the density colormap spans inferno
void test_color_modes(HdrRenderer& renderer, sf::RenderTexture& window) {
    using namespace Constants::Graphics;
    renderer.set_groups(std::vector{group(sf::Color::White, 2.f)});
    HdrRenderer::Frame velocity = frame(ColorMode::SPEED);
    velocity.velocity_scale = 1000.f;
    std::vector<Body> bodies;
    const std::vector<float> ts = {0.f, 0.2f, 0.5f, 0.8f, 1.f, 1.7f};
    for (size_t k = 0; k < ts.size(); k++) {
        // Speed t * scale, in any direction; and a Doppler velocity that gives t
        const float angle = 0.7f * k;
        bodies.push_back({.pos = {10.5f + 4 * k, 100.5f},
                .velocity = 1000.f * ts[k] * sf::Vector2f(std::cos(angle), std::sin(angle))});
        bodies.push_back({.pos = {10.5f + 4 * k, 110.5f},
                .velocity = {123.f, 2000.f * (0.5f - ts[k])}});
    }
    renderer.draw(window, vertices(bodies), velocity);
    bool speed = true;
    for (size_t k = 0; k < ts.size(); k++) {
        const sf::Vector3f ramp = Colors::unit_luminance(Colors::ramp_linear(SPEED_STOPS, ts[k]));
        speed &= near(pixel(renderer, 10 + 4 * k, 100), 2.f * ramp, 1e-4f);
    }
    CHECK(speed);
    velocity.color_mode = ColorMode::DOPPLER;
    renderer.draw(window, vertices(bodies), velocity);
    bool doppler = true;
    for (size_t k = 0; k < ts.size(); k++) {
        const sf::Vector3f ramp = Colors::unit_luminance(Colors::ramp_linear(DOPPLER_STOPS, ts[k]));
        doppler &= near(pixel(renderer, 10 + 4 * k, 110), 2.f * ramp, 1e-4f);
    }
    CHECK(doppler);
    // Up the screen (v.y < 0) recedes: red
    const sf::Vector3f receding = pixel(renderer, 10 + 4 * 4, 110);
    CHECK(receding.x > 3.f * receding.z);

    // Density: inferno from just above black to white, every body alike
    HdrRenderer::Frame density = frame(ColorMode::DENSITY);
    density.locked_white = 16.f;
    std::vector<Body> stacked = stack(60, 30, 16);
    stacked.push_back({.pos = {50.5f + 0.995f, 30.5f}});  // a 0.005 weight on pixel (50, 30)
    window.clear(sf::Color::Black);
    renderer.draw(window, vertices(stacked), density);
    window.display();
    const sf::Image image = window.getTexture().copyToImage();
    const auto close = [](sf::Color a, sf::Color b) {
        return std::abs(a.r - b.r) <= 1 && std::abs(a.g - b.g) <= 1 && std::abs(a.b - b.b) <= 1;
    };
    CHECK(close(image.getPixel({50, 30}), DENSITY_STOPS[0]));
    CHECK(close(image.getPixel({60, 30}), DENSITY_STOPS[std::size(DENSITY_STOPS) - 1]));
    CHECK(image.getPixel({70, 30}) == sf::Color::Black);
}

// Bloom: a bright pixel glows around itself, symmetrically and fading with distance, every level
// of the pyramid adding about the light above the threshold; twice that light glows twice as much;
// light under the threshold doesn't glow, and bloom leaves no trace once off
void test_bloom(HdrRenderer& renderer, sf::RenderTexture& window) {
    using namespace Constants::Graphics;
    renderer.set_groups(std::vector{group(sf::Color::White)});
    HdrRenderer::Frame bright_frame = frame(ColorMode::WHITE);
    bright_frame.locked_white = 2.f;
    bright_frame.bloom = true;
    const auto shot = [&](const std::vector<Body>& bodies, const HdrRenderer::Frame& f) {
        window.clear(sf::Color::Black);
        renderer.draw(window, vertices(bodies), f);
        window.display();
        return window.getTexture().copyToImage();
    };
    // 64 bodies at (128, 96): the 4x4 boxes holding them average 2x the white point. At half
    // size, the glow is centred between pixels 63 and 64, and rows 47 and 48.
    const sf::IntRect half({0, 0}, {static_cast<int>(SIZE.x / 2), static_cast<int>(SIZE.y / 2)});
    shot(stack(128, 96, 64), bright_frame);
    const std::vector<float> glow = renderer.read_bloom(half);
    const auto at = [&](const std::vector<float>& image, int x, int y) {
        return image[4 * ((48 + y) * half.size.x + (64 + x))];
    };
    bool symmetric = true, falling = true;
    for (int d = 0; d < 30; d++) {
        const float right = at(glow, d, 0), left = at(glow, -1 - d, 0);
        const float down = at(glow, 0, d), up = at(glow, 0, -1 - d);
        symmetric &= std::abs(right - left) <= 0.02f * right && std::abs(down - up) <= 0.02f * down
                     && std::abs(right - down) <= 0.02f * right;
        falling &= at(glow, d + 1, 0) < at(glow, d, 0);
    }
    CHECK(symmetric && falling && at(glow, 30, 0) > 0.f);
    float total = 0.f;  // in half-size pixels, each of 4 pixels
    for (size_t k = 0; k < glow.size(); k += 4) {
        total += Colors::luminance({glow[k], glow[k + 1], glow[k + 2]});
    }
    CHECK(std::abs(4.f * total / (BLOOM_LEVELS * 64.f) - 1.f) < 0.15f);
    shot(stack(128, 96, 128), bright_frame);
    const std::vector<float> brighter = renderer.read_bloom(half);
    CHECK(std::abs(at(brighter, 5, 3) / at(glow, 5, 3) - 2.f) < 1e-3f);
    // Light under the threshold: no glow at all
    const std::vector<Body> dim = stack(100, 50, 2);
    shot(dim, bright_frame);
    const std::vector<float> none = renderer.read_bloom(half);
    CHECK(std::all_of(none.begin(), none.end(), [](float v) { return v == 0.f; }));
    // Without bloom, the same frame of the dither (1024 frames on) after bloom ran is the same
    HdrRenderer::Frame plain = bright_frame;
    plain.bloom = false;
    const sf::Image before = shot(dim, plain);
    for (int k = 0; k < 1023; k++) {
        renderer.draw(window, vertices(stack(128, 96, 64)), bright_frame);
    }
    const sf::Image after = shot(dim, plain);
    CHECK(std::equal(before.getPixelsPtr(), before.getPixelsPtr() + 4 * SIZE.x * SIZE.y,
            after.getPixelsPtr()));
}

// Block maxima of `counts` bodies, one block each
std::vector<Body> blocks_of(const std::vector<uint32_t>& counts) {
    const uint32_t per_row = SIZE.x / Constants::Graphics::EXPOSURE_BLOCK_PX;
    std::vector<Body> bodies;
    for (uint32_t k = 0; k < counts.size(); k++) {
        const uint32_t x = (k % per_row) * Constants::Graphics::EXPOSURE_BLOCK_PX + 3;
        const uint32_t y = (k / per_row) * Constants::Graphics::EXPOSURE_BLOCK_PX + 5;
        const auto stacked = stack(x, y, counts[k]);
        bodies.insert(bodies.end(), stacked.begin(), stacked.end());
    }
    return bodies;
}

void test_exposure(HdrRenderer& renderer, sf::RenderTexture& window) {
    using namespace Constants::Graphics;
    renderer.set_groups(std::vector{group(sf::Color::White)});
    // Log-uniform block maxima from 1 to ~1000 bodies
    std::mt19937 random(11);
    std::uniform_real_distribution<float> log2_count(0.f, 10.f);
    std::vector<uint32_t> counts(700);
    for (uint32_t& count : counts) {
        count = static_cast<uint32_t>(std::exp2(log2_count(random)));
    }
    std::vector<float> maxima(counts.begin(), counts.end());
    const float expected = ToneMath::quantile(maxima, AUTO_WHITE_QUANTILE);
    renderer.resize(SIZE);  // snaps
    renderer.draw(window, vertices(blocks_of(counts)), frame());
    const float measured = renderer.read_white_point();
    CHECK(std::abs(std::log2(measured / expected)) <= 1.f / EXPOSURE_BINS_PER_OCTAVE);
    // Nothing visible: kept
    renderer.draw(window, vertices({}), frame());
    CHECK(renderer.read_white_point() == measured);
    // Followed in log space: halfway after ln 2 time constants
    HdrRenderer::Frame slow = frame();
    slow.frame_time_s = std::chrono::duration<double>(EXPOSURE_ADAPT_TIME).count() * std::log(2.0);
    std::vector<uint32_t> bright(counts.size(), 4000);
    renderer.draw(window, vertices(blocks_of(bright)), slow);
    const float halfway = std::sqrt(measured * 4000.f);
    const auto octaves = [](float a, float b) { return std::abs(std::log2(a / b)); };
    CHECK(octaves(renderer.read_white_point(), halfway) <= 0.5f / EXPOSURE_BINS_PER_OCTAVE);
    // A resize snaps to the measured value, which is at least AUTO_WHITE_MIN
    renderer.resize(SIZE);
    renderer.draw(window, vertices(blocks_of(bright)), slow);
    CHECK(octaves(renderer.read_white_point(), 4000.f) <= 1.f / EXPOSURE_BINS_PER_OCTAVE);
    // This frame's light, not the trails' (which still hold the bright blocks)
    HdrRenderer::Frame trails = slow;
    trails.trails_fade_s = 100.0;
    trails.frame_time_s = 10.0;  // the white point follows at once
    renderer.draw(window, vertices(blocks_of(bright)), trails);
    renderer.draw(window, vertices(stack(50, 50, 3)), trails);
    CHECK(octaves(renderer.read_white_point(), 3.f) <= 1.f / EXPOSURE_BINS_PER_OCTAVE);
    renderer.set_groups(std::vector{group(sf::Color::White, 0.1f)});
    renderer.resize(SIZE);
    renderer.draw(window, vertices(stack(50, 50, 3)), frame());
    CHECK(renderer.read_white_point() == AUTO_WHITE_MIN);
}

void test_tonemap(HdrRenderer& renderer, sf::RenderTexture& window) {
    sf::Image image;
    const auto shown = [&image](uint32_t x, uint32_t y) { return image.getPixel({x, y}); };
    renderer.set_groups(std::vector{group(sf::Color::White), group(sf::Color::Red)});
    HdrRenderer::Frame locked = frame();
    locked.locked_white = 16.f;
    std::vector<Body> bodies;
    for (const auto [x, count] :
            {std::pair{20u, 1u}, {22u, 2u}, {24u, 4u}, {26u, 8u}, {28u, 16u}}) {
        const auto stacked = stack(x, 30, count);
        bodies.insert(bodies.end(), stacked.begin(), stacked.end());
    }
    const auto red = stack(40, 30, 4, 1);
    bodies.insert(bodies.end(), red.begin(), red.end());
    window.clear(sf::Color::Black);
    renderer.draw(window, vertices(bodies), locked);
    window.display();
    image = window.getTexture().copyToImage();
    CHECK(shown(21, 30) == sf::Color::Black && shown(5, 5) == sf::Color::Black);  // no dither
    CHECK(shown(28, 30).r >= 254 && shown(28, 30).g >= 254 && shown(28, 30).b >= 254);
    bool monotonic = true;
    for (uint32_t x = 20; x < 28; x += 2) {
        monotonic &= shown(x + 2, 30).r > shown(x, 30).r + 2;
    }
    CHECK(monotonic && shown(20, 30).r > 10);
    const sf::Color pure_red = shown(40, 30);
    CHECK(pure_red.r > 100 && pure_red.g <= 1 && pure_red.b <= 1);
    // EV: +1 halves the white point
    HdrRenderer::Frame brighter = locked;
    brighter.exposure_ev = 1.0;
    window.clear(sf::Color::Black);
    renderer.draw(window, vertices(stack(28, 30, 8)), brighter);
    window.display();
    image = window.getTexture().copyToImage();
    CHECK(shown(28, 30).r >= 254);
    // A stretch of 16: a single body lifted from 44 to over 120, the white point still white
    HdrRenderer::Frame stretched = locked;
    stretched.stretch = 16.f;
    window.clear(sf::Color::Black);
    renderer.draw(window, vertices(bodies), stretched);
    window.display();
    image = window.getTexture().copyToImage();
    CHECK(shown(20, 30).r > 120 && shown(28, 30).r >= 254);
    // Over the grid's grey: light as bright as it hides it, fainter light doesn't darken it (a
    // single body is shown at 44/255), and unlit pixels leave it
    const sf::Color grey(64, 64, 64);
    window.clear(grey);
    renderer.draw(window, vertices(bodies), locked);
    window.display();
    image = window.getTexture().copyToImage();
    CHECK(shown(5, 5) == grey && std::abs(shown(20, 30).r - 64) <= 1);
    CHECK(shown(28, 30).r >= 254 && shown(40, 30).r > 100 && shown(40, 30).g <= 1);
    // Trails half way through their fade: at half their level, the selection's at half its colour
    HdrRenderer::Frame trails = locked;
    trails.trails_fade_s = 2.0;
    std::vector<Body> lit = stack(28, 30, 16);
    lit.push_back({.pos = {50.5f, 30.5f}, .highlight = true});
    renderer.reset_trails();
    renderer.draw(window, vertices(lit), trails);
    trails.frame_time_s = 1.0;
    window.clear(sf::Color::Black);
    renderer.draw(window, vertices({}), trails);
    window.display();
    image = window.getTexture().copyToImage();
    CHECK(std::abs(shown(28, 30).r - 128) <= 2 && std::abs(shown(50, 30).r - 128) <= 2
            && shown(50, 30).g <= 1);
    // Selected bodies: the selection colour at full brightness in every mode, from a quarter of a
    // pixel (a body on the corner of four)
    const std::vector<Body> selected = {{.pos = {50.5f, 30.5f}, .highlight = true},
            {.pos = {60.f, 30.f}, .highlight = true}};
    for (const ColorMode mode : {ColorMode::GROUPS, ColorMode::DENSITY}) {
        HdrRenderer::Frame f = locked;
        f.color_mode = mode;
        window.clear(sf::Color::Black);
        renderer.draw(window, vertices(selected), f);
        window.display();
        image = window.getTexture().copyToImage();
        using Constants::Graphics::SELECT_COLOR;
        CHECK(shown(50, 30) == SELECT_COLOR && shown(59, 29) == SELECT_COLOR
                && shown(60, 30) == SELECT_COLOR && shown(52, 30) == sf::Color::Black);
    }
}
}  // namespace


int main() {
    Log::verbosity = Log::Verbosity::WARNING;
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) {
        fmt::println("Skipped: no display (neither DISPLAY nor WAYLAND_DISPLAY is set)");
        return 77;
    }
    sf::Context context;
    if (!context.setActive(true)) {
        fmt::println("Skipped: no OpenGL context");
        return 77;
    }
    std::optional<HdrRenderer> renderer;
    sf::RenderTexture window;
    try {
        renderer.emplace(SIZE);
    }
    catch (const std::runtime_error& e) {
        fmt::println("Skipped: {}", e.what());
        return 77;
    }
    if (!window.resize(SIZE)) {
        fmt::println("Skipped: no render texture");
        return 77;
    }
    fmt::println("OpenGL {}.{}: {}", context.getSettings().majorVersion,
            context.getSettings().minorVersion,
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    test_float_targets(*renderer, window);
    test_deposits(*renderer, window);
    test_trails(*renderer, window);
    test_segments(*renderer, window);
    test_color_modes(*renderer, window);
    test_bloom(*renderer, window);
    test_exposure(*renderer, window);
    test_tonemap(*renderer, window);
    return Check::result();
}
