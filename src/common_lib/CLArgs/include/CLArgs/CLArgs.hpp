#pragma once

#include <filesystem>
#include <string>


namespace fs = std::filesystem;

struct CLArgs {
    fs::path config;
    CLArgs() = delete;
    CLArgs(int argc, const char* argv[]);
};
