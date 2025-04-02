#pragma once

#include <vector>

#include "Config/Config.hpp"
#include "Body/Body.hpp"

class Graphics {
public:
    Graphics(const Config &cfg, const std::vector<Body> &bodies);
};
