#pragma once

#include "Panel.hpp"


struct StatsDisplayedData {
    uint64_t iteration;
    double iter_per_sec;
    uint64_t frame;
    float fps;
    double elapsed_s;
    double simulated_time_s;
    double simulation_rate;
};

class StatsPanel : public PanelBase<StatsPanel, StatsDisplayedData> {
public:
    using Base = PanelBase<StatsPanel, StatsDisplayedData>;
    StatsPanel(sf::Vector2u size);
    void bake_impl();
};
