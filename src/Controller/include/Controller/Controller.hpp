#pragma once

#include "Config/Config.hpp"
#include "Graphics/Graphics.hpp"
#include "RLCaller/RLCaller.hpp"
#include "Simulation/Simulation.hpp"


class Controller {
public:
    static volatile bool sigint_flag;
    
    Controller(Config& cfg, Simulation& sim, Graphics& graphics);
    void run();

private:
    Config& cfg;
    Simulation& sim;
    Graphics& graphics;
    RLCaller stats_update_rate_limiter;

    void handle_events(sf::RenderWindow& window);
    void init_panels();
    void update_panels();
    void timestep_increase();
    void timestep_decrease();
};
