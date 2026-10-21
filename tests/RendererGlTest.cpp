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
            .locked_white = std::nullopt, .bloom = false};
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

void test_trails(HdrRenderer& renderer, sf::RenderTexture& window) {
    const float white = 40.f, beta = Constants::Graphics::TONE_SOFTENING;
    renderer.set_groups(std::vector{group(sf::Color(0xff, 0xa0, 0x40))});
    HdrRenderer::Frame trails = frame();
    trails.trails_fade_s = 2.0;
    trails.locked_white = white;
    // Pixels of 40 (at white), 20, 5 and 1 bodies, and one that gets new light next frame
    std::vector<Body> first;
    for (const auto [x, count] :
            {std::pair{10u, 40u}, {12u, 20u}, {14u, 5u}, {16u, 1u}, {18u, 3u}}) {
        const auto bodies = stack(x, 10, count);
        first.insert(first.end(), bodies.begin(), bodies.end());
    }
    renderer.reset_trails();
    renderer.draw(window, vertices(first), trails);
    std::vector<sf::Vector3f> before;
    for (uint32_t x = 10; x <= 18; x += 2) {
        before.push_back(pixel(renderer, x, 10));
    }
    // A fifth of the fade time: levels drop by 0.2, the new light wins where it is brighter
    trails.frame_time_s = 0.4;
    renderer.draw(window, vertices(stack(18, 10, 30)), trails);
    bool matches = true;
    for (uint32_t k = 0; k < 4; k++) {
        const sf::Vector3f expected = ToneMath::faded(before[k], white, beta, 0.2f);
        matches &= near(pixel(renderer, 10 + 2 * k, 10), expected, 1e-4f);
    }
    CHECK(matches);
    CHECK(near(pixel(renderer, 18, 10), 10.f * before[4], 1e-5f));
    // Paused: frozen, exactly
    std::vector<sf::Vector3f> frozen;
    for (uint32_t x = 10; x <= 18; x += 2) {
        frozen.push_back(pixel(renderer, x, 10));
    }
    trails.paused = true;
    for (int k = 0; k < 3; k++) {
        renderer.draw(window, vertices({}), trails);
    }
    bool kept = true;
    for (uint32_t k = 0; k < frozen.size(); k++) {
        kept &= pixel(renderer, 10 + 2 * k, 10) == frozen[k];
    }
    CHECK(kept);
    // From white, black after the fade time; a reset clears everything
    trails.paused = false;
    trails.frame_time_s = 2.0;
    renderer.draw(window, vertices(stack(30, 10, 40)), trails);
    renderer.draw(window, vertices({}), trails);
    CHECK(pixel(renderer, 30, 10) == sf::Vector3f() && pixel(renderer, 10, 10) == sf::Vector3f());
    renderer.draw(window, vertices(stack(30, 10, 40)), trails);
    renderer.reset_trails();
    trails.frame_time_s = 0.01;
    renderer.draw(window, vertices({}), trails);
    CHECK(pixel(renderer, 30, 10) == sf::Vector3f());
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
    test_exposure(*renderer, window);
    test_tonemap(*renderer, window);
    return Check::result();
}
