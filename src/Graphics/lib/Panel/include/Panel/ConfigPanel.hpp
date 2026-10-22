#pragma once

#include <optional>
#include <string>

#include "Panel.hpp"


struct ConfigDisplayedData {
    double timestep_s;
    std::string algorithm;
    double theta;
    bool show_theta;
    std::optional<double> softening_factor;
    std::optional<double> softening_length_m;
    uint32_t threads;
    bool show_threads;
    sf::Vector2<double> viewport_m;
    sf::Vector2<uint32_t> viewport_px;
    bool vsync;
    bool grid;
    uint32_t max_fps;
    bool selection_show;
    bool selection_show_center_of_mass;
    bool selection_follow_center_of_mass;
    bool selection_show_orbit;
    bool show_labels;
    bool show_named_orbits;
    bool scale_named_by_mass;
};

class ConfigPanel : public DataPanel<ConfigDisplayedData> {
public:
    ConfigPanel(float width);

private:
    void draw_contents() override;
};
