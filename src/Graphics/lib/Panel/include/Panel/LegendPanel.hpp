#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Panel.hpp"


// How bodies are coloured: the colour mode, the legend's groups (keys 1-9), the velocity and
// density modes' gradient, the exposure and bloom. Never NaN, which would never compare equal and
// re-render the panel every frame.
struct LegendDisplayedData {
    struct Row {
        std::string name;  // the group's, as UTF-8
        uint32_t count;    // bodies
        sf::Color swatch;  // its colour at full brightness
        bool hidden;

        bool operator==(const Row&) const = default;
    };

    std::string mode;                   // the colour mode's name
    std::vector<Row> rows;
    std::vector<sf::Color> gradient;    // left to right, if the mode has one
    std::string scale;                  // the gradient's, e.g. "0 – 230 km/s"
    double exposure_ev = 0.0;
    std::optional<float> locked_white;  // bodies/px, while the exposure is locked
    bool bloom = false;

    bool operator==(const LegendDisplayedData&) const = default;
};

class LegendPanel : public PanelBase<LegendPanel, LegendDisplayedData> {
public:
    using Base = PanelBase<LegendPanel, LegendDisplayedData>;
    LegendPanel(uint32_t width);
    void set_panel_text();
    void draw_decorations(sf::RenderTexture& target);  // the swatches and the gradient

private:
    // Where in the text the swatches and the gradient go: their first placeholder space
    std::vector<size_t> swatch_indices;
    std::optional<size_t> gradient_index;
};
