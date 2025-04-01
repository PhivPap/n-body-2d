#pragma once

#include <vector>

#include "Config/Config.hpp"
#include "Body/Body.hpp"

class Model {
public:
    Model(const Config &cfg, std::vector<Body> &bodies);
};