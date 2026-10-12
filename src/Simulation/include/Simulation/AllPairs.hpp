#pragma once

#include "Simulation/Simulation.hpp"


class AllPairsSim : public Simulation {
public:
    AllPairsSim(const Config::Simulation& sim_cfg, Bodies& bodies);
    ~AllPairsSim() override;

private:
    void step() override;
    void update_positions();
    void update_velocities();
};
