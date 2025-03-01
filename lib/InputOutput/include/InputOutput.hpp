#pragma once

#include <filesystem>
#include <vector>

#include "Body.hpp"

namespace IO {
    namespace fs = std::filesystem; 
    std::vector<Body> parse_csv(const fs::path& path);
    void write_csv(const fs::path& path, const std::vector<Body> &bodies);
}
