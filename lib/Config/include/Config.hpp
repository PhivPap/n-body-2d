#pragma once

#include <string>

#include "SFML/System/Vector2.hpp"

class Config {
public:
    std::string universe_infile;
    std::string universe_outfile;

    double timestep;
    uint64_t iterations;
    
    sf::Vector2<uint32_t> resolution;
    uint32_t target_fps;
    double pixel_resolution;

    Config() = delete;
    Config(const std::string& path);
    void print();
private:
    void validate();
};
