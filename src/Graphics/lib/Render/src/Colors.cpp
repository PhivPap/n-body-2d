#include "Render/Colors.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>


namespace {
// Where `t` falls between evenly spaced stops: the stop before it and how far towards the next
std::pair<size_t, float> ramp_position(size_t stops, float t) {
    assert(stops >= 2);
    const float x = std::clamp(t, 0.f, 1.f) * static_cast<float>(stops - 1);
    const size_t k = std::min(static_cast<size_t>(x), stops - 2);
    return {k, x - static_cast<float>(k)};
}
}  // namespace


namespace Colors {
float to_linear(float srgb) {
    return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

float to_srgb(float linear) {
    const float c = std::clamp(linear, 0.f, 1.f);
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

sf::Vector3f to_linear(sf::Color color) {
    return {to_linear(color.r / 255.f), to_linear(color.g / 255.f), to_linear(color.b / 255.f)};
}

sf::Color to_srgb(sf::Vector3f linear) {
    const auto encode = [](float c) {
        return static_cast<uint8_t>(std::lround(255.f * to_srgb(c)));
    };
    return {encode(linear.x), encode(linear.y), encode(linear.z)};
}

float luminance(sf::Vector3f linear) {
    return 0.2126f * linear.x + 0.7152f * linear.y + 0.0722f * linear.z;
}

sf::Vector3f unit_luminance(sf::Vector3f linear) {
    const float y = luminance(linear);
    return y > 0.f ? linear / y : linear;
}

sf::Color display_hue(sf::Vector3f linear) {
    const float max = std::max({linear.x, linear.y, linear.z});
    return to_srgb(max > 0.f ? linear / max : linear);
}

sf::Vector3f ramp_linear(std::span<const sf::Color> stops, float t) {
    const auto [k, f] = ramp_position(stops.size(), t);
    return to_linear(stops[k]) + f * (to_linear(stops[k + 1]) - to_linear(stops[k]));
}

sf::Color ramp_display(std::span<const sf::Color> stops, float t) {
    const auto [k, f] = ramp_position(stops.size(), t);
    const auto mix = [f](uint8_t a, uint8_t b) {
        return static_cast<uint8_t>(std::lround(a + f * (b - a)));
    };
    return {mix(stops[k].r, stops[k + 1].r), mix(stops[k].g, stops[k + 1].g),
            mix(stops[k].b, stops[k + 1].b)};
}
}  // namespace Colors
