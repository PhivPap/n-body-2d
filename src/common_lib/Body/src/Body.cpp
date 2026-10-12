#include "Body/Body.hpp"

#include <cassert>
#include <cmath>
#include <unordered_set>

#include "Logger/Logger.hpp"

Bodies::Bodies(std::vector<std::string>&& id, std::vector<double>&& mass,
        std::vector<sf::Vector2<double>>&& pos, std::vector<sf::Vector2<double>>&& vel)
        : n(id.size()), id_(std::move(id)), mass_(std::move(mass)), pos_(std::move(pos)),
          vel_(std::move(vel)) {
    assert(id_.size() == n);
    assert(mass_.size() == n);
    assert(pos_.size() == n);
    assert(vel_.size() == n);
    id_.shrink_to_fit();
    mass_.shrink_to_fit();
    pos_.shrink_to_fit();
    vel_.shrink_to_fit();
}

bool Bodies::validate() const {
    bool ok = true;

    if (!validate_ids()) {
        ok = false;
        Log::error("Body id uniqueness constraint violated");
    }

    // Logs the first few invalid bodies (this file isn't built with -ffast-math, which would
    // assume that every value is finite)
    constexpr uint64_t MAX_LOGGED = 10;
    uint64_t invalid = 0;
    for (uint64_t i = 0; i < n; i++) {
        const bool finite = std::isfinite(mass(i)) && std::isfinite(pos(i).x)
                            && std::isfinite(pos(i).y) && std::isfinite(vel(i).x)
                            && std::isfinite(vel(i).y);
        if (!finite || mass(i) < 0) {
            if (invalid++ < MAX_LOGGED) {
                Log::error("Body #{} `{}` has a negative mass or a value that isn't finite", i,
                        id(i));
            }
        }
    }
    if (invalid > MAX_LOGGED) {
        Log::error("... and {} more invalid bodies", invalid - MAX_LOGGED);
    }
    if (invalid > 0) {
        ok = false;
    }

    return ok;
}

bool Bodies::validate_ids() const {
    std::unordered_set<std::string_view> unique_body_ids;
    unique_body_ids.reserve(n);
    uint64_t index = 0;
    for (uint64_t i = 0; i < n; i++) {
        if (unique_body_ids.count(id(i)) != 0) {
            Log::warning("Body #{} duplicate ID `{}`", index, id(i));
        }
        else {
            unique_body_ids.emplace(id(i));
        }
        index++;
    }
    return unique_body_ids.size() == n;
}
