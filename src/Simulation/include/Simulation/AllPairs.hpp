#pragma once

#include "Simulation/Simulation.hpp"


class AllPairsSim : public Simulation {
public:
    AllPairsSim(const Config::Simulation& sim_cfg, Bodies& bodies);
    ~AllPairsSim() override;

private:
    void step(double kick, double drift) override;
    void update_positions(double drift);
    void update_velocities(double kick);
};
