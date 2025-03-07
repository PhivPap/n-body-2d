#pragma once

#include <string>
#include <filesystem>

#include "SFML/System/Vector2.hpp"

namespace fs = std::filesystem;

class Config {
public:
    fs::path universe_infile;
    fs::path universe_outfile;
    bool echo_bodies;

    double timestep;
    uint64_t iterations;
    enum class Algorithm {BARNES_HUT, BRUTE_FORCE};
    Algorithm algorithm;

    bool graphics_enabled;
    sf::Vector2<uint32_t> resolution;
    uint32_t fps;
    double pixel_resolution;

    Config() = delete;
    Config(const fs::path& path);
    void print();
private:
    bool validate();
    Algorithm string_to_enum(std::string);
    std::string enum_to_string(Algorithm algorithm);
};
