#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <mutex>
#include <shared_mutex>
#include <optional>

#include "SFML/Window.hpp"
#include "SFML/Graphics.hpp"
#include "SFML/Window/Event.hpp"
#include "SFML/Graphics/Rect.hpp"

#include "Config.hpp"
#include "InputOutput.hpp"
#include "Logger.hpp"
#include "Constants.hpp"
#include "Body.hpp"

typedef std::shared_mutex Lock;
typedef std::unique_lock<Lock> WriteLock;
typedef std::shared_lock<Lock> ReadLock;
Lock rw_body_mutex;

volatile bool sim_done = false;

double calculate_distance(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b) {
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
    for (uint64_t i = 0; i < iterations && !sim_done; i++) {
        WriteLock wlock(rw_body_mutex);
        update_positions(bodies, timestep);
        update_velocities(bodies, timestep);
    }
    sim_done = true;
}

std::optional<sf::Vector2<float>> body_position_on_viewport(const Body& b, const sf::Rect<double> &viewport) {
    const auto relative_pos = (b.pos - viewport.position).componentWiseDiv(viewport.size);
    if (relative_pos.x < 0.0 || relative_pos.x >= 1.0 || relative_pos.y < 0.0 || relative_pos.y >= 1.0)
        return std::nullopt;
    return static_cast<sf::Vector2<float>>(relative_pos);
}

void display(const std::vector<Body> &bodies) {
    const sf::Vector2<uint32_t> window_res {1280, 720};
    const sf::Vector2<float> window_res_f(window_res);
    sf::RenderWindow window(sf::VideoMode(window_res), "N-Body Sim");
    window.setFramerateLimit(60);
    const sf::Rect<double> viewport {{-1e12, -5625e8}, {2e12, 1.125e+12}};
    float x = 0, y = 0;
    while (window.isOpen() && !sim_done) {
        while (const std::optional event = window.pollEvent()) {
            // Close window: exit
            if (event->is<sf::Event::Closed>()) {
                window.close();
                sim_done = true;
            }
        }
        window.clear(sf::Color::Black);
        ReadLock rlock(rw_body_mutex);
        sf::CircleShape shape(2.f);
        shape.setFillColor(sf::Color::White);
        for (const Body& b: bodies) {
            const auto rel_pos = body_position_on_viewport(b, viewport);
            if (rel_pos) {
                shape.setPosition(rel_pos->componentWiseMul(window_res_f));
                window.draw(shape);
            }
        }
        rlock.unlock();
        window.display();
    }
}

void exit_gracefully(int signum) {
    if (signum == SIGINT) {
        sim_done = true;
    }
}

int main() {
    try {
        const Config cfg("../config.json");
        Log::debug("Parsed config");

        std::vector<Body> bodies = IO::parse_csv(cfg.universe_infile);
        Log::debug("Parsed {} bodies from `{}`", bodies.size(), cfg.universe_infile);

        signal(SIGINT, exit_gracefully);

        std::thread sim(simulate, std::ref(bodies), cfg.iterations, cfg.timestep);
        display(bodies);
        sim.join();

        IO::write_csv(cfg.universe_outfile, bodies);
        Log::debug("Wrote {} bodies to `{}`", bodies.size(), cfg.universe_outfile);
    }
    catch (const std::exception &e) {
        Log::error(e.what());   
    }
}
