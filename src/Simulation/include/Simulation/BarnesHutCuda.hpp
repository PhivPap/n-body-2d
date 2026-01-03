#pragma once

#include "Simulation/Simulation.hpp"

class BarnesHutCuda : public Simulation {
public:
    BarnesHutCuda(const Config::Simulation &sim_cfg, Bodies &bodies);
    virtual ~BarnesHutCuda();  
private:
    void on_run() override;
    void on_pause() override;
    void simulate();
};
