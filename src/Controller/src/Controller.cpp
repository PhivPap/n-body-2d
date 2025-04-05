#include "Controller/Controller.hpp"

#include <csignal>
#include <unistd.h>
#include <string.h>

#include "Exit/Exit.hpp"
#include "Logger/Logger.hpp"


Controller::Controller(Config &cfg, Simulation &sim, Graphics &graphics) : 
        cfg(cfg), sim(sim), graphics(graphics) {

}

void Controller::handle_events(sf::RenderWindow &window) {
    while (const std::optional event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            Log::info("Exit app");
            exit_app = true;
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
    }
}

void Controller::run() {
    StopWatch sw;
    sim.start();
    sf::RenderWindow &window = graphics.get_window();
    while (window.isOpen() && !exit_app) {
        handle_events(window);
        graphics.draw_frame();
    }
    window.close();
    sim.join();
    Log::debug("Sim done: [{}]", sw);
}
