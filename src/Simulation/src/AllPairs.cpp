#include "Simulation/AllPairs.hpp"


AllPairsSim::AllPairsSim(const Config::Simulation &sim_cfg, Bodies &bodies) : 
        Simulation(sim_cfg, bodies) {}

AllPairsSim::~AllPairsSim() {
    if (sim_thread.joinable()) {
        on_pause();
    }
}

void AllPairsSim::on_run() {
    stop = false;
    sim_thread = std::thread(&AllPairsSim::simulate, this);
}

void AllPairsSim::on_pause() {
    stop = true;
    sim_thread.join();
}

void AllPairsSim::simulate() {
    while (!should_stop()) {
        update_positions();
        update_velocities();
        post_iteration();
    }
}

void AllPairsSim::update_positions() {
    for (uint64_t i = 0; i < bodies.n; i++) {
        bodies.pos(i) += bodies.vel(i) * timestep;
    }
}

// This algorithm only iterates each pair once calculating the forces both ways
void AllPairsSim::update_velocities() {
    for (uint64_t i = 0; i < bodies.n; i++) {
        sf::Vector2<double> force_sum = {0.0, 0.0};
        for (uint64_t j = i + 1; j < bodies.n; j++) {
            const sf::Vector2<double> f = force(bodies.pos(i), bodies.pos(j), 
                    bodies.mass(i), bodies.mass(j));
            bodies.vel(j) -= f / bodies.mass(j) * timestep;
            force_sum += f;
        }
        bodies.vel(i) += force_sum / bodies.mass(i) * timestep;
    }
}
