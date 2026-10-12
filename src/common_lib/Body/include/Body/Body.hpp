#pragma once

#include <cassert>
#include <inttypes.h>
#include <string>
#include <vector>

#include "SFML/System/Vector2.hpp"


class Bodies {
public:
    const uint64_t n;

    Bodies() = delete;
    Bodies(std::vector<std::string>&& id, std::vector<double>&& mass,
            std::vector<sf::Vector2<double>>&& pos, std::vector<sf::Vector2<double>>&& vel);
    bool validate() const;
    std::string& id(uint64_t index);
    const std::string& id(uint64_t index) const;
    double& mass(uint64_t index);
    const double& mass(uint64_t index) const;
    sf::Vector2<double>& pos(uint64_t index);
    const sf::Vector2<double>& pos(uint64_t index) const;
    sf::Vector2<double>& vel(uint64_t index);
    const sf::Vector2<double>& vel(uint64_t index) const;
    double* mass_data();
    const double* mass_data() const;
    sf::Vector2<double>* pos_data();
    const sf::Vector2<double>* pos_data() const;
    sf::Vector2<double>* vel_data();
    const sf::Vector2<double>* vel_data() const;

private:
    std::vector<std::string> id_;
    std::vector<double> mass_;
    std::vector<sf::Vector2<double>> pos_;
    std::vector<sf::Vector2<double>> vel_;

    bool validate_ids() const;
};

inline std::string& Bodies::id(uint64_t index) {
    assert(index < n);
    return id_[index];
}

inline const std::string& Bodies::id(uint64_t index) const {
    assert(index < n);
    return id_[index];
}

inline double& Bodies::mass(uint64_t index) {
    assert(index < n);
    return mass_[index];
}

inline const double& Bodies::mass(uint64_t index) const {
    assert(index < n);
    return mass_[index];
}

inline sf::Vector2<double>& Bodies::pos(uint64_t index) {
    assert(index < n);
    return pos_[index];
}

inline const sf::Vector2<double>& Bodies::pos(uint64_t index) const {
    assert(index < n);
    return pos_[index];
}

inline sf::Vector2<double>& Bodies::vel(uint64_t index) {
    assert(index < n);
    return vel_[index];
}

inline const sf::Vector2<double>& Bodies::vel(uint64_t index) const {
    assert(index < n);
    return vel_[index];
}

inline double* Bodies::mass_data() {
    return mass_.data();
}

inline const double* Bodies::mass_data() const {
    return mass_.data();
}

inline sf::Vector2<double>* Bodies::pos_data() {
    return pos_.data();
}

inline const sf::Vector2<double>* Bodies::pos_data() const {
    return pos_.data();
}

inline sf::Vector2<double>* Bodies::vel_data() {
    return vel_.data();
}

inline const sf::Vector2<double>* Bodies::vel_data() const {
    return vel_.data();
}
