#include "Panel/Panel.hpp"

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"



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

enum class Time : uint8_t {
    ns, us, ms, s, m, h, day, year
};

template <Time time>
constexpr double seconds_to(double sec) {
    if constexpr (time == Time::ns) {
        return sec * 1e+9;
    }
    else if constexpr (time == Time::us) {
        return sec * 1e+6;
    }
    else if constexpr (time == Time::ms) {
        return sec * 1e+3;
    }
    else if constexpr (time == Time::s) {
        return sec * 1;
    }
    else if constexpr (time == Time::m) {
        return sec / 60;
    }
    else if constexpr (time == Time::h) {
        return sec / (60 * 60);
    }
    else if constexpr (time == Time::day) {
        return sec / (60 * 60 * 24);
    }
    else {
        static_assert(time == Time::year);
        return sec / (60 * 60 * 24 * 365.2425);
    }
}

template <Time time>
constexpr double seconds_from(double sec) {
    return sec / seconds_to<time>(1.0);
}

static std::string seconds_to_readable_time(double sec) {
    int sign = 1;
    if (sec < 0) {
        sign = -1;
        sec = -sec;
    }

    if (sec < seconds_from<Time::us>(1))
        return fmt::format("{:.3f}ns", sign * seconds_to<Time::ns>(sec));
    else if (sec < seconds_from<Time::ms>(1))
        return fmt::format("{:.3f}us", sign * seconds_to<Time::us>(sec));
    else if (sec < seconds_from<Time::s>(1))
        return fmt::format("{:.3f}ms", sign * seconds_to<Time::ms>(sec));
    else if (sec < seconds_from<Time::m>(1))
        return fmt::format("{:.3f}s", sign * seconds_to<Time::s>(sec));
    else if (sec < seconds_from<Time::h>(1))
        return fmt::format("{:.3f}m", sign * seconds_to<Time::m>(sec));
    else if (sec < seconds_from<Time::day>(1))
        return fmt::format("{:.3f}h", sign * seconds_to<Time::h>(sec));
    else if (sec < seconds_from<Time::year>(1))
        return fmt::format("{:.3f}days", sign * seconds_to<Time::day>(sec));
    else if (sec < seconds_from<Time::year>(1e+5))
        return fmt::format("{:.3f}years", sign * seconds_to<Time::year>(sec));
    return fmt::format("{:.3g}years", sign * seconds_to<Time::year>(sec));
}

enum class Dist : uint8_t {
    nm, um, mm, m, km, AU, LY
};

template <Dist dist>
constexpr double meters_to(double meters) {
    if constexpr (dist == Dist::nm) {
        return meters * 1e+9;
    }
    else if constexpr (dist == Dist::um) {
        return meters * 1e+6;
    }
    else if constexpr (dist == Dist::mm) {
        return meters * 1e+3;
    }
    else if constexpr (dist == Dist::m) {
        return meters * 1;
    }
    else if constexpr (dist == Dist::km) {
        return meters / 1e+3;
    }
    else if constexpr (dist == Dist::AU) {
        return meters / 149'597'870'700;
    }
    else {
        static_assert(dist == Dist::LY);
        return meters / 9'460'730'472'580'800;
    }
}

template <Dist dist>
constexpr double meters_from(double meters) {
    return meters / meters_to<dist>(1.0);
}

static std::string meters_to_readable_distance(double meters) {
    int sign = 1;
    if (meters < 0) {
        sign = -1;
        meters = -meters;
    }

    if (meters < meters_from<Dist::um>(1)) 
        return fmt::format("{:.3f}nm", sign * meters_to<Dist::nm>(meters));
    else if (meters < meters_from<Dist::mm>(1)) 
        return fmt::format("{:.3f}um", sign * meters_to<Dist::um>(meters));
    else if (meters < meters_from<Dist::m>(1)) 
        return fmt::format("{:.3f}mm", sign * meters_to<Dist::mm>(meters));
    else if (meters < meters_from<Dist::km>(1)) 
        return fmt::format("{:.3f}m", sign * meters_to<Dist::m>(meters));
    else if (meters < meters_from<Dist::AU>(1)) 
        return fmt::format("{:.3f}km", sign * meters_to<Dist::km>(meters));
    else if (meters < meters_from<Dist::LY>(1))
        return fmt::format("{:.3f}AU", sign * meters_to<Dist::AU>(meters));
    else if (meters < meters_from<Dist::LY>(1e+5))
        return fmt::format("{:.3f}LY", sign * meters_to<Dist::LY>(meters));
    return fmt::format("{:.3g}LY", sign * meters_to<Dist::LY>(meters));
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
    return fmt::format("FPS:           {:.3f}", fps);
}

std::string Panel::DisplayedData::fmt_elapsed() const {
    return fmt::format("Elapsed:       {}", seconds_to_readable_time(elapsed_s));
}

std::string Panel::DisplayedData::fmt_sim_time() const {
    return fmt::format("Sim. Time:     {}", seconds_to_readable_time(simulated_time_s));
}

std::string Panel::DisplayedData::fmt_sim_rate() const {
    return fmt::format("Sim. Rate:     {}/s", seconds_to_readable_time(timestep_s * iter_per_sec));
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
 {}
 {})";
  return fmt::format(fmt_str, fmt_timestep(), fmt_algorithm(), fmt_theta(), fmt_threads(), 
        fmt_viewport_m(), fmt_viewport_px(), fmt_vsync(), fmt_grid(), fmt_max_fps(), 
        fmt_iteration(), fmt_iter_per_sec(), fmt_frame(), fmt_fps(), fmt_elapsed(), 
        fmt_sim_time(), fmt_sim_rate());
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
