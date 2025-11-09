#include "Panel/Panel.hpp"

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"



const sf::Font ubuntu_font{"../UbuntuMono-R.ttf"};

Panel::Panel(sf::Vector2u size) : texture(size), sprite(texture.getTexture()), text(ubuntu_font), 
        visible(true) {
    text.setFillColor(sf::Color::White);
    text.setPosition({10.0f, 10.0f});
    text.setCharacterSize(18);
    text.setOutlineColor(sf::Color::White);
    text.setLineSpacing(1.4f);
    texture.setSmooth(true);
    sprite.setScale({1.0f, -1.0f});
    sprite.setPosition({0.0f, size.y});
}

void Panel::set_visible(bool visible) {
    this->visible = visible;
    if (visible) {
        bake();
    }
}

void Panel::update_displayed_data(DisplayedData &&displayed_data) {
    this->displayed_data = std::move(displayed_data);
    if (visible) {
        bake();
    }
}

static std::string seconds_to_readable_time(double sec) {
    int sign = 1;
    if (sec < 0) {
        sign = -1;
        sec = -sec;
    }

    if (sec < 1e-6)
        return fmt::format("{:.3f}ns", sign * sec * 1e+9);
    else if (sec < 1e-3)
        return fmt::format("{:.3f}us", sign * sec * 1e+6);
    else if (sec < 1)
        return fmt::format("{:.3f}ms", sign * sec * 1e+3);
    else if (sec < 60)
        return fmt::format("{:.3f}s", sign * sec);
    else if (sec < 60 * 60)
        return fmt::format("{:.3f}m", sign * sec / 60);
    else if (sec < 60 * 60 * 24)
        return fmt::format("{:.3f}h", sign * sec / (60 * 60));
    else if (sec < 60 * 60 * 24 * 365.2425)
        return fmt::format("{:.3f}days", sign * sec / (60 * 60 * 24));
    return fmt::format("{:.3f}years", sign * sec / (60 * 60 * 24 * 365.2425));
}

static std::string meters_to_readable_distance(double meters) {
    int sign = 1;
    if (meters < 0) {
        sign = -1;
        meters = -meters;
    }

    if (meters < 1e-6) 
        return fmt::format("{:.3f}nm", sign * meters * 1e+9);
    else if (meters < 1e-3) 
        return fmt::format("{:.3f}um", sign * meters * 1e+6);
    else if (meters < 1) 
        return fmt::format("{:.3f}mm", sign * meters * 1e+3);
    else if (meters < 1000) 
        return fmt::format("{:.3f}m", sign * meters);
    else if (meters < 149'597'870'700) 
        return fmt::format("{:.3f}km", sign * meters / 1'000);
    else if (meters < 149'597'870'700)
        return fmt::format("{:.3f}AU", sign * meters / 149'597'870'700);
    return fmt::format("{:.3f}LY", sign * meters / 9'460'730'472'580'800);
}

std::string Panel::DisplayedData::fmt_timestep() const {
    return fmt::format("Time Step:     {}", seconds_to_readable_time(timestep_s));
}

std::string Panel::DisplayedData::fmt_algorithm() const {
    return fmt::format("Algorithm:     {}", algorithm);
}

std::string Panel::DisplayedData::fmt_theta() const {
    return fmt::format("Theta:         {}", theta);
}

std::string Panel::DisplayedData::fmt_threads() const {
    return fmt::format("Threads:       {}", threads);
}

std::string Panel::DisplayedData::fmt_viewport_m() const {
    return fmt::format("View[W x H]:   {} x {}", meters_to_readable_distance(viewport_m.x), 
            meters_to_readable_distance(viewport_m.y));
}

std::string Panel::DisplayedData::fmt_viewport_px() const {
    return fmt::format("Window[W x H]: {}px x {}px", viewport_px.x, viewport_px.y);
}

std::string Panel::DisplayedData::fmt_vsync() const {
    return fmt::format("Vsync:         {}", vsync ? "ON" : "OFF");
}

std::string Panel::DisplayedData::fmt_grid() const {
    return fmt::format("Grid:          {}", grid ? "ON" : "OFF");
}

std::string Panel::DisplayedData::fmt_max_fps() const {
    return fmt::format("Max FPS:       {}", max_fps);
}

std::string Panel::DisplayedData::fmt_iteration() const {
    return fmt::format("Iteration:     {}", iteration);
}

std::string Panel::DisplayedData::fmt_iter_per_sec() const {
    return fmt::format("IPS:           {:.3f}", iter_per_sec);
}

std::string Panel::DisplayedData::fmt_frame() const {
    return fmt::format("Frame:         {}", frame);
}

std::string Panel::DisplayedData::fmt_fps() const {
    return fmt::format("FPS:           {}", fps);
}

std::string Panel::DisplayedData::fmt_elapsed() const {
    return fmt::format("Elapsed:       {}", seconds_to_readable_time(elapsed_s));
}

std::string Panel::DisplayedData::fmt_sim_time() const {
    return fmt::format("Sim. Time:     {}", seconds_to_readable_time(simulated_time_s));
}

std::string Panel::DisplayedData::to_string() {
constexpr const char *fmt_str = 
R"(Configuration:
  {}
  {}
  {}
  {}
  {}
  {}
  {}
  {}
  {}

Stats:
  {}
  {}
  {}
  {}
  {}
  {})";
  return fmt::format(fmt_str, fmt_timestep(), fmt_algorithm(), fmt_theta(), fmt_threads(), 
        fmt_viewport_m(), fmt_viewport_px(), fmt_vsync(), fmt_grid(), fmt_max_fps(), 
        fmt_iteration(), fmt_iter_per_sec(), fmt_frame(), fmt_fps(), fmt_elapsed(), 
        fmt_sim_time());
}


void Panel::bake() {
    texture.clear(sf::Color(40, 40, 40, 128));
    text.setString(displayed_data.to_string());
    texture.draw(text);
}

void Panel::draw(sf::RenderTarget& target, sf::RenderStates states) const {
    if (!visible) {
        return;
    }

    states.transform.combine(getTransform());
    target.draw(sprite, states);
}
