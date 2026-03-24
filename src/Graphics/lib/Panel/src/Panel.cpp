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
    sprite.setPosition({0.0f, static_cast<float>(size.y)});
}

void Panel::set_visible(bool visible) {
    this->visible = visible;
    if (visible) {
        bake();
    }
}

Panel::WriteHandle Panel::write_handle() {
    return WriteHandle(this);
}

void Panel::update_displayed_data(DisplayedData &&displayed_data) {
    this->displayed_data = std::move(displayed_data);
    if (visible) {
        bake();
    }
}

std::string Panel::DisplayedData::fmt_timestep() const {
    return fmt::format("{}", Log::Time::from(timestep_s));
}

std::string Panel::DisplayedData::fmt_algorithm() const {
    return fmt::format("{}", algorithm);
}

std::string Panel::DisplayedData::fmt_theta() const {
    return fmt::format("{}", theta);
}

std::string Panel::DisplayedData::fmt_softening_factor() const {
    return fmt::format("{:.5f}", softening_factor);
}

std::string Panel::DisplayedData::fmt_threads() const {
    return fmt::format("{}", threads);
}

std::string Panel::DisplayedData::fmt_viewport_m() const {
    return fmt::format("{} x {}", Log::Distance::from(viewport_m.x), 
            Log::Distance::from(viewport_m.y));
}

std::string Panel::DisplayedData::fmt_viewport_px() const {
    return fmt::format("{}px x {}px", viewport_px.x, viewport_px.y);
}

std::string Panel::DisplayedData::fmt_vsync() const {
    return fmt::format("{}", vsync ? "ON" : "OFF");
}

std::string Panel::DisplayedData::fmt_grid() const {
    return fmt::format("{}", grid ? "ON" : "OFF");
}

std::string Panel::DisplayedData::fmt_max_fps() const {
    return fmt::format("{}", max_fps);
}

std::string Panel::DisplayedData::fmt_iteration() const {
    return fmt::format("{}", iteration);
}

std::string Panel::DisplayedData::fmt_iter_per_sec() const {
    return fmt::format("{:.3f}", iter_per_sec);
}

std::string Panel::DisplayedData::fmt_frame() const {
    return fmt::format("{}", frame);
}

std::string Panel::DisplayedData::fmt_fps() const {
    return fmt::format("{:.3f}", fps);
}

std::string Panel::DisplayedData::fmt_elapsed() const {
    return fmt::format("{}", Log::Time::from(elapsed_s));
}

std::string Panel::DisplayedData::fmt_sim_time() const {
    return fmt::format("{}", Log::Time::from(simulated_time_s));
}

std::string Panel::DisplayedData::fmt_sim_rate() const {
    return fmt::format("{}/s", Log::Time::from(timestep_s * iter_per_sec));
}

std::string Panel::DisplayedData::to_string() {
    std::string txt = "Configuration:\n";
    txt += fmt::format(" Timestep:      {}\n", fmt_timestep());
    txt += fmt::format(" Algorithm:     {}\n", fmt_algorithm());
    if (show_theta) {
        txt += fmt::format(" Theta:         {}\n", fmt_theta());
    }
    txt += fmt::format(" Soft. Factor:  {}\n", fmt_softening_factor());
    if (show_threads) {
        txt += fmt::format(" Threads (sim):  {}\n", fmt_threads());
    }
    txt += fmt::format(" View[W x H]:   {}\n", fmt_viewport_m());
    txt += fmt::format(" Window[W x H]: {}\n", fmt_viewport_px());
    txt += fmt::format(" Vsync:         {}\n", fmt_vsync());
    txt += fmt::format(" Grid:          {}\n", fmt_grid());
    txt += fmt::format(" Max FPS:       {}\n", fmt_max_fps());
    txt += "Stats:\n";
    txt += fmt::format(" Iteration:     {}\n", fmt_iteration());
    txt += fmt::format(" IPS:           {}\n", fmt_iter_per_sec());
    txt += fmt::format(" Frame:         {}\n", fmt_frame());
    txt += fmt::format(" FPS:           {}\n", fmt_fps());
    txt += fmt::format(" Elapsed:       {}\n", fmt_elapsed());
    txt += fmt::format(" Sim. Time:     {}\n", fmt_sim_time());
    txt += fmt::format(" Sim. Rate:     {}\n", fmt_sim_rate());
    return txt;
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
