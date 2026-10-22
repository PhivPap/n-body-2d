#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Panel.hpp"
#include "SFML/Graphics/Color.hpp"


// How bodies are coloured: the colour mode, the legend's groups (keys 1-9), the velocity and
// density modes' gradient, the exposure, stretch and bloom
struct LegendDisplayedData {
    struct Row {
        std::string name;  // the group's, as UTF-8
        uint32_t count;    // bodies
        sf::Color swatch;  // its colour at full brightness
        bool hidden;
    };

    std::string mode;                   // the colour mode's name
    std::vector<Row> rows;
    std::vector<sf::Color> gradient;    // left to right, if the mode has one
    std::string scale;                  // the gradient's, e.g. "0 – 230 km/s"
    double exposure_ev = 0.0;
    std::optional<float> locked_white;  // bodies/px, while the exposure is locked
    std::string stretch;                // e.g. "x4"
    bool bloom = false;
};

class LegendPanel : public DataPanel<LegendDisplayedData> {
public:
    LegendPanel(float width);

private:
    std::string_view subtitle() const override;
    void draw_contents() override;
};
