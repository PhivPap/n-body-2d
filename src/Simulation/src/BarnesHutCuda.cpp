#include "Simulation/BarnesHutCuda.hpp"

#include <algorithm>
#include <limits>


BarnesHutCuda::BarnesHutCuda(const Config::Simulation &sim_cfg, Bodies &bodies) 
        : Simulation(sim_cfg, bodies),
          max_quads(bodies.n * 8) {
    init_device_resources();
}

BarnesHutCuda::~BarnesHutCuda() {
    if (sim_thread.joinable()) {
        on_pause();
    }
    release_device_resources();
}

void BarnesHutCuda::on_run() {
    stop = false;
    sim_thread = std::thread(&BarnesHutCuda::simulate, this);
}

void BarnesHutCuda::on_pause() {
    stop = true;
    sim_thread.join();
}

void BarnesHutCuda::simulate() {
    while (!should_stop()) {
        compute_bounding_box();
        build_quad_tree();
        compute_forces();
        update_positions();
        sync_from_gpu_bodies();
        post_iteration();
    }
}
