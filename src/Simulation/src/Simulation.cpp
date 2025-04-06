#include "Simulation/Simulation.hpp"

#include "Exit/Exit.hpp"
#include "Logger/Logger.hpp"

Simulation::Simulation(const Config &cfg, std::vector<Body> &bodies) : 
        cfg(cfg), bodies(bodies) {}

Simulation::~Simulation() {}

bool Simulation::is_done() {
    return done;
}

NaiveSim::NaiveSim(const Config &cfg, std::vector<Body> &bodies) : 
        Simulation(cfg, bodies) {}

NaiveSim::~NaiveSim() {}

void NaiveSim::start() {
    sim_thread = std::thread(&NaiveSim::simulate, this);
}

void NaiveSim::pause() {
    Log::warning("Unimplemented");
}

void NaiveSim::stop() {
    done = true;
    sim_thread.join();
}

void NaiveSim::simulate() {
    StopWatch sw;
    uint64_t i;
    for (i = 0; (i < cfg.iterations) && !done; i++) {
        update_positions();
        update_velocities();
    }
    done = true;
    Log::debug("Iterations: {}/{}", i, cfg.iterations);
    Log::debug("Iterations per second: {}", i / sw.elapsed());
}

void NaiveSim::update_positions() {
    for (Body &body : bodies) {
        body.pos += body.vel * cfg.timestep;
    }
}

// This algorithm only iterates each pair once calculating the forces both ways
void NaiveSim::update_velocities() {
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

double NaiveSim::euclidean_distance(const sf::Vector2<double> &pos_a, 
        const sf::Vector2<double> &pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
}

sf::Vector2<double> NaiveSim::gravitational_force(const Body &body_a, const Body &body_b) {
    const double distance = euclidean_distance(body_a.pos, body_b.pos);
    const double force_amplitude = Constants::G * body_a.mass * body_b.mass / 
            (distance * distance + e);
    return {
        force_amplitude * (body_b.pos.x - body_a.pos.x) / distance,
        force_amplitude * (body_b.pos.y - body_a.pos.y) / distance
    };
}
