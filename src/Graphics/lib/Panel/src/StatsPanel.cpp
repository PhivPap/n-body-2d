#include "Panel/StatsPanel.hpp"

#include <cmath>

#include "Logger/Time.hpp"
#include "Ui/Widgets.hpp"
#include "fmt/format.h"


StatsPanel::StatsPanel(float width) : DataPanel("Stats", "F3", width) {}

void StatsPanel::draw_contents() {
    const StatsDisplayedData& d = displayed_data;
    const auto time = [](double s) {
        return std::isfinite(s) ? Log::Time::from(s).to_string() : std::string("-");
    };
    if (!Ui::begin_rows("##stats")) {
        return;
    }
    Ui::row("Iteration", PanelFormat::with_thousands(d.iteration));
    Ui::row("Steps/s", PanelFormat::si(d.iter_per_sec));
    Ui::row("Frame", PanelFormat::with_thousands(d.frame));
    Ui::row("FPS", fmt::format("{:.1f}", d.fps));
    Ui::row("Elapsed", time(d.elapsed_s));
    Ui::row("Sim. time", time(d.simulated_time_s));
    Ui::row("Sim. rate", std::isfinite(d.simulation_rate) ? time(d.simulation_rate) + "/s" : "-");
    Ui::end_rows();
}
