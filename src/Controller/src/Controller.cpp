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
            case sf::Keyboard::Scan::Left:
                timestep_decrease();
                break;
            case sf::Keyboard::Scan::Right:
                timestep_increase();
                break;
            case sf::Keyboard::Scan::Up:
                graphics.body_size_increase();
                break;
            case sf::Keyboard::Scan::Down:
                graphics.body_size_decrease();
                break;
            }
        }
    }
}

void Controller::init_panel() {
    auto panel_handle = graphics.get_panel().write_handle();
    panel_handle->timestep_s = cfg.sim.timestep;
    panel_handle->algorithm = cfg.sim.simtype_str;
    panel_handle->theta = Constants::Simulation::THETA;
    panel_handle->softening_factor = cfg.sim.softening_factor;
    panel_handle->threads = cfg.sim.threads;
    panel_handle->viewport_m = {0, 0};
    panel_handle->viewport_px = {0, 0};
    panel_handle->vsync = cfg.graphics.vsync_enabled;
    panel_handle->grid = cfg.graphics.grid_enabled;
    panel_handle->max_fps = cfg.graphics.fps;
    panel_handle->iteration = 0;
    panel_handle->iter_per_sec = 0;
    panel_handle->frame = 0;
    panel_handle->fps = 0;
    panel_handle->elapsed_s = 0;
    panel_handle->simulated_time_s = 0;
}

void Controller::update_stats() {
    const auto sim_stats = sim.get_stats();
    const auto graphics_stats = graphics.get_stats();

    auto panel_handle = graphics.get_panel().write_handle();

    panel_handle->iteration = sim_stats.iteration;
    panel_handle->iter_per_sec = sim_stats.ips;
    panel_handle->elapsed_s = sim_stats.real_elapsed_s;
    panel_handle->simulated_time_s = sim_stats.simulated_elapsed_s;
    panel_handle->iteration = sim_stats.iteration;
    panel_handle->viewport_m = graphics_stats.viewport_m;
    panel_handle->viewport_px = graphics_stats.viewport_px;
    panel_handle->frame = graphics_stats.frame;
    panel_handle->fps = graphics_stats.fps;
}

void Controller::timestep_increase() {
    auto new_timestep = cfg.sim.timestep * Constants::Simulation::TIMESTEP_CHANGE_FACTOR;
    if (new_timestep > Constants::Simulation::TIMESTEP_RANGE.second) {
        Log::warning("Reached maximum timestep, cannot accelerate further");
        new_timestep = Constants::Simulation::TIMESTEP_RANGE.second;
    }
    cfg.sim.timestep = new_timestep;
    sim.set_timestep(new_timestep);
    graphics.get_panel().write_handle()->timestep_s = new_timestep;
}

void Controller::timestep_decrease() {
    auto new_timestep = cfg.sim.timestep / Constants::Simulation::TIMESTEP_CHANGE_FACTOR;
    if (new_timestep < Constants::Simulation::TIMESTEP_RANGE.first) {
        Log::warning("Reached minimum timestep, cannot decelerate further");
        new_timestep = Constants::Simulation::TIMESTEP_RANGE.first;
    }
    cfg.sim.timestep = new_timestep;
    sim.set_timestep(new_timestep);
    graphics.get_panel().write_handle()->timestep_s = new_timestep;
}

void Controller::run() {
    StopWatch sw;
    init_panel();
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
