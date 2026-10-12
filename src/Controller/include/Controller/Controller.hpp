#pragma once

#include <csignal>

#include "Config/Config.hpp"
#include "Graphics/Graphics.hpp"
#include "RLCaller/RLCaller.hpp"
#include "Simulation/Simulation.hpp"


class Controller {
public:
    static volatile std::sig_atomic_t sigint_flag;
    
    Controller(const Config& cfg, Simulation& sim, Graphics& graphics);
    void run();
    // Runs the simulation without a window until it finishes or Ctrl+C
    static void run_headless(Simulation& sim);

private:
    Simulation& sim;
    Graphics& graphics;
    RLCaller stats_update_rate_limiter;

    void handle_events(sf::RenderWindow& window);
    void update_panels();
    void timestep_increase();
    void timestep_decrease();
};
