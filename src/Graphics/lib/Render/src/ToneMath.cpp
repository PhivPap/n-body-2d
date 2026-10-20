#include "Render/ToneMath.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "Render/Colors.hpp"


namespace ToneMath {
float level(float intensity, float white, float beta) {
    return std::asinh(intensity / beta) / std::asinh(std::max(white, 1e-6f) / beta);
}

sf::Vector3f faded(sf::Vector3f color, float white, float beta, float fade) {
    const float intensity = Colors::luminance(color);
    if (!(intensity > 0.f && fade > 0.f)) {
        return color;
    }
    const float a = std::asinh(std::max(white, 1e-6f) / beta);
    const float new_level = std::asinh(intensity / beta) / a - fade;
    return new_level > 0.f ? color * (beta * std::sinh(new_level * a) / intensity)
                           : sf::Vector3f();
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
