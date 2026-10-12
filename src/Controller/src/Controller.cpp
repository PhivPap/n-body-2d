#include "Controller/Controller.hpp"

#include <csignal>
#include <unistd.h>

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"


volatile std::sig_atomic_t Controller::sigint_flag = 0;

Controller::Controller(const Config& cfg, Simulation& sim, Graphics& graphics)
        : sim(sim), graphics(graphics),
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
                key_pressed->control ? graphics.trails_length_decrease() : timestep_decrease();
                break;
            case sf::Keyboard::Scan::Right:
                key_pressed->control ? graphics.trails_length_increase() : timestep_increase();
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
            default:
                break;
            }
        }
    }
}

void Controller::update_panels() {
    const auto sim_stats = sim.get_stats();
    graphics.update_simulation_stats({.iteration = sim_stats.iteration, .ips = sim_stats.ips,
            .real_elapsed_s = sim_stats.real_elapsed_s,
            .simulated_elapsed_s = sim_stats.simulated_elapsed_s,
            .timestep_s = sim.get_timestep()});
}

void Controller::timestep_increase() {
    const double timestep = sim.get_timestep();
    auto new_timestep = timestep * Constants::Simulation::TIMESTEP_CHANGE_FACTOR;
    if (new_timestep > Constants::Simulation::TIMESTEP_RANGE.second) {
        Log::warning("Reached maximum timestep, cannot accelerate further");
        new_timestep = Constants::Simulation::TIMESTEP_RANGE.second;
    }
    if (new_timestep != timestep) {
        sim.set_timestep(new_timestep);
        graphics.notify_timestep_changed(timestep, new_timestep);
    }
}

void Controller::timestep_decrease() {
    const double timestep = sim.get_timestep();
    auto new_timestep = timestep / Constants::Simulation::TIMESTEP_CHANGE_FACTOR;
    if (new_timestep < Constants::Simulation::TIMESTEP_RANGE.first) {
        Log::warning("Reached minimum timestep, cannot decelerate further");
        new_timestep = Constants::Simulation::TIMESTEP_RANGE.first;
    }
    if (new_timestep != timestep) {
        sim.set_timestep(new_timestep);
        graphics.notify_timestep_changed(timestep, new_timestep);
    }
}

void Controller::run() {
    StopWatch sw;
    sim.run();
    sf::RenderWindow& window = graphics.get_window();
    while (!sim.is_finished() && !sigint_flag && window.isOpen()) {
        handle_events(window);
        stats_update_rate_limiter.try_call(std::bind(&Controller::update_panels, this));
        graphics.draw_frame();
    }
    // Also joins the threads of a finished simulation
    if (sim.get_state() == Simulation::State::RUNNING) {
        sim.pause();
    }
    Log::debug("Sim done: [{}]", sw);
}
