#pragma once

#include "Config/Config.hpp"
#include "Graphics/Graphics.hpp"
#include "RLCaller/RLCaller.hpp"
#include "Simulation/Simulation.hpp"


class Controller {
public:
    static volatile bool sigint_flag;
    
    Controller(const Config& cfg, Simulation& sim, Graphics& graphics);
    void run();

private:
    Simulation& sim;
    Graphics& graphics;
    double timestep;
    RLCaller stats_update_rate_limiter;

    void handle_events(sf::RenderWindow& window);
    void init_panels(const Config& cfg);
    void update_panels();
    void timestep_increase();
    void timestep_decrease();
};
