#include "Panel/ActionLogPanel.hpp"


ActionLogPanel::ActionLogPanel(uint32_t width) : Base(width) {}

void ActionLogPanel::set_panel_text() {
    text.setString(displayed_data);
}
