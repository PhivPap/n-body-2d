#pragma once

#include "Panel.hpp"


struct ConfigDisplayedData {
    double timestep_s;
    std::string algorithm;
    double theta;
    bool show_theta;
    double softening_factor;
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
    bool selection_center_on_center_of_mass;
};

class ConfigPanel : public PanelBase<ConfigPanel, ConfigDisplayedData> {
public:
    using Base = PanelBase<ConfigPanel, ConfigDisplayedData>;
    ConfigPanel(uint32_t width);
    void set_panel_text();
};
