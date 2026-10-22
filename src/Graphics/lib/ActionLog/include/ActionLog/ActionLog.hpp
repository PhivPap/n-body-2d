#pragma once

#include <chrono>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "StopWatch/StopWatch.hpp"

class ActionLog {
public:
    // A message, shown for ENTRY_DURATION
    struct Entry {
        std::string time;  // since the program started
        std::string text;
        double age_s;
    };
    static constexpr std::chrono::seconds ENTRY_DURATION{10};

    void log(std::string_view text);
    std::vector<Entry> entries() const;  // the live ones, oldest first

private:
    static constexpr uint8_t MAX_ENTRIES = 5;

    struct Action {
        std::string time;
        std::string text;
        StopWatch timer;
    };

    std::deque<Action> action_log;
};
