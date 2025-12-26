#pragma once

#include <string>

#include "fmt/base.h"

namespace Log {
    class Distance {
    public:
        enum class Unit : uint8_t {
            NM, UM, MM, M, KM, AU, LY
        };

        template <Unit unit = Unit::M> static Distance from(double units);
        std::string to_string() const;
    private:
        Distance(double meters);
        template <Unit unit> static constexpr double meters_to(double units);
        template <Unit unit> static constexpr double meters_from(double units);

        const double meters;
    };
}

template <>
struct fmt::formatter<Log::Distance> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(const Log::Distance& my_type, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", my_type.to_string());
    }
};

template <Log::Distance::Unit unit>
Log::Distance Log::Distance::from(double units) {
    return Log::Distance(meters_from<unit>(units));
}

inline Log::Distance::Distance(double meters) : meters(meters) {}

inline std::string Log::Distance::to_string() const {
    double _meters = meters;
    int sign = 1;
    if (_meters < 0) {
        sign = -1;
        _meters = -_meters;
    }

    if (_meters < meters_from<Unit::UM>(1)) 
        return fmt::format("{:.3f}nm", sign * meters_to<Unit::NM>(_meters));
    else if (_meters < meters_from<Unit::MM>(1)) 
        return fmt::format("{:.3f}um", sign * meters_to<Unit::UM>(_meters));
    else if (_meters < meters_from<Unit::M>(1)) 
        return fmt::format("{:.3f}mm", sign * meters_to<Unit::MM>(_meters));
    else if (_meters < meters_from<Unit::KM>(1)) 
        return fmt::format("{:.3f}m", sign * meters_to<Unit::M>(_meters));
    else if (_meters < meters_from<Unit::AU>(1)) 
        return fmt::format("{:.3f}km", sign * meters_to<Unit::KM>(_meters));
    else if (_meters < meters_from<Unit::LY>(1))
        return fmt::format("{:.3f}AU", sign * meters_to<Unit::AU>(_meters));
    else if (_meters < meters_from<Unit::LY>(1e+5))
        return fmt::format("{:.3f}LY", sign * meters_to<Unit::LY>(_meters));
    return fmt::format("{:.3g}LY", sign * meters_to<Unit::LY>(_meters));
}

template <Log::Distance::Unit unit>
constexpr double Log::Distance::meters_to(double units) {
    if constexpr (unit == Unit::NM)
        return units * 1e+9;
    else if constexpr (unit == Unit::UM)
        return units * 1e+6;
    else if constexpr (unit == Unit::MM)
        return units * 1e+3;
    else if constexpr (unit == Unit::M)
        return units * 1;
    else if constexpr (unit == Unit::KM)
        return units / 1e+3;
    else if constexpr (unit == Unit::AU)
        return units / 149'597'870'700;
    else {
        static_assert(unit == Unit::LY);
        return units / 9'460'730'472'580'800;
    }
}

template <Log::Distance::Unit unit>
constexpr double Log::Distance::meters_from(double units) {
    return units / meters_to<unit>(1.0);
}