#pragma once

#include <string>

struct CLArgs {
    std::string config_path;
    CLArgs() = delete;
    CLArgs(int argc, const char *argv[]);
};
