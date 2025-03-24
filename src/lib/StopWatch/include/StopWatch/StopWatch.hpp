#pragma once

#include <chrono>
#include <cmath>
#include <string>
#include <fmt/core.h>
#include <fmt/format.h>

class StopWatch {
public:
    enum class State { PAUSED, RUNNING };
    StopWatch(State state = State::RUNNING);
    StopWatch operator+(const StopWatch& sw) const;
    StopWatch operator-(const StopWatch& sw) const;
    double operator/(const StopWatch& sw) const;
    StopWatch operator*(double d) const;
    StopWatch operator/(double d) const;

    template <typename TargetDuration = std::chrono::seconds, uint8_t DECIMALS = 3>
    double elapsed() const;
    template <typename TargetDuration>
    TargetDuration duration() const;
    void resume();
    void pause();
    void reset(State state = State::RUNNING);
    State current_state() const;
    std::string to_string() const;

private:
    using SecondsF64 = std::chrono::duration<double>;
    State state;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_start;
    SecondsF64 previously_elapsed{0};

    template <uint8_t DECIMALS = 3>
    static double round(double d) {
        constexpr double ROUNDING_FACTOR = std::pow(10.0, DECIMALS);
        return std::round(ROUNDING_FACTOR * d) / ROUNDING_FACTOR;
    }
    template <typename TargetDuration, uint8_t DECIMALS, typename Duration>
    static double cast_to_f64(Duration duration) {
        using TargetDurationF64 = 
            std::chrono::duration<double, typename TargetDuration::period>;
        return round<DECIMALS>(std::chrono::duration_cast<TargetDurationF64>(duration).count());
    }
    template <typename TargetDuration, typename Duration>
    static int64_t cast_to_i64(Duration duration) {
        using TargetDurationI64 = 
            std::chrono::duration<int64_t, typename TargetDuration::period>;
        return std::chrono::duration_cast<TargetDurationI64>(duration).count();
    }
};

template <typename TargetDuration, uint8_t DECIMALS>
double StopWatch::elapsed() const {
    using TargetDurationF64 = 
            std::chrono::duration<double, typename TargetDuration::period>;
    const auto full_duration = duration<TargetDurationF64>();
    return cast_to_f64<TargetDurationF64, DECIMALS>(full_duration);
}

template <typename TargetDuration>
TargetDuration StopWatch::duration() const {
    return state == State::RUNNING ?
            std::chrono::duration_cast<TargetDuration>(previously_elapsed +
                    (std::chrono::high_resolution_clock::now() - last_start)) :
            std::chrono::duration_cast<TargetDuration>(previously_elapsed);
}

template <>
struct fmt::formatter<StopWatch> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(const StopWatch& my_type, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", my_type.to_string());
    }
};
