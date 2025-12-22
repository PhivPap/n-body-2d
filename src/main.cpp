#include <thread>
#include <signal.h>
#include <utility>
#include <unistd.h>

#include "Config/Config.hpp"
#include "InputOutput/InputOutput.hpp"
#include "Logger/Logger.hpp"
#include "Body/Body.hpp"
#include "CLArgs/CLArgs.hpp"
#include "Simulation/Simulation.hpp"
#include "Graphics/Graphics.hpp"
#include "Controller/Controller.hpp"

// Signal handler can only use signal-safe code
void sigint_handler(int signum) {
    assert(signum == SIGINT);
    constexpr std::string_view txt1 = "\nInterrupted (SIGINT)\n";
    std::ignore = write(STDERR_FILENO, txt1.data(), txt1.size());
    Controller::sigint_flag = true;
}

std::unique_ptr<Simulation> create_sim(const Config::Simulation &sim_cfg, 
        std::vector<Body> &bodies) {
    if (sim_cfg.simtype == Config::Simulation::SimType::NAIVE) {
        return std::make_unique<AllPairsSim>(sim_cfg, bodies);
    }
    else if (sim_cfg.simtype == Config::Simulation::SimType::BARNES_HUT) {
        return std::make_unique<BarnesHutSim>(sim_cfg, bodies);
    }
    assert(false);
    return nullptr;
}

int main(int argc, const char *argv[]) {
    try {
        const CLArgs clargs(argc, argv);
        Config cfg(clargs.config);
        std::vector<Body> bodies = IO::parse_csv(cfg.io.universe_infile.string(), 
                cfg.io.echo_bodies);
    
        std::unique_ptr<Simulation> sim = create_sim(cfg.sim, bodies);
        Graphics graphics(cfg.graphics, bodies);
        Controller controller(cfg, *sim.get(), graphics);
    
        signal(SIGINT, sigint_handler);
        
        controller.run();

        IO::write_csv(cfg.io.universe_outfile.string(), bodies);
    }
    catch (const std::exception &e) {
        Log::error("{}", e.what());
        return 1;
    }
    return 0;
}
