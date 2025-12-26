#include "Panel/Panel.hpp"

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Distance.hpp"
#include "Logger/Time.hpp"

const sf::Font ubuntu_font{"../UbuntuMono-R.ttf"};

Panel::Panel(sf::Vector2u size) : texture(size), sprite(texture.getTexture()), text(ubuntu_font), 
        visible(true) {
    text.setPosition({5.0f, 10.0f});
    text.setCharacterSize(16);
    text.setLineSpacing(1.4f);
    texture.setSmooth(false);
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

std::string Panel::DisplayedData::fmt_timestep() const {
    return fmt::format("Time Step:     {}", Log::Time::from(timestep_s));
}

std::string Panel::DisplayedData::fmt_algorithm() const {
    return fmt::format("Algorithm:     {}", algorithm);
}

std::string Panel::DisplayedData::fmt_theta() const {
    return fmt::format("Theta:         {}", theta);
}

std::string Panel::DisplayedData::fmt_softening_factor() const {
    return fmt::format("Soft. Factor:  {:.5f}", softening_factor);
}

std::string Panel::DisplayedData::fmt_threads() const {
    return fmt::format("Threads:       {}", threads);
}

std::string Panel::DisplayedData::fmt_viewport_m() const {
    return fmt::format("View[W x H]:   {} x {}", Log::Distance::from(viewport_m.x), 
            Log::Distance::from(viewport_m.y));
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
    return fmt::format("FPS:           {:.3f}", fps);
}

std::string Panel::DisplayedData::fmt_elapsed() const {
    return fmt::format("Elapsed:       {}", Log::Time::from(elapsed_s));
}

std::string Panel::DisplayedData::fmt_sim_time() const {
    return fmt::format("Sim. Time:     {}", Log::Time::from(simulated_time_s));
}

std::string Panel::DisplayedData::fmt_sim_rate() const {
    return fmt::format("Sim. Rate:     {}/s", Log::Time::from(timestep_s * iter_per_sec));
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
 {}

Stats:
 {}
 {}
 {}
 {}
 {}
 {}
 {})";
    return fmt::format(fmt_str, fmt_timestep(), fmt_algorithm(), fmt_theta(), 
            fmt_softening_factor(), fmt_threads(), fmt_viewport_m(), fmt_viewport_px(), 
            fmt_vsync(), fmt_grid(), fmt_max_fps(), fmt_iteration(), fmt_iter_per_sec(), 
            fmt_frame(), fmt_fps(), fmt_elapsed(), fmt_sim_time(), fmt_sim_rate());
}


void Panel::bake() {
    texture.clear(sf::Color(40, 40, 40, 180));
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
