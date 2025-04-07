#include <thread>
#include <signal.h>
#include <utility>
#include <unistd.h>

#include "SFML/Window.hpp"
#include "SFML/Graphics.hpp"

#include "Config/Config.hpp"
#include "InputOutput/InputOutput.hpp"
#include "Logger/Logger.hpp"
#include "Constants/Constants.hpp"
#include "Body/Body.hpp"
#include "CLArgs/CLArgs.hpp"
#include "StopWatch/StopWatch.hpp"
#include "ViewPort/ViewPort.hpp"
#include "Exit/Exit.hpp"

#include "Simulation/Simulation.hpp"
#include "Graphics/Graphics.hpp"
#include "Controller/Controller.hpp"

#define IGNORED_UNUSED_VALUE(expr) if (expr) {}

// Signal handler can only use signal-safe code
void sigint_handler(int signum) {
    assert(signum == SIGINT);
    constexpr std::string_view txt1 = "\nInterrupted (SIGINT)\n";
    IGNORED_UNUSED_VALUE(write(STDERR_FILENO, txt1.data(), txt1.size()));
    Controller::sigint_flag = true;
}

std::unique_ptr<Simulation> create_sim(const Config &cfg, std::vector<Body> &bodies) {
    if (cfg.algorithm == Config::Algorithm::NAIVE) {
        return std::make_unique<NaiveSim>(cfg, bodies);
    }
    else if (cfg.algorithm == Config::Algorithm::BARNES_HUT) {
        return std::make_unique<BarnesHutSim>(cfg, bodies);
    }
    assert(false);
    return nullptr;
}

int main(int argc, const char *argv[]) {
    try {
        const CLArgs clargs(argc, argv);
        Config cfg(clargs.config);
        std::vector<Body> bodies = IO::parse_csv(cfg.universe_infile.string(), cfg.echo_bodies);
    
        std::unique_ptr<Simulation> sim = create_sim(cfg, bodies);
        Graphics graphics(cfg, bodies);
        Controller controller(cfg, *sim.get(), graphics);
    
        signal(SIGINT, sigint_handler);
        
        controller.run();

        IO::write_csv(cfg.universe_outfile.string(), bodies);
    }
    catch (const std::exception &e) {
        Log::error("{}", e.what());
        return 1;
    }
    return 0;
}
