#pragma once

#include <fmt/base.h>
#include <fmt/color.h>
#include <string>

namespace Log {
    enum class Verbosity {
        ERROR, WARNING, INFO, DEBUG
    };

    inline Verbosity verbosity = Verbosity::DEBUG;

    template <typename ...T>
    void error(const std::string& fmt_str, T&&... args) {
        fmt::print(fg(fmt::color::crimson), "[Error] " + fmt_str + '\n', args...);
    }

    template <typename ...T>
    void warning(const std::string& fmt_str, T&&... args) {
        if (verbosity >= Verbosity::WARNING)
            fmt::print(fg(fmt::color::orange), "[Warning] " + fmt_str + '\n', args...);
    }

    template <typename ...T>
    void info(const std::string& fmt_str, T&&... args) {
        if (verbosity >= Verbosity::INFO)
            fmt::print("[Info] " + fmt_str + '\n', args...);
    }   

    template <typename ...T>
    void debug(const std::string& fmt_str, T&&... args) {
        if (verbosity >= Verbosity::DEBUG)
            fmt::print(fg(fmt::color::dark_cyan), "[Debug] " + fmt_str + '\n', args...);
    }
}