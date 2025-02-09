#include <iostream>
#include <chrono>
#include <thread>

#include "SFML/Window.hpp"

#include "Config.hpp"
#include "InputOutput.hpp"
#include "Logger.hpp"
#include "Constants.hpp"

volatile bool sim_done = false;

double calculate_distance(const Pos2D &pos_a, const Pos2D &pos_b) {
    const double dx = pos_a.x - pos_b.x;
    const double dy = pos_a.y - pos_b.y;
    return std::sqrt(dx * dx + dy * dy);
}

double calculate_force(double mass_a, double mass_b, double distance) {
    return Constants::G * mass_a * mass_b / (distance * distance);
}

// O(n^2) solution
void update_velocities(std::vector<Body> &bodies, double timestep) {
    for (Body &body_a : bodies) {
        for (const Body &body_b : bodies) {
            if (&body_a != &body_b) {
                const double distance = calculate_distance(body_a.pos, body_b.pos);
                const double force = calculate_force(body_a.mass, body_b.mass, distance);
                const double Fx = force * (body_b.pos.x - body_a.pos.x) / distance;
                const double Fy = force * (body_b.pos.y - body_a.pos.y) / distance;

                body_a.vel.x += Fx / body_a.mass * timestep;
                body_a.vel.y += Fy / body_a.mass * timestep;
            }
        }
    }
}

void update_positions(std::vector<Body> &bodies, double timestep) {
    for (Body &body : bodies) {
        const double dx = body.vel.x * timestep;
        const double dy = body.vel.y * timestep;
        body.pos.x += dx;
        body.pos.y += dy;
    }
}

void simulate(std::vector<Body> &bodies, uint64_t iterations, double timestep) {
    for (uint64_t i = 0; i < iterations; i++) {
        update_positions(bodies, timestep);
        update_velocities(bodies, timestep);
    }
    sim_done = true;
}

void display() {
    sf::Window window(sf::VideoMode({800, 600}), "N-Body Sim");
    while (!sim_done) {
        // Do stuff
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

int main() {
    try {
        const Config cfg("../config.json");
        Log::debug("Parsed config");

        std::vector<Body> bodies = IO::parse_csv(cfg.universe_infile);
        Log::debug("Parsed {} bodies from `{}`", bodies.size(), cfg.universe_infile);

        std::thread(simulate, std::ref(bodies), cfg.iterations, cfg.timestep).detach();
        display();

        IO::write_csv(cfg.universe_outfile, bodies);
        Log::debug("Wrote {} bodies to `{}`", bodies.size(), cfg.universe_outfile);
    }
    catch (const std::exception &e) {
        Log::error(e.what());   
    }
}
