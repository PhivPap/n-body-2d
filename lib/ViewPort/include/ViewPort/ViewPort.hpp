#pragma once

#include <optional>

#include "SFML/System.hpp"
#include "SFML/Graphics.hpp"


class ViewPort {
private:
    sf::Vector2f window_res;
    double pixel_res;
    sf::Rect<double> rect;

    void init_rect();
    void compute_rect_size();
public:
    enum class Zoom { IN, OUT };
    ViewPort() = delete;
    ViewPort(sf::Vector2f window_res, double pixel_res);
    void zoom(Zoom direction, sf::Vector2f cursor_pos);
    void resize(sf::Vector2f new_res);
    void pan(sf::Vector2f pan_pixels);
    std::optional<sf::Vector2f> body_on_viewport(const sf::Vector2<double> &body_pos);
};
