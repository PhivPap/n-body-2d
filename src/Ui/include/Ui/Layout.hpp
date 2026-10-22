#pragma once

#include <optional>
#include <span>
#include <vector>

#include "SFML/System/Vector2.hpp"


namespace Ui {
enum class Corner { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT };

// A panel to place in a corner of the window
struct PanelSlot {
    Corner corner;
    sf::Vector2f size;
    bool visible;
};

// The panels' top-left positions, in order: each corner's panels are stacked from it,
// Constants::Ui::PANEL_MARGIN_PX apart and from the window's edges, without overlapping the
// other corners' stacks. Hidden panels take no room, and a panel that doesn't fit is skipped
// (nullopt), as are hidden ones.
std::vector<std::optional<sf::Vector2f>> place_panels(sf::Vector2f window_size,
        std::span<const PanelSlot> panels);
}  // namespace Ui
