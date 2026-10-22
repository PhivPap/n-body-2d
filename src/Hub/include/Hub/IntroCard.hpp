#pragma once

#include <chrono>
#include <optional>

#include "Hub/Catalogue.hpp"
#include "SFML/System/Vector2.hpp"


namespace Hub {
// A scenario's name, description and tips, from its config's Scenario block, over the scenario
// when it starts. It fades out after a while, and a key or a click dismisses it at once.
class IntroCard {
public:
    // No card without a Scenario block
    explicit IntroCard(std::optional<ScenarioInfo> info);
    void dismiss();
    // Draws it while it shows, over a window of `window_size`, between Ui::Context::new_frame()
    // and render()
    void draw(sf::Vector2u window_size);

private:
    std::optional<ScenarioInfo> info;
    std::chrono::steady_clock::time_point start;
};
}  // namespace Hub
