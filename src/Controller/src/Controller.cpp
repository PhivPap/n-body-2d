#include "Controller/Controller.hpp"

#include <csignal>
#include <unistd.h>
#include <string.h>

#include "Logger/Logger.hpp"

volatile bool Controller::sigint_flag = false;

Controller::Controller(Config &cfg, Simulation &sim, Graphics &graphics) : 
        cfg(cfg), sim(sim), graphics(graphics) {}

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
    }
}

void Controller::run() {
    StopWatch sw;
    sim.run();
    sf::RenderWindow &window = graphics.get_window();
    while (!sim.completed() && !sigint_flag && window.isOpen()) {
        handle_events(window);
        graphics.draw_frame();
    }
    Log::debug("Sim done: [{}]", sw);
}
