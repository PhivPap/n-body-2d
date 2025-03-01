#pragma once

#include <string>
#include <filesystem>

#include "SFML/System/Vector2.hpp"

namespace fs = std::filesystem;

class Config {
public:
    fs::path universe_infile;
    fs::path universe_outfile;

    double timestep;
    uint64_t iterations;
    
    sf::Vector2<uint32_t> resolution;
    uint32_t fps;
    double pixel_resolution;

    Config() = delete;
    Config(const fs::path& path);
    void print();
private:
    void validate();
};
