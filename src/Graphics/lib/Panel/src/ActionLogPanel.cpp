#include "Panel/ActionLogPanel.hpp"


ActionLogPanel::ActionLogPanel(uint32_t width) : Base(width) {}

void ActionLogPanel::set_panel_text() {
    text.setString(Text::from_utf8(displayed_data));
}
