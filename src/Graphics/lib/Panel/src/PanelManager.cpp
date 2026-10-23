#include "Panel/PanelManager.hpp"


void PanelManager::register_panel(Panel* panel, Position position) {
    panels.emplace_back(panel, position);
}

void PanelManager::draw(sf::Vector2f window_size) {
    slots.clear();
    for (const auto& [panel, position] : panels) {
        slots.push_back({position, panel->get_size(), panel->is_shown()});
    }
    const auto positions = Ui::place_panels(window_size, slots);
    for (size_t i = 0; i < panels.size(); i++) {
        Panel& panel = *panels[i].first;
        if (positions[i] && panel.get_size() != sf::Vector2f()) {
            panel.draw(*positions[i]);
        }
        // Out of the window, only to be measured: a panel that doesn't fit may fit once its
        // contents shrink, and a new session's panels have no size yet
        else if (panel.is_shown()) {
            panel.draw(window_size);
        }
    }
}
