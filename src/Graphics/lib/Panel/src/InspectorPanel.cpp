#include "Panel/InspectorPanel.hpp"

#include <cmath>

#include "Constants/Constants.hpp"
#include "Logger/Distance.hpp"
#include "Logger/Time.hpp"
#include "Ui/Ui.hpp"
#include "Ui/Widgets.hpp"


namespace InspectorFormat {
std::string distance(double m) {
    return std::isfinite(m) ? Log::Distance::from(m).to_string() : "-";
}

std::string speed(double m_s) {
    return std::isfinite(m_s) ? fmt::format("{}/s", Log::Distance::from(m_s)) : "-";
}

std::string time(double s) {
    return std::isfinite(s) ? Log::Time::from(s).to_string() : "-";
}

std::string mass(double kg) {
    return std::isfinite(kg) ? fmt::format("{:.4g}kg", kg) : "-";
}

std::string attractor(std::string_view id, uint32_t inner_count) {
    return inner_count > 0 ? fmt::format("{} (+{} inner)", id, inner_count) : std::string(id);
}
}  // namespace InspectorFormat


namespace {
using namespace InspectorFormat;

std::string vector(const sf::Vector2<double>& v, std::string (*format)(double)) {
    return fmt::format("({}, {})", format(v.x), format(v.y));
}

// The rows of a single body's orbit
void orbit_rows(const InspectorDisplayedData::Orbit& o) {
    Ui::row("Orbits", attractor(o.attractor, o.inner_count));
    Ui::row("Distance", distance(o.distance_m));
    Ui::row("Rel. speed", speed(o.relative_speed_m_s));
    if (o.semi_major_axis_m) {
        Ui::row("Semi-major", distance(*o.semi_major_axis_m));
    }
    Ui::row("Eccentricity", o.eccentricity < 10.0 ? fmt::format("{:.5f}", o.eccentricity)
                                                  : fmt::format("{:.4g}", o.eccentricity));
    if (o.period_s) {
        Ui::row("Period", time(*o.period_s));
    }
    // On one row: under the commands, with the messages below, the panel fits 900 px
    if (o.apoapsis_m) {
        Ui::row("Pe / Ap", fmt::format("{} / {}", distance(o.periapsis_m), distance(*o.apoapsis_m)));
    }
    else {
        Ui::row("Periapsis", distance(o.periapsis_m));
    }
    if (o.excess_speed_m_s) {
        Ui::row("Excess speed", speed(*o.excess_speed_m_s));
    }
    Ui::row("Next Pe in", o.time_to_periapsis_s ? time(*o.time_to_periapsis_s) : "passed");
    Ui::row("Direction", fmt::format("{}{}", o.clockwise ? "clockwise" : "counter-clockwise",
                                 o.retrograde ? " (retrograde)" : ""));
}
}  // namespace


InspectorPanel::InspectorPanel(float width) : DataPanel("Inspector", "F5", width) {}

void InspectorPanel::draw_contents() {
    const InspectorDisplayedData& d = displayed_data;
    if (d.count == 0) {
        ImGui::PushTextWrapPos(0.f);
        Ui::text("Right-click a body, or right-drag a box around bodies, to inspect them",
                Ui::fonts().text, Constants::Ui::DIM_TEXT);
        ImGui::PopTextWrapPos();
        return;
    }
    if (!Ui::begin_rows("##inspector")) {
        return;
    }
    if (d.count > 1) {
        Ui::row("Selected", fmt::format("{} bodies", d.count));
        Ui::row("Heaviest", d.body);
        Ui::row("Total mass", mass(d.mass_kg));
        Ui::row("CoM", vector(d.position_m, distance));
        Ui::row("CoM velocity", vector(d.velocity_m_s, speed));
    }
    else {
        // Which orbit along with it
        std::string satellites;
        if (const uint32_t n = d.orbit ? d.orbit->satellite_count : 0; n > 0) {
            satellites = fmt::format(" (+{} satellite{})", n, n == 1 ? "" : "s");
        }
        Ui::row("Body", d.body + satellites);
        Ui::row("Mass", mass(d.mass_kg));
        Ui::row("Position", vector(d.position_m, distance));
        Ui::row("Velocity", vector(d.velocity_m_s, speed));
        if (d.orbit) {
            orbit_rows(*d.orbit);
        }
        else {
            Ui::row("Orbits", "none");
        }
    }
    Ui::end_rows();
}
