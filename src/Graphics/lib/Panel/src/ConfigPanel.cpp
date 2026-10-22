#include "Panel/ConfigPanel.hpp"

#include "Logger/Distance.hpp"
#include "Logger/Time.hpp"
#include "Ui/Widgets.hpp"
#include "fmt/format.h"


namespace {
// A distance without its decimals from 1000 on, which would not fit twice: "2400000km",
// "22.524AU"
std::string short_distance(double m) {
    std::string text = Log::Distance::from(m).to_string();
    const size_t point = text.find('.');
    if (point != std::string::npos && point >= 4 && text.find('e') == std::string::npos) {
        const size_t digits_end = text.find_first_not_of("0123456789", point + 1);
        text.erase(point, digits_end - point);
    }
    return text;
}
}  // namespace


ConfigPanel::ConfigPanel(float width) : DataPanel("Config", "F2", width) {}

void ConfigPanel::draw_contents() {
    const ConfigDisplayedData& d = displayed_data;
    if (!Ui::begin_rows("##config")) {
        return;
    }
    Ui::row("Timestep", Log::Time::from(d.timestep_s).to_string());
    Ui::row("Algorithm", d.algorithm);
    if (d.show_theta) {  // the trees'
        Ui::row("Theta", fmt::format("{}", d.theta));
    }
    // As configured: a length or a factor
    if (d.softening_length_m) {
        Ui::row("Softening", Log::Distance::from(*d.softening_length_m).to_string());
    }
    else {
        Ui::row("Softening factor", fmt::format("{:.5f}", d.softening_factor.value_or(0.0)));
    }
    if (d.show_threads) {  // the CPU tree's
        Ui::row("Threads", fmt::format("{}", d.threads));
    }
    Ui::gap_row();
    Ui::row("View", fmt::format("{} × {}", short_distance(d.viewport_m.x),
                            short_distance(d.viewport_m.y)));
    Ui::row("Window", fmt::format("{} × {} px", d.viewport_px.x, d.viewport_px.y));
    Ui::flag_row("Vsync", d.vsync);
    Ui::row("Max FPS", fmt::format("{}", d.max_fps));
    Ui::flag_row("Grid", d.grid);
    Ui::gap_row();
    Ui::flag_row("Selection", d.selection_show);
    Ui::flag_row("Centre of mass", d.selection_show_center_of_mass);
    Ui::flag_row("Follow", d.selection_follow_center_of_mass);
    Ui::flag_row("Orbit", d.selection_show_orbit);
    Ui::gap_row();
    Ui::flag_row("Labels", d.show_labels);
    Ui::flag_row("Mass sizes", d.scale_named_by_mass);
    Ui::flag_row("Named orbits", d.show_named_orbits);
    Ui::end_rows();
}
