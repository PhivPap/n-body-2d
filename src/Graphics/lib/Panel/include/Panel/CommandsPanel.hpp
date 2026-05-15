#pragma once

#include "Panel.hpp"


class CommandsPanel : public PanelBase<CommandsPanel, std::monostate> {
public:
    using Base = PanelBase<CommandsPanel, std::monostate>;
    CommandsPanel(sf::Vector2u size);
    void bake_impl();
};
