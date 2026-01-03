#pragma once

#include <filesystem>
#include <vector>

#include "Body/Body.hpp"

namespace IO {
    namespace fs = std::filesystem; 
    Bodies parse_csv(const fs::path& path, bool echo_bodies);
    void write_csv(const fs::path& path, const Bodies &bodies);
}
