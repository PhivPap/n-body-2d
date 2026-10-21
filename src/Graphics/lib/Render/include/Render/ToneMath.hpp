#pragma once

#include <optional>
#include <span>
#include <vector>

#include "SFML/System/Vector3.hpp"


// The renderer's tone curve, trails and auto exposure as its shaders compute them (see
// HdrRenderer.cpp), on the CPU. Light is linear RGB; its luminance counts bodies per pixel.
namespace ToneMath {
// The display level of luminance `intensity`: asinh(I / beta) / asinh(white / beta), which is 1
// at the white point and goes on above it
float level(float intensity, float white, float beta);

// A trail pixel: the light it was given last, that light's age (seconds), and whether it showed
// the selection
struct TrailPixel {
    sf::Vector3f light;
    float age = 0.f;
    bool selected = false;

    bool operator==(const TrailPixel&) const = default;
};
// Its display level: its light's, up to 1 (the selection's: 1), times 1 - age / fade_time, and 0
// from the fade time on
float trail_level(const TrailPixel& pixel, float fade_time, float white, float beta);
// The pixel after a frame of `frame_time` (0 paused) that gave it `light`, `selection_coverage`
// of it covered by the selection: the light takes the pixel if it is shown at least as bright, or
// if it holds TRAIL_TAKEOVER_MIN of a body's light (luminance) of another colour (hue off by more
// than TRAIL_HUE_TOLERANCE) and that body would be, laid over another colour as far as it covers
// the pixel (a body's light covers it); else the pixel ages, and is black once its level reaches 0
TrailPixel trail_step(const TrailPixel& pixel, sf::Vector3f light, float selection_coverage,
        float frame_time, float fade_time, float white, float beta);
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
