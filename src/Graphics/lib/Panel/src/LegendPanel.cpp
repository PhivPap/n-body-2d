#include "Panel/LegendPanel.hpp"

#include "Constants/Constants.hpp"
#include "Ui/Format.hpp"
#include "Ui/Ui.hpp"
#include "Ui/Widgets.hpp"
#include "fmt/format.h"


namespace {
constexpr size_t NAME_MAX_CHARS = 14;
constexpr float SWATCH_WIDTH_PX = 14.f;
constexpr float GRADIENT_WIDTH_PX = 120.f;

// `name`, cut to NAME_MAX_CHARS characters with "…" if it is longer
std::string short_name(std::string_view name) {
    size_t chars = 0;
    size_t cut = name.size();  // where the "…" goes
    for (size_t i = 0; i < name.size(); i++) {
        if ((static_cast<uint8_t>(name[i]) & 0xC0) != 0x80) {  // a character's first byte
            if (chars == NAME_MAX_CHARS - 1) {
                cut = i;
            }
            if (++chars > NAME_MAX_CHARS) {
                return std::string(name.substr(0, cut)) + "…";
            }
        }
    }
    return std::string(name);
}

// A white point in bodies/px: "6.79", "401", "1016"
std::string white_text(float white) {
    return white < 999.5f ? fmt::format("{:.3g}", white) : fmt::format("{:.0f}", white);
}

// Filled, or hollow while hidden
void swatch(sf::Color color, bool hidden) {
    const auto [min, max] = Ui::capitals_box(SWATCH_WIDTH_PX);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (hidden) {
        draw_list->AddRect(min, max, Ui::draw_color(color));
    }
    else {
        draw_list->AddRectFilled(min, max, Ui::draw_color(color));
    }
}

void gradient_bar(const std::vector<sf::Color>& gradient) {
    const auto [min, max] = Ui::capitals_box(GRADIENT_WIDTH_PX);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float step = GRADIENT_WIDTH_PX / static_cast<float>(gradient.size() - 1);
    for (size_t k = 0; k + 1 < gradient.size(); k++) {
        const ImU32 left = Ui::draw_color(gradient[k]);
        const ImU32 right = Ui::draw_color(gradient[k + 1]);
        draw_list->AddRectFilledMultiColor({min.x + step * static_cast<float>(k), min.y},
                {min.x + step * static_cast<float>(k + 1), max.y}, left, right, right, left);
    }
}
}  // namespace


LegendPanel::LegendPanel(float width) : DataPanel("Colours", "V", width) {}

std::string_view LegendPanel::subtitle() const {
    return displayed_data.mode;
}

void LegendPanel::draw_contents() {
    using namespace Constants::Ui;
    const LegendDisplayedData& d = displayed_data;
    if (!d.rows.empty()
            && ImGui::BeginTable("##groups", 5,
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
        // The counts right-aligned in the column that takes the rest of the width: a column that
        // fits its contents would keep the width they were aligned to
        ImGui::TableSetupColumn("swatch", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("hidden", ImGuiTableColumnFlags_WidthFixed);
        for (size_t k = 0; k < d.rows.size(); k++) {
            const LegendDisplayedData::Row& row = d.rows[k];
            const sf::Color color = row.hidden ? DIM_TEXT : TEXT;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            swatch(row.swatch, row.hidden);
            ImGui::TableNextColumn();
            Ui::keycap(std::to_string(k + 1));
            ImGui::TableNextColumn();
            Ui::text(short_name(row.name), Ui::fonts().text, color);
            ImGui::TableNextColumn();
            Ui::text_right(Ui::Format::with_thousands(row.count), Ui::fonts().mono, color);
            ImGui::TableNextColumn();
            if (row.hidden) {
                Ui::text("hidden", Ui::fonts().text, DIM_TEXT);
            }
        }
        ImGui::EndTable();
    }
    if (d.gradient.size() >= 2) {
        gradient_bar(d.gradient);
        ImGui::SameLine();
        Ui::text(d.scale, Ui::fonts().mono, TEXT);
    }
    if (Ui::begin_rows("##settings")) {
        Ui::row("Exposure", d.locked_white ? fmt::format("Locked at {}/px {:+.1f} EV",
                                                     white_text(*d.locked_white), d.exposure_ev)
                                           : fmt::format("Auto {:+.1f} EV", d.exposure_ev));
        Ui::hint_right("E [ ]");
        Ui::row("Stretch", d.stretch);
        Ui::hint_right("Ctrl Up/Down");
        Ui::flag_row("Bloom", d.bloom);
        Ui::hint_right("B");
        Ui::end_rows();
    }
    if (!d.rows.empty()) {
        Ui::text("1–9 toggle · Shift solo · 0 all", Ui::fonts().text, DIM_TEXT);
    }
}
