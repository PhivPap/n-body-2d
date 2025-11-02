#pragma once

#include <string>
#include <filesystem>

#include "SFML/System/Vector2.hpp"

namespace fs = std::filesystem;

class Config {
public:
    struct IO {
        fs::path universe_infile;
        fs::path universe_outfile;
        bool echo_bodies;

        bool validate();
        std::string to_string() const;
    } io;

    struct Simulation {
        double timestep;
        uint64_t iterations;
        enum class Algorithm {BARNES_HUT, NAIVE} algorithm;
        uint16_t threads;
        float stats_update_hz;

        bool validate() const;
        std::string to_string() const;
    } sim;

    struct Graphics {
        bool enabled;
        sf::Vector2<uint16_t> resolution;
        bool vsync_enabled;
        uint16_t fps;
        double pixel_scale;
        bool grid_enabled;

        bool validate() const;
        std::string to_string() const;
    } graphics;

    Config() = delete;
    Config(const fs::path& path);
    void print();
private:
    bool validate();
    static Simulation::Algorithm string_to_enum(const std::string& str_alg);
    static std::string enum_to_string(Simulation::Algorithm alg);
};
