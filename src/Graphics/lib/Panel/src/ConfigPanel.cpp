#include "Panel/ConfigPanel.hpp"

#include "Logger/Distance.hpp"
#include "Logger/Time.hpp"


ConfigPanel::ConfigPanel(uint32_t width) : Base(width) {}

void ConfigPanel::set_panel_text() {
    const auto &d = displayed_data;
    const auto txt = fmt::format(
        "Configuration:\n"
        " Timestep:      {}\n"
        " Algorithm:     {}\n"
        "{}" // Theta only relevant for Barnes-Hut
        "{}" // Softening as configured: a length or a factor
        "{}" // Threads only relevant for threaded algorithms
        " View[W x H]:   {} x {}\n"
        " Window[W x H]: {}px x {}px\n"
        " Vsync:         {}\n"
        " Grid:          {}\n"
        " Max FPS:       {}\n"
        " Sel. Show:     {}\n"
        " Sel. Show CoM: {}\n"
        " Sel. Follow:   {}\n"
        " Sel. Orbit:    {}\n"
        " Labels:        {}\n"
        " Mass Sizes:    {}\n",
        Log::Time::from(d.timestep_s),
        d.algorithm,
        displayed_data.show_theta ? fmt::format(" Theta:         {}\n", d.theta) : "",
        d.softening_length_m
                ? fmt::format(" Softening:     {}\n", Log::Distance::from(*d.softening_length_m))
                : fmt::format(" Soft. Factor:  {:.5f}\n", d.softening_factor.value_or(0.0)),
        displayed_data.show_threads ? fmt::format(" Threads (sim):  {}\n", d.threads) : "",
        Log::Distance::from(d.viewport_m.x),
        Log::Distance::from(d.viewport_m.y),
        d.viewport_px.x,
        d.viewport_px.y,
        d.vsync ? "ON" : "OFF",
        d.grid  ? "ON" : "OFF",
        d.max_fps,
        d.selection_show ? "ON" : "OFF",
        d.selection_show_center_of_mass ? "ON" : "OFF",
        d.selection_follow_center_of_mass ? "ON" : "OFF",
        d.selection_show_orbit ? "ON" : "OFF",
        d.show_labels ? "ON" : "OFF",
        d.scale_named_by_mass ? "ON" : "OFF"
    );
    text.setString(txt);
}