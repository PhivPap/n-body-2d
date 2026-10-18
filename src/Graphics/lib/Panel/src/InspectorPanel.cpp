#include "Panel/InspectorPanel.hpp"

#include <cmath>

#include "Logger/Distance.hpp"
#include "Logger/Time.hpp"


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

// What the inspector shows of a single body's orbit
std::string orbit_text(const InspectorDisplayedData::Orbit& o) {
    std::string txt = fmt::format(
        " Orbits:        {}\n"
        " Distance:      {}\n"
        " Rel. Speed:    {}\n",
        attractor(o.attractor, o.inner_count),
        distance(o.distance_m),
        speed(o.relative_speed_m_s)
    );
    if (o.semi_major_axis_m) {
        txt += fmt::format(" Semi-major:    {}\n", distance(*o.semi_major_axis_m));
    }
    txt += o.eccentricity < 10.0 ? fmt::format(" Eccentricity:  {:.5f}\n", o.eccentricity)
                                 : fmt::format(" Eccentricity:  {:.4g}\n", o.eccentricity);
    if (o.period_s) {
        txt += fmt::format(" Period:        {}\n", time(*o.period_s));
    }
    // On one line: under the commands, with the action log below, the panel fits 900 px
    txt += o.apoapsis_m ? fmt::format(" Pe / Ap:       {} / {}\n", distance(o.periapsis_m),
                                  distance(*o.apoapsis_m))
                        : fmt::format(" Periapsis:     {}\n", distance(o.periapsis_m));
    if (o.excess_speed_m_s) {
        txt += fmt::format(" Excess Speed:  {}\n", speed(*o.excess_speed_m_s));
    }
    txt += fmt::format(
        " Next Pe in:    {}\n"
        " Direction:     {}{}\n",
        o.time_to_periapsis_s ? time(*o.time_to_periapsis_s) : "passed",
        o.clockwise ? "clockwise" : "counter-clockwise",
        o.retrograde ? " (retrograde)" : ""
    );
    return txt;
}
}  // namespace


InspectorPanel::InspectorPanel(uint32_t width) : Base(width) {}

void InspectorPanel::set_panel_text() {
    const auto& d = displayed_data;
    std::string txt = "Inspector:\n";
    if (d.count == 0) {
        txt += " Right-click a body, or right-drag a box\n"
               " around bodies, to inspect them\n";
    }
    else if (d.count > 1) {
        txt += fmt::format(
            " Selected:      {} bodies\n"
            " Heaviest:      {}\n"
            " Total Mass:    {}\n"
            " CoM:           ({}, {})\n"
            " CoM Velocity:  ({}, {})\n",
            d.count,
            d.body,
            mass(d.mass_kg),
            distance(d.position_m.x),
            distance(d.position_m.y),
            speed(d.velocity_m_s.x),
            speed(d.velocity_m_s.y)
        );
    }
    else {
        // Which orbit along with it
        std::string satellites;
        if (const uint32_t n = d.orbit ? d.orbit->satellite_count : 0; n > 0) {
            satellites = fmt::format(" (+{} satellite{})", n, n == 1 ? "" : "s");
        }
        txt += fmt::format(
            " Body:          {}{}\n"
            " Mass:          {}\n"
            " Position:      ({}, {})\n"
            " Velocity:      ({}, {})\n",
            d.body,
            satellites,
            mass(d.mass_kg),
            distance(d.position_m.x),
            distance(d.position_m.y),
            speed(d.velocity_m_s.x),
            speed(d.velocity_m_s.y)
        );
        txt += d.orbit ? orbit_text(*d.orbit) : " Orbits:        none\n";
    }
    text.setString(Text::from_utf8(txt));
}
