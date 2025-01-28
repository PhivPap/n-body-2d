#pragma once

#include <vector>

#include "body.hpp"

namespace IO {
    std::vector<Body> parse_csv(std::string_view path);
    void write_csv(std::string_view path, const std::vector<Body> &bodies);
}
