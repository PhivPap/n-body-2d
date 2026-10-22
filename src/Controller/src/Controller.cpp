#include "Controller/Controller.hpp"

#include <csignal>
#include <unistd.h>

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Time.hpp"


// A signal handler may only use lock-free atomics, and not volatile std::sig_atomic_t from another
// thread, which it may run on
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> Controller::sigint_flag = false;

Controller::Controller(const Config& cfg, Simulation& sim, Graphics& graphics, Ui::Context& ui)
        : sim(sim), graphics(graphics), ui(ui),
          stats_update_rate_limiter(
                  std::chrono::duration<float>(1 / cfg.graphics.panel_update_hz)) {}

void Controller::handle_events(sf::RenderWindow& window) {
    while (const std::optional event = window.pollEvent()) {
        ui.process_event(*event);
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
        else if (event->is<sf::Event::MouseLeft>()) {
            graphics.set_cursor_in_window(false);
        }
        else if (event->is<sf::Event::MouseEntered>()) {
            graphics.set_cursor_in_window(true);
        }
        else if (const auto* mouse_clicked = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouse_clicked->button == sf::Mouse::Button::Left) {
                graphics.grab_view();
            }
            else if (mouse_clicked->button == sf::Mouse::Button::Right) {
                graphics.grab_select(mouse_clicked->position);
            }
        }
        else if (const auto* mouse_released = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouse_released->button == sf::Mouse::Button::Left) {
                graphics.release_view();
            }
            else if (mouse_released->button == sf::Mouse::Button::Right) {
                graphics.release_select(mouse_released->position);
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
            case sf::Keyboard::Scan::O:
                graphics.toggle_selection_orbit();
                break;
            case sf::Keyboard::Scan::A:
                graphics.toggle_named_orbits();
                break;
            case sf::Keyboard::Scan::L:
                graphics.toggle_labels();
                break;
            case sf::Keyboard::Scan::M:
                graphics.toggle_mass_scaled_markers();
                break;
            case sf::Keyboard::Scan::V:
                graphics.cycle_color_mode();
                break;
            case sf::Keyboard::Scan::E:
                graphics.toggle_exposure_lock();
                break;
            case sf::Keyboard::Scan::LBracket:
                graphics.exposure_decrease();
                break;
            case sf::Keyboard::Scan::RBracket:
                graphics.exposure_increase();
                break;
            case sf::Keyboard::Scan::B:
                graphics.toggle_bloom();
                break;
            case sf::Keyboard::Scan::Num1:
            case sf::Keyboard::Scan::Num2:
            case sf::Keyboard::Scan::Num3:
            case sf::Keyboard::Scan::Num4:
            case sf::Keyboard::Scan::Num5:
            case sf::Keyboard::Scan::Num6:
            case sf::Keyboard::Scan::Num7:
            case sf::Keyboard::Scan::Num8:
            case sf::Keyboard::Scan::Num9: {
                // Legend rows: Num1 to Num9 are in sequence
                const auto row = static_cast<uint32_t>(key_pressed->scancode)
                                 - static_cast<uint32_t>(sf::Keyboard::Scan::Num1);
                key_pressed->shift ? graphics.solo_group(row) : graphics.toggle_group(row);
                break;
            }
            case sf::Keyboard::Scan::Num0:
                graphics.show_all_groups();
                break;
            case sf::Keyboard::Scan::Left:
                key_pressed->control ? graphics.trails_length_decrease() : timestep_decrease();
                break;
            case sf::Keyboard::Scan::Right:
                key_pressed->control ? graphics.trails_length_increase() : timestep_increase();
                break;
            case sf::Keyboard::Scan::Up:
                key_pressed->control ? graphics.stretch_increase() : graphics.body_size_increase();
                break;
            case sf::Keyboard::Scan::Down:
                key_pressed->control ? graphics.stretch_decrease() : graphics.body_size_decrease();
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
            case sf::Keyboard::Scan::F5:
                graphics.toggle_inspector_panel();
                break;
            case sf::Keyboard::Scan::F6:
                graphics.toggle_legend_panel();
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
        if (!window.isOpen()) {
            break;  // closed: nothing to draw on
        }
        stats_update_rate_limiter.try_call(std::bind(&Controller::update_panels, this));
        ui.new_frame();
        graphics.draw_frame();
        ui.render();
        window.display();
    }
    // Also joins the threads of a finished simulation
    if (sim.get_state() == Simulation::State::RUNNING) {
        sim.pause();
    }
    Log::debug("Sim done: [{}]", sw);
}

void Controller::run_headless(Simulation& sim) {
    using namespace std::chrono_literals;
    StopWatch sw;
    StopWatch progress_sw;
    sim.run();
    while (!sim.is_finished() && !sigint_flag) {
        std::this_thread::sleep_for(10ms);
        if (progress_sw.duration<std::chrono::seconds>() >= 10s) {
            progress_sw.reset();
            const auto stats = sim.get_stats();
            Log::info("Iteration {} ({:.1f} it/s), simulated {}", stats.iteration, stats.ips,
                    Log::Time::from(stats.simulated_elapsed_s));
        }
    }
    if (sim.get_state() == Simulation::State::RUNNING) {
        sim.pause();
    }
    Log::debug("Sim done: [{}]", sw);
}
