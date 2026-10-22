#pragma once

#include "SFML/Graphics/RenderWindow.hpp"
#include "SFML/System/Clock.hpp"
#include "SFML/Window/Event.hpp"
#include "imgui.h"


// Dear ImGui in the window, through ImGui-SFML
namespace Ui {
// The app's fonts, loaded by the Context
struct Fonts {
    ImFont* text = nullptr;        // Ubuntu Regular: labels and text
    ImFont* title = nullptr;       // Ubuntu Medium: titles
    ImFont* mono = nullptr;        // Ubuntu Mono: numbers, ids and keys
    ImFont* mono_small = nullptr;  // Ubuntu Mono, smaller: keycaps and key hints
    ImFont* heading = nullptr;     // Ubuntu Medium, larger: the hub's scenario names
    ImFont* card_title = nullptr;  // Ubuntu Medium, larger still: the intro card's title
};
const Fonts& fonts();

// Dear ImGui on the window, with the app's fonts and theme. Only one at a time.
class Context {
public:
    explicit Context(sf::RenderWindow& window);
    // Closes the window, then shuts ImGui-SFML down: the other way round, it frees cursors that
    // the window still uses (X11 BadCursor)
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    void process_event(const sf::Event& event);  // every event of the window
    void new_frame();                            // before the frame's first ImGui call
    // Draws the frame's ImGui windows, then resets SFML's GL state cache, without which the next
    // SFML draw is lost
    void render();

private:
    sf::RenderWindow& window;
    sf::Clock frame_clock;
};
}  // namespace Ui
