#include <thread>
#include <signal.h>
#include <mutex>
#include <shared_mutex>

#include "SFML/Window.hpp"
#include "SFML/Graphics.hpp"
#include "SFML/Window/Event.hpp"
#include "SFML/Graphics/Rect.hpp"

#include "Config/Config.hpp"
#include "InputOutput/InputOutput.hpp"
#include "Logger/Logger.hpp"
#include "Constants.hpp"
#include "Body/Body.hpp"
#include "CLArgs/CLArgs.hpp"
#include "StopWatch/StopWatch.hpp"

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
    if (relative_pos.x < 0.0 || relative_pos.x >= 1.0 || relative_pos.y < 0.0 || relative_pos.y >= 1.0) {
        return std::nullopt;
    }
    return static_cast<sf::Vector2<float>>(relative_pos);
}

void display(const Config &cfg, const std::vector<Body> &bodies) {
    const sf::Vector2<float> window_res_f(cfg.resolution);
    sf::RenderWindow window(sf::VideoMode(cfg.resolution), "N-Body Sim");
    window.setFramerateLimit(cfg.fps);
    window.setVerticalSyncEnabled(cfg.vsync_enabled);
    const sf::Rect<double> viewport {{-1e12, -5625e8}, {2e12, 1.125e+12}};
    float x = 0, y = 0;
    while (window.isOpen() && !sim_done) {
        while (const std::optional event = window.pollEvent()) {
            // Close window: exit
            if (event->is<sf::Event::Closed>()) {
                window.close();
                sim_done = true;
                Log::info("Window closed");
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

void run_sim(const Config &cfg, std::vector<Body> &bodies) {
    StopWatch sw;
    std::thread sim(simulate, std::ref(bodies), cfg.iterations, cfg.timestep);
    if (cfg.graphics_enabled) {
         display(cfg, bodies);
    }
    sim.join();
    Log::debug("Sim done: [{}]", sw);   
}

void exit_gracefully(int signum) {
    assert (signum == SIGINT);
    constexpr const char *txt = "\nInterrupted (SIGINT)\n";
    write(STDOUT_FILENO, txt, strlen(txt) + 1);
    sim_done = true;
}

int main(int argc, const char *argv[]) {
    try {
        const CLArgs clargs(argc, argv);
        const Config cfg(clargs.config);
        std::vector<Body> bodies = IO::parse_csv(cfg.universe_infile.string(), cfg.echo_bodies);
        signal(SIGINT, exit_gracefully);
        run_sim(cfg, bodies);
        IO::write_csv(cfg.universe_outfile.string(), bodies);
    }
    catch (const std::exception &e) {
        Log::error(e.what());
        return 1;
    }
    return 0;
}
