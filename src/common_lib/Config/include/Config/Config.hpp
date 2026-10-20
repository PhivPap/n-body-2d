#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include "SFML/Graphics/Color.hpp"
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
        enum class SimType : uint8_t { ALL_PAIRS, BARNES_HUT, BARNES_HUT_GPU } simtype{};
        double theta;
        // Plummer softening, one of: a fraction of the mean distance between bodies, a length
        std::optional<double> softening_factor;
        std::optional<double> softening_length_m;
        uint16_t threads;

        bool parse_simtype();
        static std::string_view simtype_to_string(SimType simtype);
        std::string to_string() const;
        bool validate();
    } sim;

    struct Graphics {
        // How a group of bodies is drawn: an exact body id's or an id group's
        struct GroupStyle {
            std::string color_str;  // "#rrggbb"
            sf::Color color{};      // sRGB, parsed from color_str by validate()
            float brightness;       // 0: drawn as hidden
        };

        bool enabled;
        sf::Vector2<uint16_t> resolution;
        bool vsync_enabled;
        uint16_t fps;
        double pixel_scale;
        bool show_grid;
        float panel_update_hz;
        bool show_commands_panel;
        bool show_config_panel;
        bool show_stats_panel;
        bool show_action_log_panel;
        bool show_inspector_panel;
        bool show_legend_panel;
        bool selection_show;
        bool selection_show_center_of_mass;
        bool follow_selected;
        bool selection_show_orbit;
        bool named_bodies_show_labels;
        bool named_bodies_scale_by_mass;
        std::chrono::duration<double> trails_fade;
        std::string color_mode_str;
        // In the order V cycles through them, which the renderer's shaders also use
        enum class ColorMode : uint8_t { WHITE, GROUPS, SPEED, DOPPLER, DENSITY } color_mode{};
        double exposure_ev;
        bool bloom;
        std::map<std::string, GroupStyle> group_styles;  // by body id or id group

        bool parse_color_mode();
        static std::string_view color_mode_to_string(ColorMode color_mode);
        bool validate();
        std::string to_string() const;
    } graphics;

    Config() = delete;
    Config(const fs::path& path);
    void print();

private:
    bool validate();
};
