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
        if (positions[i]) {
            panels[i].first->draw(*positions[i]);
        }
    }
}
