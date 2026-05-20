#pragma once

#include "Panel.hpp"


class CommandsPanel : public PanelBase<CommandsPanel, std::monostate> {
public:
    using Base = PanelBase<CommandsPanel, std::monostate>;
    CommandsPanel(uint32_t width);
    void set_panel_text();
};
