#include "Simulation/Simulation.hpp"

#include "Exit/Exit.hpp"
#include "Logger/Logger.hpp"

constexpr double e = 1e39;

Simulation::Simulation(const Config &cfg, std::vector<Body> &bodies) : 
        cfg(cfg), bodies(bodies) {}

void Simulation::start() {
    sim_thread = std::thread(&Simulation::simulate, this);
}

void Simulation::join() {
    sim_thread.join();
}

void Simulation::simulate() {
    StopWatch sw;
    uint64_t i;
    for (i = 0; (i < cfg.iterations) && !exit_app; i++) {
        update_positions();
        update_velocities();
    }
    Log::debug("Iterations: {}/{}", i, cfg.iterations);
    Log::debug("Iterations per second: {}", i / sw.elapsed());
}

void Simulation::update_positions() {
    for (Body &body : bodies) {
        body.pos += body.vel * cfg.timestep;
    }
}

// This algorithm only iterates each pair once calculating the forces both ways
void Simulation::update_velocities() {
    Body* _bodies = bodies.data();
    const uint64_t total_bodies = bodies.size();
    for (uint64_t i = 0; i < total_bodies - 1; i++) {
        Body &body_a = _bodies[i];
        sf::Vector2<double> force_sum = {0.0, 0.0};
        double Fx_sum = 0, Fy_sum = 0;
        for (uint64_t j = i + 1; j < total_bodies; j++) {
            Body &body_b = _bodies[j];
            const sf::Vector2<double> force = gravitational_force(body_a, body_b);
            body_b.vel -= force / body_b.mass * cfg.timestep;
            force_sum += force;
        }
        body_a.vel += force_sum / body_a.mass * cfg.timestep;
    }
}

double Simulation::euclidean_distance(const sf::Vector2<double> &pos_a, 
        const sf::Vector2<double> &pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y + e);
}

sf::Vector2<double> Simulation::gravitational_force(const Body &body_a, const Body &body_b) {
    const double distance = euclidean_distance(body_a.pos, body_b.pos);
    const double force_amplitude = Constants::G * body_a.mass * body_b.mass / 
            (distance * distance + e);
    return {
        force_amplitude * (body_b.pos.x - body_a.pos.x) / distance,
        force_amplitude * (body_b.pos.y - body_a.pos.y) / distance
    };
}
