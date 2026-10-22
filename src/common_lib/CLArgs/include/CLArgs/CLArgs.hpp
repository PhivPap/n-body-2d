#pragma once

#include <filesystem>
#include <optional>
#include <string>


namespace fs = std::filesystem;

struct CLArgs {
    std::optional<fs::path> config;     // none: the scenario hub opens
    std::optional<fs::path> scenarios;  // the hub's folder
    CLArgs() = delete;
    CLArgs(int argc, const char* argv[]);
};
