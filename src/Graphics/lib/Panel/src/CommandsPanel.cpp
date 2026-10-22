#include "Panel/CommandsPanel.hpp"

#include <string_view>

#include "Constants/Constants.hpp"
#include "Ui/Ui.hpp"
#include "Ui/Widgets.hpp"


namespace {
struct Command {
    std::string_view input;  // keys, as Ui::keys() takes them, or what the mouse does
    std::string_view action;
    bool mouse = false;
};

constexpr Command COMMANDS[] = {{"Space", "Pause / run"}, {"G", "Grid"},
        {"S", "Selection highlight"}, {"D", "Selection centre of mass"},
        {"F", "Follow the selection"}, {"C", "Centre on the selection"},
        {"O", "Selection orbit"}, {"A", "Named orbits"}, {"L / M", "Labels / mass sizes"},
        {"F1 – F6", "Panels"}, {"Left / Right", "Timestep − / +"},
        {"Up / Down", "Body size + / −"}, {"Ctrl + Left / Right", "Trails − / +"},
        {"Scroll", "Zoom", true}, {"Left-drag", "Pan", true},
        {"Right-click (drag)", "Select body (bodies)", true}};
}  // namespace


CommandsPanel::CommandsPanel(float width) : Panel("Commands", "F1", width) {}

void CommandsPanel::draw_contents() {
    if (!ImGui::BeginTable("##commands", 2,
                ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
        return;
    }
    ImGui::TableSetupColumn("input", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
    for (const Command& command : COMMANDS) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (command.mouse) {
            Ui::text(command.input, Ui::fonts().mono_small, Constants::Ui::DIM_TEXT);
        }
        else {
            Ui::keys(command.input);
        }
        ImGui::TableNextColumn();
        Ui::text(command.action, Ui::fonts().text, Constants::Ui::TEXT);
    }
    ImGui::EndTable();
}
