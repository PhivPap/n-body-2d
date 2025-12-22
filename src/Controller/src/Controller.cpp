#include "Controller/Controller.hpp"

#include <csignal>
#include <unistd.h>
#include <string.h>

#include "Logger/Logger.hpp"
#include "Constants/Constants.hpp"

volatile bool Controller::sigint_flag = false;

Controller::Controller(Config &cfg, Simulation &sim, Graphics &graphics) : 
        cfg(cfg), sim(sim), graphics(graphics), 
        stats_update_rate_limiter(std::chrono::duration<float>(1 / cfg.graphics.panel_update_hz)) 
        {}

void Controller::handle_events(sf::RenderWindow &window) {
    while (const std::optional event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window.close();
            Log::info("Closed window");
        }
        else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
            graphics.resize_view(sf::Vector2f(resized->size));
        }
        else if (const auto *scrolled = event->getIf<sf::Event::MouseWheelScrolled>()) {
            graphics.zoom_view(scrolled->delta);
        }
        else if (const auto *mouse_clicked = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouse_clicked->button == sf::Mouse::Button::Left) {
                graphics.grab_view();
            }
        }
        else if (const auto *mouse_released = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouse_released->button == sf::Mouse::Button::Left) {
                graphics.release_view();
            }
        }
        else if (const auto *key_pressed = event->getIf<sf::Event::KeyPressed>()) {
            switch (key_pressed->scancode) {
            case sf::Keyboard::Scan::Space:
                if (sim.get_state() == Simulation::State::PAUSED) {
                    sim.run();
                }
                else {
                    sim.pause();
                }
                break;
            case sf::Keyboard::Scan::G:
                cfg.graphics.grid_enabled = !cfg.graphics.grid_enabled;
                graphics.set_grid(cfg.graphics.grid_enabled);
                break;
            case sf::Keyboard::Scan::S:
                cfg.graphics.show_panel = !cfg.graphics.show_panel;
                graphics.get_panel().set_visible(cfg.graphics.show_panel);
                break;
            }
        }
    }
}

void Controller::update_stats() {
    const auto sim_stats = sim.get_stats();
    const auto graphics_stats = graphics.get_stats();

    graphics.get_panel().update_displayed_data(Panel::DisplayedData{
        .timestep_s = cfg.sim.timestep,
        .algorithm = cfg.sim.simtype_str,
        .theta = Constants::Simulation::THETA,
        .threads = cfg.sim.threads,
        .viewport_m = graphics_stats.viewport_m,
        .viewport_px = graphics_stats.viewport_px,
        .vsync = cfg.graphics.vsync_enabled,
        .grid = cfg.graphics.grid_enabled,
        .max_fps = cfg.graphics.fps,
        .iteration = sim_stats.iteration,
        .iter_per_sec = sim_stats.ips,
        .frame = graphics_stats.frame,
        .fps = graphics_stats.fps,
        .elapsed_s = sim_stats.real_elapsed_s,
        .simulated_time_s = sim_stats.simulated_elapsed_s,
    });
}

void Controller::run() {
    StopWatch sw;
    sim.run();
    sf::RenderWindow &window = graphics.get_window();
    while (!sim.is_finished()) {
        if (sigint_flag || !window.isOpen()) {
            sim.pause();
            break;
        }
        handle_events(window);
        stats_update_rate_limiter.try_call(std::bind(&Controller::update_stats, this));
        graphics.draw_frame();
    }
    Log::debug("Sim done: [{}]", sw);
}
