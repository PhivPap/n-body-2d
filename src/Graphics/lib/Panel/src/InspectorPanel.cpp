#include "Panel/InspectorPanel.hpp"

#include <cmath>

#include "Logger/Distance.hpp"
#include "Logger/Time.hpp"


namespace {
// The values as the panel shows them, "-" if they aren't finite
std::string distance(double m) {
    return std::isfinite(m) ? Log::Distance::from(m).to_string() : "-";
}

std::string speed(double m_s) {
    return std::isfinite(m_s) ? fmt::format("{}/s", Log::Distance::from(m_s)) : "-";
}

std::string time(double s) {
    return std::isfinite(s) ? Log::Time::from(s).to_string() : "-";
}

// What the inspector shows of a single body's orbit
std::string orbit_text(const InspectorDisplayedData::Orbit& o) {
    std::string txt = fmt::format(
        " Orbits:        {}{}\n"
        " Distance:      {}\n"
        " Rel. Speed:    {}\n",
        o.attractor,
        o.inner_count > 0 ? fmt::format(" (+{} inner)", o.inner_count) : "",
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
    txt += fmt::format(" Periapsis:     {}\n", distance(o.periapsis_m));
    if (o.apoapsis_m) {
        txt += fmt::format(" Apoapsis:      {}\n", distance(*o.apoapsis_m));
    }
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
            " Total Mass:    {:.4g}kg\n"
            " CoM:           ({}, {})\n"
            " CoM Velocity:  ({}, {})\n",
            d.count,
            d.body,
            d.mass_kg,
            distance(d.position_m.x),
            distance(d.position_m.y),
            speed(d.velocity_m_s.x),
            speed(d.velocity_m_s.y)
        );
    }
    else {
        txt += fmt::format(
            " Body:          {}\n"
            " Mass:          {:.4g}kg\n"
            " Position:      ({}, {})\n"
            " Velocity:      ({}, {})\n",
            d.body,
            d.mass_kg,
            distance(d.position_m.x),
            distance(d.position_m.y),
            speed(d.velocity_m_s.x),
            speed(d.velocity_m_s.y)
        );
        txt += d.orbit ? orbit_text(*d.orbit) : " Orbits:        none\n";
    }
    text.setString(sf::String::fromUtf8(txt.begin(), txt.end()));
}
