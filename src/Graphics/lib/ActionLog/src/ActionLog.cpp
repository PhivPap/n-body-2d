#include "ActionLog/ActionLog.hpp"
#include "Logger/Logger.hpp"

const StopWatch SW;

void ActionLog::log(std::string_view text) {
    action_log.emplace_back(fmt::format("[{}] {}", SW, text), StopWatch());
    if (action_log.size() > MAX_ENTRIES) {
        action_log.pop_front();
    }
}

std::string ActionLog::to_string() const {
    std::string result;
    for (const auto& action : action_log) {
        if (action.timer.duration<std::chrono::seconds>() < entry_duration) {
            result += action.text;
        }
        result += "\n";
    }
    return result;
}
