#pragma once

#include "Simulation/Simulation.hpp"

class AllPairsSim : public Simulation {
public:
    AllPairsSim(const Config::Simulation &sim_cfg, Bodies &bodies);
    ~AllPairsSim() override;
private:
    std::thread sim_thread;

    void on_run() override;
    void on_pause() override;
    void simulate();
    void update_positions();
    void update_velocities();
};
