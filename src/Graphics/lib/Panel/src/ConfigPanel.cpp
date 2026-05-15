#include "Panel/ConfigPanel.hpp"

#include "Logger/Time.hpp"
#include "Logger/Distance.hpp"

ConfigPanel::ConfigPanel(sf::Vector2u size) : Base(size) {}

void ConfigPanel::bake_impl() {
    const auto &d = displayed_data;
    const auto txt = fmt::format(
        "Configuration:\n"
        " Timestep:      {}\n"
        " Algorithm:     {}\n"
        "{}" // Theta only relevant for Barnes-Hut
        " Soft. Factor:  {:.5f}\n"
        "{}" // Threads only relevant for threaded algorithms
        " View[W x H]:   {} x {}\n"
        " Window[W x H]: {}px x {}px\n"
        " Vsync:         {}\n"
        " Grid:          {}\n"
        " Max FPS:       {}",
        Log::Time::from(d.timestep_s),
        d.algorithm,
        displayed_data.show_theta ? fmt::format(" Theta:         {}\n", d.theta) : "",
        d.softening_factor,
        displayed_data.show_threads ? fmt::format(" Threads (sim):  {}\n", d.threads) : "",
        Log::Distance::from(d.viewport_m.x),
        Log::Distance::from(d.viewport_m.y),
        d.viewport_px.x,
        d.viewport_px.y,
        d.vsync ? "ON" : "OFF",
        d.grid  ? "ON" : "OFF",
        d.max_fps
    );
    text.setString(txt);
    texture.draw(text);
}