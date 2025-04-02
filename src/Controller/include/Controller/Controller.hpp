#pragma once

#include "Config/Config.hpp"
#include "Graphics/Graphics.hpp"
#include "Simulation/Simulation.hpp"

class Controller {
public:
    Controller(const Config &cfg, Simulation &sim, Graphics &graphics);
    void run();
};
