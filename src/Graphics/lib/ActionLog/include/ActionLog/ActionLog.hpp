#pragma once

#include <queue>
#include <chrono>

#include "StopWatch/StopWatch.hpp"

class ActionLog {
public:
    void log(std::string_view text);
    std::string to_string() const;

private:
    static constexpr uint8_t MAX_ENTRIES = 5;
    static constexpr std::chrono::seconds entry_duration{10}; 

    struct Action {
        std::string text;
        StopWatch timer;
    };

    std::deque<Action> action_log;
};
