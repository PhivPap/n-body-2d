#pragma once

#include "Panel.hpp"


struct StatsDisplayedData {
    struct Selection {
        uint32_t num_selected;
        double total_mass;
        sf::Vector2<double> center_of_mass;
        sf::Vector2<double> weighted_velocity;
    };

    uint64_t iteration;
    double iter_per_sec;
    uint64_t frame;
    float fps;
    double elapsed_s;
    double simulated_time_s;
    double simulation_rate;
    std::optional<Selection> opt_selection;
};

class StatsPanel : public PanelBase<StatsPanel, StatsDisplayedData> {
public:
    using Base = PanelBase<StatsPanel, StatsDisplayedData>;
    StatsPanel(uint32_t width);
    void set_panel_text();
};
