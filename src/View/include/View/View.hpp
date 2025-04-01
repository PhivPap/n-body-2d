#pragma once

#include <vector>

#include "Config/Config.hpp"
#include "Body/Body.hpp"

class View {
public:
    View(const Config &cfg, const std::vector<Body> &bodies);
};
