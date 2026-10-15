#include "Panel/StatsPanel.hpp"

#include "Logger/Time.hpp"


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
    text.setString(txt);
}
