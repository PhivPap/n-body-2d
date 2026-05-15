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
};

class ConfigPanel : public PanelBase<ConfigPanel, ConfigDisplayedData> {
public:
    using Base = PanelBase<ConfigPanel, ConfigDisplayedData>;
    ConfigPanel(sf::Vector2u size);
    void bake_impl();
};
