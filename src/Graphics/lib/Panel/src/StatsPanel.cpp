#include "Panel/StatsPanel.hpp"

#include "Logger/Time.hpp"
#include "Logger/Distance.hpp"


StatsPanel::StatsPanel(uint32_t width) : Base(width) {}

void StatsPanel::set_panel_text() {
    const auto &d = displayed_data;
    auto txt = fmt::format(
        "Stats:\n"
        " Iteration:     {}\n"
        " IPS:           {:.3f}\n"
        " Frame:         {}\n"
        " FPS:           {:.3f}\n"
        " Elapsed:       {}\n"
        " Sim. Time:     {}\n"
        " Sim. Rate:     {}/s\n",
        d.iteration,
        d.iter_per_sec,
        d.frame,
        d.fps,
        Log::Time::from(d.elapsed_s),
        Log::Time::from(d.simulated_time_s),
        Log::Time::from(d.simulation_rate)
    );
    if (d.opt_selection) {
        const auto& sel = d.opt_selection.value();
        txt += fmt::format(
            "Selection:\n"
            " # Selected:    {}\n"
            " Total Mass:    {:.3g}kg\n"
            " CoM:           ({}, {})\n"
            " Weighted Vel.: ({}/s, {}/s)\n",
            sel.num_selected,
            sel.total_mass,
            Log::Distance::from(sel.center_of_mass.x),
            Log::Distance::from(sel.center_of_mass.y),
            Log::Distance::from(sel.weighted_velocity.x),
            Log::Distance::from(sel.weighted_velocity.y)
        );
    }
    text.setString(txt);
}
