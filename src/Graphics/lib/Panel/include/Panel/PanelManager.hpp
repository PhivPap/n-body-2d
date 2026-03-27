#pragma once

#include "Panel.hpp"

class PanelManager : public sf::Drawable, public sf::Transformable {
public:
    enum class Position {
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT
    };

    PanelManager() = default;
    ~PanelManager() = default;
    void register_panel(Panel* panel, Position position);

private:
    static constexpr float PANEL_MARGIN = 10.f;
    std::vector<std::pair<Panel*, Position>> panels;

    PanelManager(const PanelManager&) = delete;
    PanelManager& operator=(const PanelManager&) = delete;
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;
};
