#pragma once

#include <vector>

#include "Body.hpp"

namespace IO {
    std::vector<Body> parse_csv(const std::string& path);
    void write_csv(const std::string& path, const std::vector<Body> &bodies);
}
