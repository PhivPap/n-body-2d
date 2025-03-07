#include "StopWatch.hpp"

#include "Logger.hpp"
#include "fmt/format.h"
#include "fmt/chrono.h"

StopWatch::StopWatch(State state): state(state) {
    if (state == State::RUNNING) {
        last_start = std::chrono::high_resolution_clock::now();
    }
}

void StopWatch::resume() {
    if (state == State::RUNNING) {
        Log::warning("StopWatch::resume() called on running StopWatch");
        return;
    }
    last_start = std::chrono::high_resolution_clock::now();
    state = State::RUNNING;
}

void StopWatch::pause() {
    if (state == State::PAUSED) {
        Log::warning("StopWatch::pause() called on paused StopWatch");   
        return;
    }
    previously_elapsed += std::chrono::duration_cast<decltype(previously_elapsed)>
            (std::chrono::high_resolution_clock::now() - last_start);
    state = State::PAUSED;
}

void StopWatch::reset(State state) {
    this->state = state;
    previously_elapsed.zero();
    if (state == State::RUNNING) {
        last_start = std::chrono::high_resolution_clock::now();
    }
}

StopWatch::State StopWatch::current_state() const {
    return state;
}

std::string StopWatch::to_string() const {
    const auto total_elapsed = state == State::RUNNING ?
            previously_elapsed + std::chrono::duration_cast<decltype(previously_elapsed)>
                (std::chrono::high_resolution_clock::now() - last_start) :
            previously_elapsed;

    using namespace std::chrono_literals;
    if (total_elapsed < 1ms) {
        return fmt::format("{}us", round(std::chrono::duration_cast<
                std::chrono::duration<double, std::chrono::microseconds::period>
            >(total_elapsed).count()));
    }
    else if (total_elapsed < 1s) {
        return fmt::format("{}ms", round(std::chrono::duration_cast<
                std::chrono::duration<double, std::chrono::milliseconds::period>
            >(total_elapsed).count()));
    }
    else if (total_elapsed < 1min) {
        return fmt::format("{}s", round(total_elapsed.count()));
    }
    else if (total_elapsed < 1h) {
        return fmt::format("{:%Mm}{}s", total_elapsed, round(total_elapsed.count()));
    }
    else if (total_elapsed < 24h) {
        return fmt::format("{:%Hh%Mm}{}s", total_elapsed, round<0>(total_elapsed.count()));
    }
    return fmt::format("{:%jd%Hh%Mm}", total_elapsed, round(total_elapsed.count()));
}
