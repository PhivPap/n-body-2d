#pragma once

#include "Panel.hpp"


// The selection: a single body and its orbit, or several bodies as a whole. Values are never NaN,
// which would never compare equal and re-render the panel every frame; others that aren't finite
// show as "-".
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

    uint32_t count = 0;                  // selected bodies, 0 for none
    std::string body;                    // the only body's id, or the heaviest's
    double mass_kg = 0.0;                // in all
    sf::Vector2<double> position_m{};    // the centre of mass
    sf::Vector2<double> velocity_m_s{};  // mass-weighted
    std::optional<Orbit> orbit;          // a single body's, nullopt without an attractor

    bool operator==(const InspectorDisplayedData&) const = default;
};

class InspectorPanel : public PanelBase<InspectorPanel, InspectorDisplayedData> {
public:
    using Base = PanelBase<InspectorPanel, InspectorDisplayedData>;
    InspectorPanel(uint32_t width);
    void set_panel_text();
};
