#include "Panel/CommandsPanel.hpp"


CommandsPanel::CommandsPanel(uint32_t width) : Base(width) {}

void CommandsPanel::set_panel_text() {
    const auto& d = displayed_data;
    const auto txt = R"(Commands:
 Space:          Pause/Run
 G:              Toggle grid
 F1/F2/F3:       Toggle panels
 Left/Right:     Decrease/Increase timestep
 Up/Down:        Increase/Decrease body size
 Scroll:         Zoom view
 LClick & Drag:  Pan view
 RClick & Drag:  Select bodies
)";
    text.setString(txt);
}
