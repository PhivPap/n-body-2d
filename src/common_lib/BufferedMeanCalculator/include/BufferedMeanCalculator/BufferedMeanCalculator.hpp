#pragma once

#include <array>
#include <type_traits>
#include <numeric>
#include <cstdint>


template <typename T, uint32_t Len>
class BufferedMeanCalculator {
    static_assert(Len > 1, "Don't use for less than 2 elements");
    static_assert(Len <= 10'000, "Buffer too large");
    static_assert(std::is_arithmetic_v<T>, "T must be a numeric type");
public:
    BufferedMeanCalculator() = default;
    void register_value(T value);
    template <typename M  = double>
    M get_mean() const;

private:
    std::array<T, Len> buffer{};
    uint64_t idx = 0;
};

template <typename T, uint32_t Len>
void BufferedMeanCalculator<T, Len>::register_value(T value) {
    idx %= Len;
    buffer[idx++] = value;
}

template <typename T, uint32_t Len>
template <typename M>
M BufferedMeanCalculator<T, Len>::get_mean() const {
    static_assert(std::is_arithmetic_v<M>, "M must be a numeric type");
    return static_cast<M>(std::accumulate(buffer.begin(), buffer.end(), static_cast<T>(0))) / 
            static_cast<M>(Len);
}
