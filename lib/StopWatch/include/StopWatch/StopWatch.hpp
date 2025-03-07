#pragma once

#include <chrono>
#include <cmath>
#include <string>
#include <fmt/core.h>
#include <fmt/format.h>

class StopWatch {
public:
    enum class State { PAUSED, RUNNING };
private:
    State state;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_start;
    std::chrono::duration<double> previously_elapsed{0};

    template <uint8_t DECIMALS = 3>
    static double round(double d) {
        constexpr double ROUNDING_FACTOR = std::pow(10.0, DECIMALS);
        return std::round(ROUNDING_FACTOR * d) / ROUNDING_FACTOR;
    }
public:
    StopWatch(State state = State::RUNNING);
    template <typename Duration = std::chrono::seconds, uint8_t DECIMALS = 3>
    double elapsed() {
        using TargetDurationType = 
                std::chrono::duration<double, typename Duration::period>;
        constexpr double ROUNDING_FACTOR = std::pow(10.0, DECIMALS);

        const TargetDurationType full_duration = state == State::RUNNING ?
                std::chrono::duration_cast<TargetDurationType>(previously_elapsed +
                        (std::chrono::high_resolution_clock::now() - last_start)) :
                std::chrono::duration_cast<TargetDurationType>(previously_elapsed);
        return round<DECIMALS>(full_duration.count());
    }
    void resume();
    void pause();
    void reset(State state = State::RUNNING);
    State current_state() const;
    std::string to_string() const;
};

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
