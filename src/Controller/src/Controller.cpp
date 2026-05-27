#include "Controller/Controller.hpp"

#include <csignal>
#include <unistd.h>

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"


volatile bool Controller::sigint_flag = false;

Controller::Controller(Config& cfg, Simulation& sim, Graphics& graphics)
        : cfg(cfg), sim(sim), graphics(graphics),
          stats_update_rate_limiter(
                  std::chrono::duration<float>(1 / cfg.graphics.panel_update_hz)) {}

void Controller::handle_events(sf::RenderWindow& window) {
    while (const std::optional event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window.close();
            Log::info("Closed window");
        }
        else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            graphics.resize_view(sf::Vector2f(resized->size));
        }
        else if (const auto* scrolled = event->getIf<sf::Event::MouseWheelScrolled>()) {
            graphics.zoom_view(scrolled->delta);
        }
        else if (const auto* mouse_clicked = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouse_clicked->button == sf::Mouse::Button::Left) {
                graphics.grab_view();
            }
            else if (mouse_clicked->button == sf::Mouse::Button::Right) {
                graphics.grab_select();
            }
        }
        else if (const auto* mouse_released = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouse_released->button == sf::Mouse::Button::Left) {
                graphics.release_view();
            }
            else if (mouse_released->button == sf::Mouse::Button::Right) {
                graphics.release_select();
            }
        }
        else if (const auto* key_pressed = event->getIf<sf::Event::KeyPressed>()) {
            switch (key_pressed->scancode) {
            case sf::Keyboard::Scan::Space:
                if (sim.get_state() == Simulation::State::PAUSED) {
                    sim.run();
                    graphics.notify_resumed();
                }
                else {
                    sim.pause();
                    graphics.notify_paused();
                }
                break;
            case sf::Keyboard::Scan::G:
                graphics.toggle_grid();
                break;
            case sf::Keyboard::Scan::S:
                graphics.toggle_selection_show();
                break;
            case sf::Keyboard::Scan::D:
                graphics.toggle_selection_show_center_of_mass();
                break;
            case sf::Keyboard::Scan::F:
                graphics.toggle_follow_selected();
                break;
            case sf::Keyboard::Scan::C:
                graphics.center_on_selection_center_of_mass();
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
            case sf::Keyboard::Scan::F1:
                graphics.toggle_commands_panel();
                break;
            case sf::Keyboard::Scan::F2:
                graphics.toggle_config_panel();
                break;
            case sf::Keyboard::Scan::F3:
                graphics.toggle_stats_panel();
                break;
            case sf::Keyboard::Scan::F4:
                graphics.toggle_action_log_panel();
                break;
            }
        }
    }
}

void Controller::init_panels() {
    {
        auto write_handle = graphics.get_config_panel().write_handle();
        write_handle->timestep_s = cfg.sim.timestep;
        write_handle->algorithm = cfg.sim.simtype_str;
        write_handle->theta = cfg.sim.theta;
        write_handle->show_theta =
                cfg.sim.simtype == Config::Simulation::SimType::BARNES_HUT
                || cfg.sim.simtype == Config::Simulation::SimType::BARNES_HUT_GPU;
        write_handle->softening_factor = cfg.sim.softening_factor;
        write_handle->threads = cfg.sim.threads;
        write_handle->show_threads = cfg.sim.simtype != Config::Simulation::SimType::BARNES_HUT_GPU;
        write_handle->viewport_m = {0, 0};
        write_handle->viewport_px = {0, 0};
        write_handle->vsync = cfg.graphics.vsync_enabled;
        write_handle->grid = cfg.graphics.show_grid;
        write_handle->max_fps = cfg.graphics.fps;
    }

    {
        auto write_handle = graphics.get_stats_panel().write_handle();
        write_handle->iteration = 0;
        write_handle->iter_per_sec = 0;
        write_handle->frame = 0;
        write_handle->fps = 0;
        write_handle->elapsed_s = 0;
        write_handle->simulated_time_s = 0;
        write_handle->simulation_rate = std::numeric_limits<double>::quiet_NaN();
    }
}

void Controller::update_panels() {
    const auto sim_stats = sim.get_stats();
    const auto graphics_stats = graphics.get_stats();

    {
        auto write_handle = graphics.get_config_panel().write_handle();
        write_handle->viewport_m = graphics_stats.viewport_m;
        write_handle->viewport_px = graphics_stats.viewport_px;
    }

    {
        auto write_handle = graphics.get_stats_panel().write_handle();
        write_handle->iteration = sim_stats.iteration;
        write_handle->iter_per_sec = sim_stats.ips;
        write_handle->frame = graphics_stats.frame;
        write_handle->fps = graphics_stats.fps;
        write_handle->elapsed_s = sim_stats.real_elapsed_s;
        write_handle->simulated_time_s = sim_stats.simulated_elapsed_s;
        write_handle->simulation_rate = sim_stats.ips * cfg.sim.timestep;
    }
}

void Controller::timestep_increase() {
    auto new_timestep = cfg.sim.timestep * Constants::Simulation::TIMESTEP_CHANGE_FACTOR;
    if (new_timestep > Constants::Simulation::TIMESTEP_RANGE.second) {
        Log::warning("Reached maximum timestep, cannot accelerate further");
        new_timestep = Constants::Simulation::TIMESTEP_RANGE.second;
    }
    if (new_timestep != cfg.sim.timestep) {
        sim.set_timestep(new_timestep);
        graphics.notify_timestep_changed(cfg.sim.timestep, new_timestep);
        cfg.sim.timestep = new_timestep;
    }
}

void Controller::timestep_decrease() {
    auto new_timestep = cfg.sim.timestep / Constants::Simulation::TIMESTEP_CHANGE_FACTOR;
    if (new_timestep < Constants::Simulation::TIMESTEP_RANGE.first) {
        Log::warning("Reached minimum timestep, cannot decelerate further");
        new_timestep = Constants::Simulation::TIMESTEP_RANGE.first;
    }
    if (new_timestep != cfg.sim.timestep) {
        sim.set_timestep(new_timestep);
        graphics.notify_timestep_changed(cfg.sim.timestep, new_timestep);
        cfg.sim.timestep = new_timestep;
    }
}

void Controller::run() {
    StopWatch sw;
    init_panels();
    sim.run();
    sf::RenderWindow& window = graphics.get_window();
    while (!sim.is_finished()) {
        if (sigint_flag || !window.isOpen()) {
            sim.pause();
            break;
        }
        handle_events(window);
        stats_update_rate_limiter.try_call(std::bind(&Controller::update_panels, this));
        graphics.draw_frame();
    }
    Log::debug("Sim done: [{}]", sw);
}
