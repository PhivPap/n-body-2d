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
        std::string simtype_str;
        enum class SimType : uint8_t {BARNES_HUT, NAIVE} simtype;
        uint16_t threads;

        bool parse_simtype();
        static std::string_view simtype_to_string(SimType simtype);
        std::string to_string() const;
        bool validate();
    } sim;

    struct Graphics {
        bool enabled;
        sf::Vector2<uint16_t> resolution;
        bool vsync_enabled;
        uint16_t fps;
        double pixel_scale;
        bool grid_enabled;
        float panel_update_hz;
        bool show_panel;

        bool validate() const;
        std::string to_string() const;
    } graphics;

    Config() = delete;
    Config(const fs::path& path);
    void print();
private:
    bool validate();
};
