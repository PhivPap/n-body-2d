#pragma once

#include "Panel.hpp"


// The selection, treated as one body, and its orbit. Values are finite or optional, never NaN,
// which would never compare equal and re-render the panel every frame.
struct InspectorDisplayedData {
    struct Orbit {
        std::string attractor;  // the primary's id
        uint32_t inner_count;
        double distance_m;
        double relative_speed_m_s;
        double eccentricity;
        double periapsis_m;
        std::optional<double> semi_major_axis_m;    // closed orbits
        std::optional<double> apoapsis_m;           // closed orbits
        std::optional<double> period_s;             // closed orbits
        std::optional<double> excess_speed_m_s;     // open orbits
        std::optional<double> time_to_periapsis_s;  // nullopt: moving away on an open orbit
        bool clockwise;                             // on screen
        bool retrograde;

        bool operator==(const Orbit&) const = default;
    };

    std::optional<std::string> body;  // nullopt: nothing selected
    double mass_kg = 0.0;
    double speed_m_s = 0.0;           // absolute, shown when there's no attractor
    std::optional<Orbit> orbit;       // nullopt: no attractor

    bool operator==(const InspectorDisplayedData&) const = default;
};

class InspectorPanel : public PanelBase<InspectorPanel, InspectorDisplayedData> {
public:
    using Base = PanelBase<InspectorPanel, InspectorDisplayedData>;
    InspectorPanel(uint32_t width);
    void set_panel_text();
};
