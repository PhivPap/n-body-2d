#include "Simulation/AllPairs.hpp"


AllPairsSim::AllPairsSim(const Config::Simulation& sim_cfg, Bodies& bodies)
        : Simulation(sim_cfg, bodies) {}

AllPairsSim::~AllPairsSim() {
    if (get_state() == State::RUNNING) {
        pause();
    }
}

void AllPairsSim::step(double kick, double drift) {
    update_velocities(kick);
    update_positions(drift);
}

void AllPairsSim::update_positions(double drift) {
    for (uint64_t i = 0; i < bodies.n; i++) {
        bodies.pos(i) += bodies.vel(i) * drift;
    }
}

// This algorithm only iterates each pair once calculating the accelerations both ways
void AllPairsSim::update_velocities(double kick) {
    for (uint64_t i = 0; i < bodies.n; i++) {
        sf::Vector2<double> acc = {0.0, 0.0};
        const double mass_kick = bodies.mass(i) * kick;
        for (uint64_t j = i + 1; j < bodies.n; j++) {
            const sf::Vector2<double> p = pull(bodies.pos(i), bodies.pos(j));
            acc += p * bodies.mass(j);
            bodies.vel(j) -= p * mass_kick;
        }
        bodies.vel(i) += acc * kick;
    }
}
