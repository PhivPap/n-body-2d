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
        fmt::print(fg(fmt::color::crimson), "[Error] {}\n", fmt::format(fmt_str, std::forward<T>(args)...));
    }

    template <typename ...T>
    inline void warning(fmt::format_string<T...> fmt_str, T &&...args) {
        if (verbosity >= Verbosity::WARNING) {
            fmt::print(fg(fmt::color::orange), "[Warning] {}\n", fmt::format(fmt_str, std::forward<T>(args)...));
        }
    }

    template <typename ...T>
    inline void info(fmt::format_string<T...> fmt_str, T &&...args) {
        if (verbosity >= Verbosity::INFO) {
            fmt::print("[Info] {}\n", fmt::format(fmt_str, std::forward<T>(args)...));
        }
    }   

    template <typename ...T>
    inline void debug(fmt::format_string<T...> fmt_str, T &&...args) {
        if (verbosity >= Verbosity::DEBUG) {
            fmt::print(fg(fmt::color::dark_cyan), "[Debug] {}\n", fmt::format(fmt_str, std::forward<T>(args)...));
        }
    }
}   