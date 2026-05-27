#pragma once

#include "Panel.hpp"


class ActionLogPanel : public PanelBase<ActionLogPanel, std::string> {
public:
    using Base = PanelBase<ActionLogPanel, std::string>;
    ActionLogPanel(uint32_t width);
    void set_panel_text();
};
