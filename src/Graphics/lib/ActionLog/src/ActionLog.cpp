#include "ActionLog/ActionLog.hpp"
#include "Logger/Logger.hpp"

const StopWatch SW;

void ActionLog::log(std::string_view text) {
    action_log.emplace_back(SW.to_string(), std::string(text), StopWatch());
    if (action_log.size() > MAX_ENTRIES) {
        action_log.pop_front();
    }
}

std::vector<ActionLog::Entry> ActionLog::entries() const {
    std::vector<Entry> entries;
    for (const auto& action : action_log) {
        const auto age = action.timer.duration<std::chrono::duration<double>>();
        if (age < ENTRY_DURATION) {
            entries.push_back({action.time, action.text, age.count()});
        }
    }
    return entries;
}
