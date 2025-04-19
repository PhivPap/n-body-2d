#include "ViewPort/ViewPort.hpp"

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"


ViewPort::ViewPort(sf::Vector2f window_res, double pixel_scale) : window_res(window_res), 
        pixel_scale(pixel_scale) {
    init_rect();
}

void ViewPort::init_rect() {
    compute_rect_size();
    rect.position = rect.position - (rect.size / 2.0);
}

void ViewPort::compute_rect_size() {
    rect.size = sf::Vector2<double>(window_res) * pixel_scale;
}

void ViewPort::zoom(Zoom direction, sf::Vector2f cursor_pos) {
    if (direction == Zoom::IN) {
        const double new_pixel_res = pixel_scale * Constants::ZOOM_FACTOR;
        if (new_pixel_res < Constants::MIN_PIXEL_RES) {
            Log::warning("Reached max zoom, cannot zoom in");
            return;
        }
        pixel_scale = new_pixel_res;
        rect.position += sf::Vector2<double>(cursor_pos.componentWiseDiv(window_res))
                .componentWiseMul(rect.size) * (1 - Constants::ZOOM_FACTOR);
        compute_rect_size();
    }
    else {
        const double new_pixel_res = pixel_scale / Constants::ZOOM_FACTOR;
        if (new_pixel_res > Constants::MAX_PIXEL_RES) {
            Log::warning("Reached min zoom, cannot zoom out");
            return;
        }
        pixel_scale = new_pixel_res;
        compute_rect_size();
        rect.position -= sf::Vector2<double>(cursor_pos.componentWiseDiv(window_res))
                .componentWiseMul(rect.size) * (1 - Constants::ZOOM_FACTOR);
    }
}

void ViewPort::resize(sf::Vector2f new_res) {
    window_res = new_res;
    compute_rect_size();
}

void ViewPort::pan(sf::Vector2f pan_pixels) {
    rect.position += sf::Vector2<double>(pan_pixels.componentWiseDiv(window_res))
            .componentWiseMul(rect.size);
}

sf::Vector2f ViewPort::body_on_viewport(
        const sf::Vector2<double> &body_pos) {
    const auto relative_pos = (body_pos - rect.position).componentWiseDiv(rect.size);
    return window_res.componentWiseMul(sf::Vector2f(relative_pos));
}
