#include "Panel/Panel.hpp"

#include <cmath>

#include "Constants/Constants.hpp"
#include "Ui/Ui.hpp"
#include "Ui/Widgets.hpp"
#include "imgui_internal.h"


Panel::Panel(std::string title, std::string key, float width)
        : title(std::move(title)), key(std::move(key)), window_id("##panel " + this->key),
          width(width) {}

bool Panel::is_visible() const {
    return visible;
}

void Panel::set_visible(bool visible) {
    this->visible = visible;
}

bool Panel::is_shown() const {
    return visible;
}

sf::Vector2f Panel::get_size() const {
    return size;
}

std::string_view Panel::subtitle() const {
    return {};
}

float Panel::alpha() const {
    return 1.f;
}

void Panel::draw(sf::Vector2f position) {
    ImGui::SetNextWindowPos({std::round(position.x), std::round(position.y)});
    ImGui::SetNextWindowSize({width, 0.f});  // as high as its contents
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha());
    if (ImGui::Begin(window_id.c_str(), nullptr, Ui::OVERLAY_FLAGS)) {
        // Under the other windows (the hub, the tooltip, the intro card), even those created
        // before it
        ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
        if (!title.empty()) {
            Ui::text(title, Ui::fonts().title, Constants::Ui::TEXT);
            if (const std::string_view sub = subtitle(); !sub.empty()) {
                ImGui::SameLine();
                Ui::text(sub, Ui::fonts().text, Constants::Ui::TEXT);
            }
            Ui::hint_right(key);
            ImGui::Separator();
        }
        draw_contents();
    }
    size = ImGui::GetWindowSize();
    ImGui::End();
    ImGui::PopStyleVar();
}

