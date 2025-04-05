#include "Graphics/Graphics.hpp"

#include "Logger/Logger.hpp"
#include "Constants/Constants.hpp"

Graphics::Graphics(const Config &cfg, const std::vector<Body> &bodies) :
        bodies(bodies), window(sf::VideoMode(cfg.resolution), "N-Body Sim"), 
        vp(sf::Vector2f(cfg.resolution), cfg.pixel_scale) {
    window.setFramerateLimit(cfg.fps);
    window.setVerticalSyncEnabled(cfg.vsync_enabled);
}

sf::RenderWindow &Graphics::get_window() {
    return window;
}

void Graphics::pan_if_view_grabbed() {
    if (opt_view_grabbed_pos) {
        const sf::Vector2i new_cursor_pos = sf::Mouse::getPosition(window);
        vp.pan(sf::Vector2f(*opt_view_grabbed_pos - new_cursor_pos));
        opt_view_grabbed_pos = new_cursor_pos;
    }
}

// This calculation guarantees:
// 1. There are at least N grid squares in the smallest window dimension,
// 2. There are at most N*N grid squares in the smallest window dimension.
// Whenever the above conditions break from either zoom or window resizing, 
// the grid spacing will adjust accordingly.
void Graphics::draw_grid() {
    const sf::Rect<double> rect = vp.get_rect();
    const sf::Vector2f res = vp.get_window_res();

    auto hline = sf::RectangleShape({res.x, 1});
    hline.setFillColor(Constants::GRID_COLOR);

    auto vline = sf::RectangleShape({1, res.y});
    vline.setFillColor(Constants::GRID_COLOR);

    const double min_dim = std::min(rect.size.x, rect.size.y);

    auto log = [](double num, double base) {
        return std::log(num) / std::log(base);
    };

    const double spacing = std::pow(Constants::GRID_SPACING_FACTOR, 
            std::floor(log(min_dim, Constants::GRID_SPACING_FACTOR)) - 1);
    
    const double x2 = rect.size.x + rect.position.x;
    double x = ceil(rect.position.x / spacing) * spacing;
    while (x < x2) {
        const float x_ratio = (x - rect.position.x) / rect.size.x;
        vline.setPosition({x_ratio * res.x, 0});
        window.draw(vline);
        x += spacing;
    }

    const double y2 = rect.size.y + rect.position.y;
    double y = ceil(rect.position.y / spacing) * spacing;
    while (y < y2) {
        const float y_ratio = (y - rect.position.y) / rect.size.y;
        hline.setPosition({0, y_ratio * res.y});
        window.draw(hline);
        y += spacing;
    }
}

void Graphics::draw_bodies() {
    sf::CircleShape shape(1.f, 12);
    shape.setFillColor(Constants::BODY_COLOR);
    for (const Body& b: bodies) {
        const auto pos_on_vp = vp.body_on_viewport(b.pos);
        if (pos_on_vp) {
            shape.setPosition(static_cast<sf::Vector2f>(*pos_on_vp));
            window.draw(shape); 
        }
    }
}

void Graphics::resize_view(sf::Vector2f new_size) {
    vp.resize(new_size);
    window.setView(sf::View(sf::Rect<float>{{0.f, 0.f}, new_size}));
}

void Graphics::zoom_view(double delta) {
    if (delta > 0) {
        vp.zoom(ViewPort::Zoom::IN, sf::Vector2f(sf::Mouse::getPosition(window)));
    }
    else if (delta < 0) {
        vp.zoom(ViewPort::Zoom::OUT, sf::Vector2f(sf::Mouse::getPosition(window)));
    }
}

void Graphics::grab_view() {
    static const std::optional grabbed_cursor = 
            sf::Cursor::createFromSystem(sf::Cursor::Type::Cross);
    if (grabbed_cursor) {
        window.setMouseCursor(*grabbed_cursor);
    }
    else {
        Log::warning("Failed to create cross cursor");
    }
    opt_view_grabbed_pos = sf::Mouse::getPosition(window);
}

void Graphics::release_view() {
    static const std::optional def_cursor = 
            sf::Cursor::createFromSystem(sf::Cursor::Type::Arrow);
    if (def_cursor) {
        window.setMouseCursor(*def_cursor);
    }
    else {
        Log::warning("Failed to create default cursor");
    }
    opt_view_grabbed_pos = std::nullopt;
}

void Graphics::set_grid(bool enabled) {
    grid_enabled = enabled;
}

void Graphics::draw_frame() {
    window.clear(Constants::BG_COLOR);
    pan_if_view_grabbed();
    if (grid_enabled)
        draw_grid();
    draw_bodies();
    window.display();
}   
