#include "Panel/ActionLogPanel.hpp"

#include <algorithm>

#include "Constants/Constants.hpp"
#include "Ui/Ui.hpp"
#include "Ui/Widgets.hpp"


namespace {
// Messages fade out over their last MESSAGE_FADE_TIME
float message_alpha(const ActionLog::Entry& entry) {
    using Seconds = std::chrono::duration<double>;
    const double left_s = Seconds(ActionLog::ENTRY_DURATION).count() - entry.age_s;
    return static_cast<float>(std::clamp(
            left_s / Seconds(Constants::Ui::MESSAGE_FADE_TIME).count(), 0.0, 1.0));
}
}  // namespace


ActionLogPanel::ActionLogPanel(float width) : DataPanel("", "F4", width) {}

bool ActionLogPanel::is_shown() const {
    return is_visible() && !displayed_data.empty();
}

// The panel fades out with its last message
float ActionLogPanel::alpha() const {
    return displayed_data.empty() ? 1.f : message_alpha(displayed_data.back());
}

void ActionLogPanel::draw_contents() {
    for (const ActionLog::Entry& entry : displayed_data) {
        // Older messages fade first: none is more opaque than the panel
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, message_alpha(entry));
        Ui::text(entry.time, Ui::fonts().mono_small, Constants::Ui::DIM_TEXT);
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0.f);
        Ui::text(entry.text, Ui::fonts().text, Constants::Ui::TEXT);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleVar();
    }
}
