#include "Simulation/BarnesHutCuda.hpp"

#include <limits>


BarnesHutCuda::BarnesHutCuda(Config::Simulation& sim_cfg, Bodies& bodies)
        : Simulation(sim_cfg, bodies), n(static_cast<uint32_t>(bodies.n)),
          inv_theta_sq(sim_cfg.theta > 0.0 ? 1.0 / (sim_cfg.theta * sim_cfg.theta)
                                           : std::numeric_limits<double>::max()) {
    // The tree refers to bodies by 32-bit signed indices
    if (bodies.n > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
        throw std::runtime_error("Too many bodies for Barnes-Hut GPU");
    init_device_resources();
}

BarnesHutCuda::~BarnesHutCuda() {
    if (get_state() == State::RUNNING) {
        pause();
    }
    release_device_resources();
}
