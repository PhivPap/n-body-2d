#pragma once

#include <string>

#include "fmt/base.h"

namespace Log {
    class Time {
    public:
        enum class Unit : uint8_t {
            NS, US, MS, S, M, H, DAYS, YEARS
        };

        template <Unit unit = Unit::S> static Time from(double units);
        std::string to_string() const;
    private:
        Time(double sec);
        template <Unit unit> static constexpr double seconds_to(double units);
        template <Unit unit> static constexpr double seconds_from(double units);

        const double sec;
    };
}

template <>
struct fmt::formatter<Log::Time> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(const Log::Time& my_type, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", my_type.to_string());
    }
};

template <Log::Time::Unit unit>
Log::Time Log::Time::from(double units) {
    return Log::Time(seconds_from<unit>(units));
}

inline Log::Time::Time(double sec) : sec(sec) {}

inline std::string Log::Time::to_string() const {
    double _sec = sec;
    int sign = 1;
    if (_sec < 0) {
        sign = -1;
        _sec = -_sec;
    }

    if (_sec < seconds_from<Unit::US>(1))
        return fmt::format("{:.3f}ns", sign * seconds_to<Unit::NS>(_sec));
    else if (_sec < seconds_from<Unit::MS>(1))
        return fmt::format("{:.3f}us", sign * seconds_to<Unit::US>(_sec));
    else if (_sec < seconds_from<Unit::S>(1))
        return fmt::format("{:.3f}ms", sign * seconds_to<Unit::MS>(_sec));
    else if (_sec < seconds_from<Unit::M>(1))
        return fmt::format("{:.3f}s", sign * seconds_to<Unit::S>(_sec));
    else if (_sec < seconds_from<Unit::H>(1))
        return fmt::format("{:.3f}m", sign * seconds_to<Unit::M>(_sec));
    else if (_sec < seconds_from<Unit::DAYS>(1))
        return fmt::format("{:.3f}h", sign * seconds_to<Unit::H>(_sec));
    else if (_sec < seconds_from<Unit::YEARS>(1))
        return fmt::format("{:.3f}days", sign * seconds_to<Unit::DAYS>(_sec));
    else if (_sec < seconds_from<Unit::YEARS>(1e+5))
        return fmt::format("{:.3f}years", sign * seconds_to<Unit::YEARS>(_sec));
    return fmt::format("{:.3g}years", sign * seconds_to<Unit::YEARS>(_sec));
}

template <Log::Time::Unit unit>
constexpr double Log::Time::seconds_to(double units) {
    if constexpr (unit == Unit::NS)
        return units * 1e+9;
    else if constexpr (unit == Unit::US)
        return units * 1e+6;
    else if constexpr (unit == Unit::MS)
        return units * 1e+3;
    else if constexpr (unit == Unit::S)
        return units * 1;
    else if constexpr (unit == Unit::M)
        return units / 60;
    else if constexpr (unit == Unit::H)
        return units / (60 * 60);
    else if constexpr (unit == Unit::DAYS)
        return units / (60 * 60 * 24);
    else {
        static_assert(unit == Unit::YEARS);
        return units / (60 * 60 * 24 * 365.2425);
    }
}

template <Log::Time::Unit unit>
constexpr double Log::Time::seconds_from(double units) {
    return units / seconds_to<unit>(1.0);
}
