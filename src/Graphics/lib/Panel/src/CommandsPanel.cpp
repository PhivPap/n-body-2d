#include "Panel/CommandsPanel.hpp"


CommandsPanel::CommandsPanel(uint32_t width) : Base(width) {}

void CommandsPanel::set_panel_text() {
    const auto txt = R"(Commands:
 Space:           Pause/Run
 G:               Toggle grid
 S:               Toggle selection show
 D:               Toggle selection CoM show
 F:               Toggle selection follow
 C:               Center on selection CoM
 O:               Toggle selection orbit
 A:               Toggle named orbits
 L:               Toggle labels
 M:               Toggle mass-scaled markers
 F1-F5:           Toggle panels
 Left/Right:      Decrease/Increase timestep
 Up/Down:         Increase/Decrease body size
 Scroll:          Zoom view
 LClick & Drag:   Pan view
 RClick:          Select body
 RClick & Drag:   Select bodies
 Ctrl Left/Right: Decrease/Increase trails
)";
    text.setString(txt);
}
