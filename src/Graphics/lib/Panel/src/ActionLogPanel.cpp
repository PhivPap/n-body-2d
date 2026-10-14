#include "Panel/ActionLogPanel.hpp"


ActionLogPanel::ActionLogPanel(uint32_t width) : Base(width) {}

void ActionLogPanel::set_panel_text() {
    text.setString(sf::String::fromUtf8(displayed_data.begin(), displayed_data.end()));
}
