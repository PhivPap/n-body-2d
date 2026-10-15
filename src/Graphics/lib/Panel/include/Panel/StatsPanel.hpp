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

    bool operator==(const StatsDisplayedData&) const = default;
};

class StatsPanel : public PanelBase<StatsPanel, StatsDisplayedData> {
public:
    using Base = PanelBase<StatsPanel, StatsDisplayedData>;
    StatsPanel(uint32_t width);
    void set_panel_text();
};
