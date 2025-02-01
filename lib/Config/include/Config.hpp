#pragma once

#include <string>

class Config {
public:
    std::string universe_infile;
    std::string universe_outfile;
    double timestep;
    double min_timestep;
    double max_timestep;
    uint64_t iterations;
    uint64_t max_fps;

    Config() = delete;
    Config(const std::string& path, bool print=true);
};
