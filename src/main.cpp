#include <filesystem>
#include <memory>
#include <optional>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <utility>

#include "Body/Body.hpp"
#include "CLArgs/CLArgs.hpp"
#include "Config/Config.hpp"
#include "Controller/Controller.hpp"
#include "Constants/Constants.hpp"
#include "Graphics/Graphics.hpp"
#include "Hub/ScenarioHub.hpp"
#include "InputOutput/InputOutput.hpp"
#include "Logger/Logger.hpp"
#include "Simulation/AllPairs.hpp"
#include "Simulation/BarnesHut.hpp"
#ifdef NBODY_CUDA
#include "Simulation/BarnesHutCuda.hpp"
#endif
#include "Simulation/Simulation.hpp"
#include "Text/Text.hpp"
#include "Ui/Ui.hpp"

// Signal handler can only use signal-safe code
void sigint_handler([[maybe_unused]] int signum) {
    assert(signum == SIGINT);
    constexpr std::string_view txt1 = "\nInterrupted (SIGINT)\n";
    std::ignore = write(STDERR_FILENO, txt1.data(), txt1.size());
    Controller::sigint_flag = true;
}

std::unique_ptr<Simulation> create_sim(const Config::Simulation& sim_cfg, Bodies& bodies) {
    if (sim_cfg.simtype == Config::Simulation::SimType::ALL_PAIRS) {
        return std::make_unique<AllPairsSim>(sim_cfg, bodies);
    }
    else if (sim_cfg.simtype == Config::Simulation::SimType::BARNES_HUT) {
        return std::make_unique<BarnesHut>(sim_cfg, bodies);
    }
#ifdef NBODY_CUDA
    else if (sim_cfg.simtype == Config::Simulation::SimType::BARNES_HUT_GPU) {
        return std::make_unique<BarnesHutCuda>(sim_cfg, bodies);
    }
#endif
    assert(false);
    return nullptr;
}

// A scenario's run: its config, its bodies, their simulation and, with a window, their graphics.
// Destroyed in reverse: the simulation and the graphics keep references to the config and bodies.
struct Session {
    const Config cfg;
    Bodies bodies;
    std::unique_ptr<Simulation> sim;
    std::optional<Graphics> graphics;

    explicit Session(const fs::path& config_path)
            : cfg(config_path),
              bodies(IO::parse_csv(cfg.io.universe_infile.string(), cfg.io.echo_bodies)),
              sim(create_sim(cfg.sim, bodies)) {}
};

// "N-Body Sim - solar-system": the separator is ASCII, since X11's WM_NAME is Latin-1
sf::String window_title(const fs::path& config_path) {
    return sf::String("N-Body Sim - ") + Text::from_utf8(config_path.stem().string());
}

int main(int argc, const char* argv[]) {
    try {
        const CLArgs clargs(argc, argv);
        // The hub's folder: --scenarios, else the config's, else the repository's scenarios/
        Hub::ScenarioHub hub(clargs.scenarios ? *clargs.scenarios
                             : clargs.config  ? clargs.config->parent_path()
                                              : Hub::repository_scenarios());
        // The window, created for the first session that has one, at its resolution, or for the
        // hub, and Dear ImGui, which closes it, outlive the sessions drawn on them
        std::optional<sf::RenderWindow> window;
        std::optional<Ui::Context> ui;
        std::optional<fs::path> config_path = clargs.config;
        if (!config_path) {
            signal(SIGINT, sigint_handler);  // which ends the hub too
            window.emplace(sf::VideoMode(Constants::Ui::HUB_RESOLUTION), "N-Body Sim");
            ui.emplace(*window);
            hub.open(std::nullopt);
            config_path = Controller::run_hub(*window, *ui, hub);
        }
        // An error in the command line's config ends the program, before any window opens. A
        // config picked in the hub runs in the window, and if it fails to load, the hub says why.
        bool from_command_line = clargs.config.has_value();
        while (config_path) {
            std::optional<Session> session;
            try {
                session.emplace(*config_path);
                if (window || session->cfg.graphics.enabled) {
                    if (!window) {
                        window.emplace(
                                sf::VideoMode(sf::Vector2u(session->cfg.graphics.resolution)),
                                window_title(*config_path));
                        ui.emplace(*window);
                    }
                    else {
                        window->setTitle(window_title(*config_path));
                    }
                    session->graphics.emplace(session->cfg, session->bodies, *window);
                }
            }
            catch (const std::exception& e) {
                if (from_command_line || !window) {
                    throw;
                }
                Log::error("{}", e.what());
                hub.set_load_error(*config_path, e.what());
                window->setTitle("N-Body Sim");
                hub.open(*config_path);
                config_path = Controller::run_hub(*window, *ui, hub);
                continue;
            }
            hub.set_load_error(*config_path, std::nullopt);
            from_command_line = false;

            signal(SIGINT, sigint_handler);

            const fs::path running = *config_path;
            config_path = std::nullopt;
            if (session->graphics) {
                config_path = Controller(session->cfg, *session->sim, *session->graphics, *ui,
                        hub, running).run();
                if (config_path) {
                    Controller::show_loading(*window, *ui, *config_path);
                }
            }
            else {
                Controller::run_headless(*session->sim);
            }

            // Also after a simulation error: `bodies` holds the last state it reached (whose
            // velocities may then lag its positions by half a step)
            IO::write_csv(session->cfg.io.universe_outfile.string(), session->bodies);
            session->sim->rethrow_error();
        }
    }
    catch (const std::exception& e) {
        Log::error("{}", e.what());
        return 1;
    }
    return 0;
}
