#include "Body/Body.hpp"

#include <unordered_set>

#include "Logger/Logger.hpp"

Bodies::Bodies(std::vector<std::string> &&id, std::vector<double> &&mass, 
        std::vector<sf::Vector2<double>> &&pos, std::vector<sf::Vector2<double>> &&vel) : 
                n(id.size()), id_(std::move(id)), mass_(std::move(mass)), 
                pos_(std::move(pos)), vel_(std::move(vel)) {
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
        Log::error("Body id uniqueness constrain violated");
    }

    // ...

    return ok;
}

std::string &Bodies::id(uint64_t index) {
    assert(index <= n);
    return id_[index];
}

const std::string &Bodies::id(uint64_t index) const {
    assert(index <= n);
    return id_[index];
}

double &Bodies::mass(uint64_t index) {
    assert(index <= n);
    return mass_[index];
}

const double &Bodies::mass(uint64_t index) const {
    assert(index <= n);
    return mass_[index];
}

sf::Vector2<double> &Bodies::pos(uint64_t index) {
    assert(index <= n);
    return pos_[index];
}

const sf::Vector2<double> &Bodies::pos(uint64_t index) const {
    assert(index <= n);
    return pos_[index];
}

sf::Vector2<double> &Bodies::vel(uint64_t index) {
    assert(index <= n);
    return vel_[index];
}

const sf::Vector2<double> &Bodies::vel(uint64_t index) const {
    assert(index <= n);
    return vel_[index];
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
