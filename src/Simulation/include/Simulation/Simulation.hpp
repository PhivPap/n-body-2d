#pragma once

#include <vector>

#include "Config/Config.hpp"
#include "Body/Body.hpp"

class Simulation {
public:
    Simulation(const Config &cfg, std::vector<Body> &bodies);
};