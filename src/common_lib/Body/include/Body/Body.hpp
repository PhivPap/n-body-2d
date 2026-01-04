#pragma once

#include <string>
#include <vector>
#include <inttypes.h>

#include "SFML/System/Vector2.hpp"


class Bodies {
public:
    const uint64_t n;

    Bodies() = delete;
    Bodies(std::vector<std::string> &&id, std::vector<double> &&mass, 
            std::vector<sf::Vector2<double>> &&pos, 
            std::vector<sf::Vector2<double>> &&vel);
    bool validate() const;
    std::string &id(uint64_t index);
    const std::string &id(uint64_t index) const;
    double &mass(uint64_t index);
    const double &mass(uint64_t index) const;
    sf::Vector2<double> &pos(uint64_t index);
    const sf::Vector2<double> &pos(uint64_t index) const;
    sf::Vector2<double> &vel(uint64_t index);
    const sf::Vector2<double> &vel(uint64_t index) const;
    double *mass_data();
    const double *mass_data() const;
    sf::Vector2<double> *pos_data();
    const sf::Vector2<double> *pos_data() const;
    sf::Vector2<double> *vel_data();
    const sf::Vector2<double> *vel_data() const;
private:
    std::vector<std::string> id_;
    std::vector<double> mass_;
    std::vector<sf::Vector2<double>> pos_;
    std::vector<sf::Vector2<double>> vel_;

    bool validate_ids() const;
};
