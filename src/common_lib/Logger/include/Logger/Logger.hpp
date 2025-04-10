#pragma once

#include <string>

#include "fmt/base.h"
#include "fmt/color.h"

namespace Log {
    enum class Verbosity {
        ERROR, WARNING, INFO, DEBUG
    };

    inline Verbosity verbosity = Verbosity::DEBUG;

    template <typename ...T>
    inline void error(fmt::format_string<T...> fmt_str, T &&...args) {
        fmt::print(fg(fmt::color::crimson), "[Error] ");
        fmt::print(fg(fmt::color::crimson), fmt_str, std::forward<T>(args)...);
        fmt::print(fg(fmt::color::crimson), "\n");
    }

    template <typename ...T>
    inline void warning(fmt::format_string<T...> fmt_str, T &&...args) {
        if (verbosity >= Verbosity::WARNING) {
            fmt::print(fg(fmt::color::orange), "[Warning] ");
            fmt::print(fg(fmt::color::orange), fmt_str, std::forward<T>(args)...);
            fmt::print(fg(fmt::color::orange), "\n");
        }
    }

    template <typename ...T>
    inline void info(fmt::format_string<T...> fmt_str, T &&...args) {
        if (verbosity >= Verbosity::INFO) {
            fmt::print("[Info] ");
            fmt::print(fmt_str, std::forward<T>(args)...);
            fmt::print("\n");
        }
    }   

    template <typename ...T>
    inline void debug(fmt::format_string<T...> fmt_str, T &&...args) {
        if (verbosity >= Verbosity::DEBUG) {
            fmt::print(fg(fmt::color::dark_cyan), "[Debug] ");
            fmt::print(fg(fmt::color::dark_cyan), fmt_str, std::forward<T>(args)...);
            fmt::print(fg(fmt::color::dark_cyan), "\n");
        }
    }
}