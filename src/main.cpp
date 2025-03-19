#include <thread>
#include <signal.h>

#include "SFML/Window.hpp"
#include "SFML/Graphics.hpp"

#include "Config/Config.hpp"
#include "InputOutput/InputOutput.hpp"
#include "Logger/Logger.hpp"
#include "Constants.hpp"
#include "Body/Body.hpp"
#include "CLArgs/CLArgs.hpp"
#include "StopWatch/StopWatch.hpp"
#include "ViewPort/ViewPort.hpp"


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
        update_positions(bodies, timestep);
        update_velocities(bodies, timestep);
    }
    sim_done = true;
}

void display(const Config &cfg, const std::vector<Body> &bodies) {
    ViewPort vp(static_cast<sf::Vector2f>(cfg.resolution), cfg.pixel_resolution);
    sf::RenderWindow window(sf::VideoMode(cfg.resolution), "N-Body Sim");
    window.setFramerateLimit(cfg.fps);
    window.setVerticalSyncEnabled(cfg.vsync_enabled);
    while (window.isOpen() && !sim_done) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
                sim_done = true;
                Log::info("Window closed");
            }
            else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                vp.resize(sf::Vector2f(resized->size));
                sf::Rect<float> area({0.f, 0.f}, sf::Vector2f(resized->size));
                window.setView(sf::View(area));
            }
            else if (const auto *scrolled = event->getIf<sf::Event::MouseWheelScrolled>()) {
                vp.zoom(scrolled->delta > 0 ? ViewPort::Zoom::IN : ViewPort::Zoom::OUT, 
                        sf::Vector2f(sf::Mouse::getPosition(window)));
            }
        }
        window.clear(sf::Color::Black);
        sf::CircleShape shape(2.f);
        shape.setFillColor(sf::Color::White);
        for (const Body& b: bodies) {
            const auto pos_on_vp = vp.body_on_viewport(b.pos);
            if (pos_on_vp) {
                shape.setPosition(static_cast<sf::Vector2f>(*pos_on_vp));
                window.draw(shape);
            }
        }
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
