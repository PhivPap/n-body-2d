#pragma once

#include <vector>

#include "ActionLog/ActionLog.hpp"
#include "Panel.hpp"


// The action log's messages, without a title. Hidden while there are none.
class ActionLogPanel : public DataPanel<std::vector<ActionLog::Entry>> {
public:
    ActionLogPanel(float width);
    bool is_shown() const override;

private:
    void draw_contents() override;
    float alpha() const override;
};
