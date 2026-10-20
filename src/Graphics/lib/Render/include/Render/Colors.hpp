#pragma once

#include <span>

#include "SFML/Graphics/Color.hpp"
#include "SFML/System/Vector3.hpp"


// Colours as configured and shown (sRGB, 8-bit) and as light adds up (linear RGB, in an
// sf::Vector3f). Luminance has Rec. 709 weights, as in the renderer's shaders.
namespace Colors {
float to_linear(float srgb);  // an encoded channel in [0, 1]
float to_srgb(float linear);  // clamped to [0, 1]
sf::Vector3f to_linear(sf::Color color);
sf::Color to_srgb(sf::Vector3f linear);  // rounded to 8 bits
float luminance(sf::Vector3f linear);
// Scaled to a luminance of 1, so that its hue doesn't change how much light it adds; black as is
sf::Vector3f unit_luminance(sf::Vector3f linear);
// The hue at full brightness, as markers and swatches show it: scaled to a maximum channel of 1,
// then encoded, which brightens dark colours the way the tone curve does; black as is
sf::Color display_hue(sf::Vector3f linear);
// The colour at `t` (clamped to [0, 1]) along evenly spaced stops: interpolated in linear RGB (the
// speed and Doppler ramps), or in sRGB values (the density colormap)
sf::Vector3f ramp_linear(std::span<const sf::Color> stops, float t);
sf::Color ramp_display(std::span<const sf::Color> stops, float t);
}  // namespace Colors
