#include "Panel/CommandsPanel.hpp"


CommandsPanel::CommandsPanel(sf::Vector2u size) : Base(size) { bake_impl(); }

void CommandsPanel::bake_impl() {
    const auto &d = displayed_data;
    const auto txt = R"(Commands:
 Space:          Pause/Run
 G:              Toggle grid
 F1/F2/F3:       Toggle panels
 Left/Right:     Decrease/Increase timestep
 Up/Down:        Increase/Decrease body size
 Scroll:         Zoom view
 LClick & Drag:  Pan view
 RClick & Drag:  Select bodies)";
    text.setString(txt);
    texture.draw(text);
}
