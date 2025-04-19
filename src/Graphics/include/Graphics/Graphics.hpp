#pragma once

#include <vector>

#include "Config/Config.hpp"
#include "Body/Body.hpp"
#include "ViewPort/ViewPort.hpp"
#include "RLCaller/RLCaller.hpp"
#include "FPSCounter/FPSCounter.hpp"

class Graphics {
private:
    RLCaller rl_5_sec {std::chrono::seconds{5}};
    FPSCounter fps_counter;
    const std::vector<Body> &bodies;
    sf::RenderWindow window;
    ViewPort vp;
    bool grid_enabled = true;
    std::optional<sf::Vector2i> opt_view_grabbed_pos = std::nullopt;

    void pan_if_view_grabbed();
    void draw_grid();
    void draw_bodies();
public:
    Graphics(const Config &cfg, const std::vector<Body> &bodies);
    sf::RenderWindow &get_window();
    void resize_view(sf::Vector2f new_size);
    void zoom_view(double delta);
    void grab_view();
    void release_view();

    void set_grid(bool enabled);
    void draw_frame();
};
