#include "Simulation/BarnesHutCuda.hpp"

#include "Simulation/BarnesHutCuda.cuh"


BarnesHutCuda::BarnesHutCuda(const Config::Simulation &sim_cfg, Bodies &bodies) 
        : Simulation(sim_cfg, bodies) {
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
        compute_morton_codes();
        sort_by_morton_code();

        // build quad tree

        // update velocities

        // update positions

        post_iteration();
    }
}
