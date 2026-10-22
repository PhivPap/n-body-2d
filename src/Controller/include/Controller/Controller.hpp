#pragma once

#include <atomic>

#include "Config/Config.hpp"
#include "Graphics/Graphics.hpp"
#include "RLCaller/RLCaller.hpp"
#include "Simulation/Simulation.hpp"
#include "Ui/Ui.hpp"


class Controller {
public:
    static std::atomic<bool> sigint_flag;  // set by the SIGINT handler
    
    Controller(const Config& cfg, Simulation& sim, Graphics& graphics, Ui::Context& ui);
    void run();
    // Runs the simulation without a window until it finishes or Ctrl+C
    static void run_headless(Simulation& sim);

private:
    Simulation& sim;
    Graphics& graphics;
    Ui::Context& ui;
    RLCaller stats_update_rate_limiter;

    void handle_events(sf::RenderWindow& window);
    void update_panels();
    void timestep_increase();
    void timestep_decrease();
};
