#include <thread>
#include <signal.h>
#include <utility>

#include "SFML/Window.hpp"
#include "SFML/Graphics.hpp"

#include "Config/Config.hpp"
#include "InputOutput/InputOutput.hpp"
#include "Logger/Logger.hpp"
#include "Constants/Constants.hpp"
#include "Body/Body.hpp"
#include "CLArgs/CLArgs.hpp"
#include "StopWatch/StopWatch.hpp"
#include "ViewPort/ViewPort.hpp"

#include "Model/Model.hpp"
#include "View/View.hpp"
#include "Controller/Controller.hpp"


volatile bool sim_done = false;
constexpr double e = 1e39;

double calculate_distance(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b) {
    const double dx = pos_a.x - pos_b.x;
    const double dy = pos_a.y - pos_b.y;
    return std::sqrt(dx * dx + dy * dy);
}

double calculate_force(double mass_a, double mass_b, double distance) {
    return Constants::G * mass_a * mass_b / (distance * distance + e);
}

// O(n^2) solution
void update_velocities(std::vector<Body> &bodies, double timestep) {
    Body* _bodies = bodies.data();
    const uint64_t total_bodies = bodies.size();
    for (uint64_t i = 0; i < total_bodies - 1; i++) {
        Body &body_a = _bodies[i];
        double Fx_sum = 0, Fy_sum = 0;
        for (uint64_t j = i + 1; j < total_bodies; j++) {
            Body &body_b = _bodies[j];
            const double distance = calculate_distance(body_a.pos, body_b.pos);
            const double force = calculate_force(body_a.mass, body_b.mass, distance);
            const double Fx = force * (body_b.pos.x - body_a.pos.x) / distance;
            const double Fy = force * (body_b.pos.y - body_a.pos.y) / distance;
            body_b.vel.x -= Fx / body_b.mass * timestep;
            body_b.vel.y -= Fy / body_b.mass * timestep;
            Fx_sum += Fx;
            Fy_sum += Fy;
        }
        body_a.vel.x += Fx_sum / body_a.mass * timestep;
        body_a.vel.y += Fy_sum / body_a.mass * timestep;
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
    StopWatch sw;
    uint64_t i;
    for (i = 0; i < iterations && !sim_done; i++) {
        update_positions(bodies, timestep);
        update_velocities(bodies, timestep);
    }
    Log::debug("Iterations per second: {}", i / sw.elapsed());
    sim_done = true;
}

void handle_events(sf::RenderWindow& window, ViewPort &vp) {
    // static variables for view panning
    static bool mouse_button_hold = false;
    static sf::Vector2i cursor_pos;

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
        else if (const auto *mouse_clicked = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouse_clicked->button == sf::Mouse::Button::Left) {
                static const auto def_cursor = *sf::Cursor::createFromSystem(
                        sf::Cursor::Type::Cross);
                window.setMouseCursor(def_cursor);
                mouse_button_hold = true;
                cursor_pos = sf::Mouse::getPosition(window);
            }
        }
        else if (const auto *mouse_released = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouse_released->button == sf::Mouse::Button::Left) {
                static const auto grabbed_cursor = *sf::Cursor::createFromSystem(
                        sf::Cursor::Type::Arrow);
                window.setMouseCursor(grabbed_cursor);
                mouse_button_hold = false;
            }
        }
    }

    if (mouse_button_hold) {
        const sf::Vector2i new_cursor_pos = sf::Mouse::getPosition(window);
        vp.pan(sf::Vector2f(cursor_pos - new_cursor_pos));
        cursor_pos = new_cursor_pos;
    }
}

// This calculation guarantees:
// 1. There are at least N grid squares in the smallest window dimension,
// 2. There are at most N*N grid squares in the smallest window dimension.
// Whenever the above conditions break from either zoom or window resizing, 
// the grid spacing will adjust accordingly.
void draw_grid(sf::RenderWindow& window, const ViewPort &vp, uint8_t N) {
    const sf::Rect<double> rect = vp.get_rect();
    const sf::Vector2f res = vp.get_window_res();

    auto hline = sf::RectangleShape({res.x, 1});
    hline.setFillColor(Constants::grid_color);

    auto vline = sf::RectangleShape({1, res.y});
    vline.setFillColor(Constants::grid_color);

    const double min_dim = std::min(rect.size.x, rect.size.y);

    auto log = [](double num, double base) {
        return std::log(num) / std::log(base);
    };

    const double spacing = std::pow(N, std::floor(log(min_dim, N)) - 1);
    
    const double x2 = rect.size.x + rect.position.x;
    double x = ceil(rect.position.x / spacing) * spacing;
    while (x < x2) {
        const float x_ratio = (x - rect.position.x) / rect.size.x;
        vline.setPosition({x_ratio * res.x, 0});
        window.draw(vline);
        x += spacing;
    }

    const double y2 = rect.size.y + rect.position.y;
    double y = ceil(rect.position.y / spacing) * spacing;
    while (y < y2) {
        const float y_ratio = (y - rect.position.y) / rect.size.y;
        hline.setPosition({0, y_ratio * res.y});
        window.draw(hline);
        y += spacing;
    }
}

void display(const Config &cfg, const std::vector<Body> &bodies) {
    ViewPort vp(static_cast<sf::Vector2f>(cfg.resolution), cfg.pixel_scale);
    sf::RenderWindow window(sf::VideoMode(cfg.resolution), "N-Body Sim");
    window.setFramerateLimit(cfg.fps);
    window.setVerticalSyncEnabled(cfg.vsync_enabled);
    while (window.isOpen() && !sim_done) {
        handle_events(window, vp);
        window.clear(Constants::background_color);
        draw_grid(window, vp, 4);
        sf::CircleShape shape(1.f, 12);
        shape.setFillColor(sf::Color(255, 255, 255, 180));
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
        Config cfg(clargs.config);
        std::vector<Body> bodies = IO::parse_csv(cfg.universe_infile.string(), cfg.echo_bodies);
        signal(SIGINT, exit_gracefully);

        Model model(std::as_const(cfg), bodies);
        View view(std::as_const(cfg), std::as_const(bodies));
        Controller controller(cfg, model, view);

        run_sim(cfg, bodies);
        IO::write_csv(cfg.universe_outfile.string(), bodies);
    }
    catch (const std::exception &e) {
        Log::error(e.what());
        return 1;
    }
    return 0;
}
