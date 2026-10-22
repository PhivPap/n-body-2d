#pragma once

#include <utility>
#include <vector>

#include "Panel.hpp"
#include "Ui/Layout.hpp"

class PanelManager {
public:
    using Position = Ui::Corner;

    PanelManager() = default;
    PanelManager(const PanelManager&) = delete;
    PanelManager& operator=(const PanelManager&) = delete;
    // Panels are stacked in their corners in the order they were registered
    void register_panel(Panel* panel, Position position);
    // Draws the shown panels that fit in the window this frame, placed by their sizes in the last
    // frame they were drawn
    void draw(sf::Vector2f window_size);

private:
    std::vector<std::pair<Panel*, Position>> panels;
    std::vector<Ui::PanelSlot> slots;
};
