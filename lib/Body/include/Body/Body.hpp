#pragma once

#include <string>

#include "SFML/System/Vector2.hpp"

struct Body {
    std::string id;
    double mass;
    sf::Vector2<double> pos;
    sf::Vector2<double> vel;
};
