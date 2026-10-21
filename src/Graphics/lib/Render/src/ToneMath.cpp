#include "Render/ToneMath.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "Constants/Constants.hpp"
#include "Render/Colors.hpp"


namespace ToneMath {
float level(float intensity, float white, float beta) {
    return std::asinh(intensity / beta) / std::asinh(std::max(white, 1e-6f) / beta);
}

float trail_level(const TrailPixel& pixel, float fade_time, float white, float beta) {
    if (!(pixel.age < fade_time)) {
        return 0.f;
    }
    const float shown =
            pixel.selected ? 1.f : std::min(level(Colors::luminance(pixel.light), white, beta), 1.f);
    return shown * (1.f - pixel.age / fade_time);
}

TrailPixel trail_step(const TrailPixel& pixel, sf::Vector3f light, float selection_coverage,
        float frame_time, float fade_time, float white, float beta) {
    using namespace Constants::Graphics;
    TrailPixel aged = pixel;
    aged.age += frame_time;
    const float old_level = trail_level(aged, fade_time, white, beta);
    const float intensity = Colors::luminance(light);
    const bool selected = selection_coverage * SELECT_COVERAGE_GAIN >= 1.f;
    const float body = intensity >= TRAIL_TAKEOVER_MIN ? std::max(intensity, 1.f) : intensity;
    const float new_level = selected ? 1.f : std::min(level(body, white, beta), 1.f);
    if (intensity > 0.f && new_level >= old_level) {
        return {light, 0.f, selected};
    }
    return old_level > 0.f ? aged : TrailPixel{};
}

std::optional<float> white_from_histogram(std::span<const float> histogram, float quantile,
        float min_white, float log2_min, float bins_per_octave) {
    float total = 0.f;
    for (const float count : histogram) {
        total += count;
    }
    if (total == 0.f) {
        return std::nullopt;
    }
    const float target = (1.f - quantile) * total;
    float above = 0.f;
    float white = min_white;
    for (size_t i = histogram.size(); i-- > 0;) {
        const float count = histogram[i];
        if (count > 0.f && above + count > target) {
            const float f = (target - above) / count;
            white = std::exp2(log2_min + (static_cast<float>(i) + 1.f - f) / bins_per_octave);
            break;
        }
        above += count;
    }
    return std::max(white, min_white);
}

float quantile(std::vector<float>& values, double q) {
    assert(!values.empty());
    const auto rank = static_cast<size_t>(std::lround(q * static_cast<double>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + rank, values.end());
    return values[rank];
}

double smoothed(double current, double target, double dt, double tau) {
    const double adapt = 1.0 - std::exp(-dt / tau);
    return std::exp(std::log(current) + adapt * (std::log(target) - std::log(current)));
}
}  // namespace ToneMath
