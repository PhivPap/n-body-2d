#pragma once

#include <atomic>
#include <filesystem>
#include <optional>

#include "Config/Config.hpp"
#include "Graphics/Graphics.hpp"
#include "Hub/IntroCard.hpp"
#include "Hub/ScenarioHub.hpp"
#include "RLCaller/RLCaller.hpp"
#include "Simulation/Simulation.hpp"
#include "Ui/Ui.hpp"


class Controller {
public:
    static std::atomic<bool> sigint_flag;  // set by the SIGINT handler
    
    // `config_path`: the scenario's, which the hub shows as running
    Controller(const Config& cfg, Simulation& sim, Graphics& graphics, Ui::Context& ui,
            Hub::ScenarioHub& hub, const std::filesystem::path& config_path);
    // Runs the simulation in the window until it finishes, the window closes, Ctrl+C or another
    // scenario is picked in the hub (Esc), and returns the config to run next, if any
    std::optional<std::filesystem::path> run();
    // Runs the simulation without a window until it finishes or Ctrl+C
    static void run_headless(Simulation& sim);
    // Runs the open hub alone in the window until a scenario is picked, whose config it returns,
    // or the window closes or Ctrl+C
    static std::optional<std::filesystem::path> run_hub(sf::RenderWindow& window,
            Ui::Context& ui, Hub::ScenarioHub& hub);
    // One frame that says that the scenario of `config_path` is loading
    static void show_loading(sf::RenderWindow& window, Ui::Context& ui,
            const std::filesystem::path& config_path);

private:
    Simulation& sim;
    Graphics& graphics;
    Ui::Context& ui;
    Hub::ScenarioHub& hub;
    const std::filesystem::path& config_path;
    Hub::IntroCard intro_card;
    RLCaller stats_update_rate_limiter;

    void handle_events(sf::RenderWindow& window);
    void update_panels();
    void timestep_increase();
    void timestep_decrease();
};
