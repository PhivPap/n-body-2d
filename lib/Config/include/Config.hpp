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
    
    uint16_t window_width;
    uint16_t window_height;
    uint16_t max_fps;
    double pixel_resolution;

    Config() = delete;
    Config(const std::string& path);
    void print();
};
