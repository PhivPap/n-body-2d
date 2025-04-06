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
    static volatile bool sigint_flag;
    Controller(Config &cfg, Simulation &sim, Graphics &graphics);
    void run();
};
