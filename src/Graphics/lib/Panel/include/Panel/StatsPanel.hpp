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

class StatsPanel : public DataPanel<StatsDisplayedData> {
public:
    StatsPanel(float width);

private:
    void draw_contents() override;
};
