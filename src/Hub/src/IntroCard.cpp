#include "Hub/IntroCard.hpp"

#include <algorithm>
#include <cmath>

#include "Constants/Constants.hpp"
#include "Ui/Ui.hpp"
#include "Ui/Widgets.hpp"
#include "imgui_internal.h"


namespace Hub {
IntroCard::IntroCard(std::optional<ScenarioInfo> info)
        : info(std::move(info)), start(std::chrono::steady_clock::now()) {}

void IntroCard::dismiss() {
    info = std::nullopt;
}

void IntroCard::draw(sf::Vector2u window_size) {
    using namespace Constants::Ui;
    if (!info) {
        return;
    }
    // Shown, then fading out
    const std::chrono::duration<float> fading =
            std::chrono::steady_clock::now() - start - INTRO_CARD_TIME;
    const float alpha =
            1.f - std::clamp(fading / std::chrono::duration<float>(INTRO_CARD_FADE_TIME), 0.f, 1.f);
    if (alpha <= 0.f) {
        dismiss();
        return;
    }
    const sf::Vector2f window(window_size);
    ImGui::SetNextWindowPos({std::round(window.x / 2.f), std::round(INTRO_CARD_CENTRE * window.y)},
            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(
            {std::round(std::min(INTRO_CARD_WIDTH_PX, INTRO_CARD_WIDTH_SHARE * window.x)), 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, INTRO_CARD_PADDING);
    if (ImGui::Begin("##intro card", nullptr, Ui::OVERLAY_FLAGS)) {
        // Over the panels, also those first shown after it
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::PushTextWrapPos(0.f);
        Ui::text(info->name, Ui::fonts().card_title, TEXT);
        ImGui::Spacing();
        Ui::text(info->description, Ui::fonts().text, TEXT);
        for (const std::string& tip : info->tips) {
            ImGui::Spacing();
            ImGui::Bullet();
            ImGui::SameLine();
            Ui::text(tip, Ui::fonts().text, TEXT);
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Spacing();
        Ui::text("Esc: scenarios · F1: keys", Ui::fonts().text, DIM_TEXT);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
}  // namespace Hub
