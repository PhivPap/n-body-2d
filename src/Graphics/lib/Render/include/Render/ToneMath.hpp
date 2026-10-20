#pragma once

#include <optional>
#include <span>
#include <vector>

#include "SFML/System/Vector3.hpp"


// The renderer's tone curve, trail fade and auto exposure as its shaders compute them (see
// HdrRenderer.cpp), on the CPU. Light is linear RGB; its luminance counts bodies per pixel.
namespace ToneMath {
// The display level of luminance `intensity`: asinh(I / beta) / asinh(white / beta), which is 1
// at the white point and goes on above it
float level(float intensity, float white, float beta);
// A trail pixel after one frame of fading: its display level lowered by `fade`, its hue kept, and
// black once the level reaches 0. Unchanged without a fade (paused).
sf::Vector3f faded(sf::Vector3f color, float white, float beta, float fade);
// The auto white point from a histogram of block maxima in log2 bins, bin i starting at
// 2^(log2_min + i / bins_per_octave): the value with (1 - quantile) of the blocks above it,
// interpolated in its bin in log2, at least `min_white`. Nullopt if the histogram is empty.
std::optional<float> white_from_histogram(std::span<const float> histogram, float quantile,
        float min_white, float log2_min, float bins_per_octave);
// The q-quantile of `values` (reordered, not empty), the value at rank round(q (n - 1))
float quantile(std::vector<float>& values, double q);
// `current` moved towards `target` in log space, as an exponential over `dt` of time constant
// `tau` (both positive)
double smoothed(double current, double target, double dt, double tau);
}  // namespace ToneMath
