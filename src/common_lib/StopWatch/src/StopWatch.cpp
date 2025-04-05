#include "StopWatch/StopWatch.hpp"

#include "Logger/Logger.hpp"
#include "fmt/format.h"
#include "fmt/chrono.h"

StopWatch::StopWatch(State state): state(state) {
    if (state == State::RUNNING) {
        last_start = std::chrono::high_resolution_clock::now();
    }
}

StopWatch StopWatch::operator+(const StopWatch& sw) const {
    StopWatch res(State::PAUSED);
    res.previously_elapsed = 
        this->duration<SecondsF64>() + sw.duration<SecondsF64>();
    return res;
}

StopWatch StopWatch::operator-(const StopWatch& sw) const {
    StopWatch res(State::PAUSED);
    res.previously_elapsed = 
        this->duration<SecondsF64>() - sw.duration<SecondsF64>();
    return res;
}

double StopWatch::operator/(const StopWatch& sw) const {
    return this->duration<SecondsF64>() / sw.duration<SecondsF64>();
}

StopWatch StopWatch::operator*(double d) const {
    StopWatch res(State::PAUSED);
    res.previously_elapsed = 
        this->duration<SecondsF64>() * d;
    return res;
}

StopWatch StopWatch::operator/(double d) const {
    StopWatch res(State::PAUSED);
    res.previously_elapsed = 
        this->duration<SecondsF64>() / d;
    return res;
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
    using namespace std::chrono_literals;

    auto full_duration = duration<SecondsF64>();
    char sign = 0;
    if (full_duration < 0s) {
        sign = '-';
        full_duration = -full_duration;
    }

    if (full_duration < 1ms) {
        const int64_t us = cast_to_i64<std::chrono::microseconds>(full_duration);
        const int64_t ns = cast_to_i64<std::chrono::nanoseconds>(full_duration) % 1000;
        return fmt::format("{}{}.{:03}us", sign, us, ns);
    }
    else if (full_duration < 1s) {
        const int64_t ms = cast_to_i64<std::chrono::milliseconds>(full_duration);
        const int64_t us = cast_to_i64<std::chrono::microseconds>(full_duration) % 1000;
        return fmt::format("{}{}.{:03}ms", sign, ms, us);
    }
    else if (full_duration < 1min) {
        const int64_t s = cast_to_i64<std::chrono::seconds>(full_duration);
        const int64_t ms = cast_to_i64<std::chrono::milliseconds>(full_duration) % 1000;
        return fmt::format("{}{}.{:03}s", sign, s, ms);
    }
    else if (full_duration < 1h) {
        const int64_t m = cast_to_i64<std::chrono::minutes>(full_duration);
        const int64_t s = cast_to_i64<std::chrono::seconds>(full_duration) % 60;
        const int64_t ms = cast_to_i64<std::chrono::milliseconds>(full_duration) % 1000;
        return fmt::format("{}{:02}m{:02}.{:03}s", sign, m, s, ms);
    }
    else if (full_duration < 24h) {
        const int64_t h = cast_to_i64<std::chrono::hours>(full_duration);
        const int64_t m = cast_to_i64<std::chrono::minutes>(full_duration) % 60;
        const int64_t s = cast_to_i64<std::chrono::seconds>(full_duration) % 60;
        return fmt::format("{}{:02}h{:02}m{:02}s", sign, h, m, s);
    }
    else {
        const int64_t d = cast_to_i64<std::chrono::hours>(full_duration) / 24;
        const int64_t h = cast_to_i64<std::chrono::hours>(full_duration) % 24;
        const int64_t m = cast_to_i64<std::chrono::minutes>(full_duration) % 60;
        return fmt::format("{}{}d{:02}h{:02}m", sign, d, h, m);
    }
}
