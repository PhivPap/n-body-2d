#pragma once

#include "Config/Config.hpp"
#include "Graphics/Graphics.hpp"
#include "Simulation/Simulation.hpp"

class Controller {
private:
    Config &cfg;
    Simulation &sim;
    Graphics &graphics;

    void handle_events(sf::RenderWindow &window);

public:
    Controller(Config &cfg, Simulation &sim, Graphics &graphics);
    void run();
};
