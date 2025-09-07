#pragma once

#include <chrono>
#include <thread>
#include <functional>

#include "Logger/Logger.hpp"

// use std::bind() if you want to call a member function
class RLCaller {
private:
    std::chrono::microseconds min_interval;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_call;
public:
    RLCaller() = delete;    
    template <typename _Rep, typename _Period>
    RLCaller(const std::chrono::duration<_Rep, _Period> &min_interval) : 
            min_interval(std::chrono::duration_cast<std::chrono::microseconds>(min_interval)) {}

    // For non void functions
    template <typename F, typename... ArgPack,
            typename Ret = decltype(std::declval<F>()(std::declval<ArgPack>()...)),
            std::enable_if_t<!std::is_void_v<Ret>, bool> = true>
    auto block_call(F &&func, ArgPack &&...args) -> decltype(func(std::forward<ArgPack>(args)...)) {
        auto now = std::chrono::high_resolution_clock::now();
        const auto elapsed = now - last_call;
        if (elapsed < min_interval) {
            std::this_thread::sleep_for(min_interval - elapsed);
            now = std::chrono::high_resolution_clock::now();
        }
        last_call = now;
        return func(args...);
    }

    // For void functions
    template <typename F, typename... ArgPack,
            typename Ret = decltype(std::declval<F>()(std::declval<ArgPack>()...)),
            std::enable_if_t<std::is_void_v<Ret>, bool> = true>
    void block_call(F&& func, ArgPack&&... args) {
        auto now = std::chrono::high_resolution_clock::now();
        const auto elapsed = now - last_call;
        if (elapsed < min_interval) {
            std::this_thread::sleep_for(min_interval - elapsed);
            now = std::chrono::high_resolution_clock::now();
        }
        last_call = now;
        func(args...);
    }

    // For non void functions
    template <typename F, typename... ArgPack,
            typename Ret = decltype(std::declval<F>()(std::declval<ArgPack>()...)),
            std::enable_if_t<!std::is_void_v<Ret>, bool> = true>
    std::optional<Ret> try_call(F&& func, ArgPack&&... args) {
        const auto now = std::chrono::high_resolution_clock::now();
        if (now - last_call >= min_interval) {
            last_call = now;
            return func(args...);
        }
        return std::nullopt;
    }

    // For void functions
    template <typename F, typename... ArgPack,
            typename Ret = decltype(std::declval<F>()(std::declval<ArgPack>()...)),
            std::enable_if_t<std::is_void_v<Ret>, bool> = true>
    bool try_call(F&& func, ArgPack&&... args) {
        const auto now = std::chrono::high_resolution_clock::now();
        if (now - last_call >= min_interval) {
            last_call = now;
            func(args...);
            return true;
        }
        return false;
    }
};

