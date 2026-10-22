#pragma once

#include "Panel.hpp"


class CommandsPanel : public Panel {
public:
    CommandsPanel(float width);

private:
    void draw_contents() override;
};
