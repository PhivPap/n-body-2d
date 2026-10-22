#define IMGUI_DEFINE_MATH_OPERATORS
#include "Ui/Widgets.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "Constants/Constants.hpp"
#include "Ui/Ui.hpp"


namespace Ui {
namespace {
// How far below a line's top text in `font` starts, to sit on the text font's baseline
float baseline_offset(const ImFont* font) {
    return std::max(0.f, fonts().text->Ascent - font->Ascent);
}

float text_width(ImFont* font, std::string_view str) {
    return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, str.data(), str.data() + str.size())
            .x;
}

bool is_separator(std::string_view token) {
    return token == "+" || token == "/" || token == "–";
}

// A keycap's size: its key's, padded, and at least square
ImVec2 keycap_size(std::string_view key) {
    const float height = fonts().mono_small->FontSize + 2.f;
    return {std::max(height, std::round(text_width(fonts().mono_small, key))
                                     + 2.f * Constants::Ui::KEYCAP_PADDING_PX),
            height};
}

// Calls `f` with each space-separated token of `spec`
template <typename F>
void for_each_token(std::string_view spec, F&& f) {
    size_t begin = 0;
    while (begin < spec.size()) {
        const size_t end = std::min(spec.find(' ', begin), spec.size());
        if (end > begin) {
            f(spec.substr(begin, end - begin));
        }
        begin = end + 1;
    }
}
}  // namespace


ImU32 u32(sf::Color color) {
    return IM_COL32(color.r, color.g, color.b, color.a);
}

ImU32 draw_color(sf::Color color) {
    return ImGui::GetColorU32(u32(color));
}

void text(std::string_view str, ImFont* font, sf::Color color) {
    const float offset = baseline_offset(font);
    if (offset == 0.f) {  // which wraps if asked to
        ImGui::PushFont(font);
        ImGui::PushStyleColor(ImGuiCol_Text, u32(color));
        ImGui::TextUnformatted(str.data(), str.data() + str.size());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        return;
    }
    // Lower, in an item that starts at the line's top, where what follows on the line starts too
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(font, font->FontSize, pos + ImVec2(0.f, offset),
            draw_color(color), str.data(), str.data() + str.size());
    ImGui::Dummy(
            {text_width(font, str), std::max(fonts().text->FontSize, offset + font->FontSize)});
}

void text_right(std::string_view str, ImFont* font, sf::Color color) {
    ImGui::SetCursorPosX(std::round(
            ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - text_width(font, str)));
    text(str, font, color);
}

void keycap(std::string_view key) {
    using namespace Constants::Ui;
    ImFont* font = fonts().mono_small;
    const float line_height = fonts().text->FontSize;
    const ImVec2 size = keycap_size(key);
    // Centred on the line, and its key in it
    const ImVec2 pos = ImGui::GetCursorScreenPos()
                       + ImVec2(0.f, std::round((line_height - size.y) / 2.f));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(pos, pos + size, draw_color(KEYCAP_FILL), KEYCAP_ROUNDING);
    draw_list->AddRect(pos, pos + size, draw_color(KEYCAP_BORDER), KEYCAP_ROUNDING);
    const ImVec2 text_pos(std::round((size.x - text_width(font, key)) / 2.f),
            std::round((size.y - font->FontSize) / 2.f));
    draw_list->AddText(font, font->FontSize, pos + text_pos, ImGui::GetColorU32(ImGuiCol_Text),
            key.data(), key.data() + key.size());
    ImGui::Dummy({size.x, line_height});
}

void keys(std::string_view spec) {
    bool first = true;
    for_each_token(spec, [&first](std::string_view token) {
        if (!first) {
            ImGui::SameLine(0.f, Constants::Ui::KEYS_SPACING_PX);
        }
        first = false;
        if (is_separator(token)) {
            text(token, fonts().mono_small, Constants::Ui::DIM_TEXT);
        }
        else {
            keycap(token);
        }
    });
}

void hint_right(std::string_view hint) {
    ImGui::SameLine();
    text_right(hint, fonts().mono_small, Constants::Ui::DIM_TEXT);
}

std::pair<ImVec2, ImVec2> capitals_box(float width) {
    const ImFont* font = fonts().text;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy({width, font->FontSize});
    constexpr float CAPITALS_HEIGHT = 0.62f;  // Ubuntu's, of its size
    return {{pos.x, std::round(pos.y + font->Ascent - CAPITALS_HEIGHT * font->FontSize)},
            {pos.x + width, pos.y + font->Ascent}};
}

void on_off(bool on) {
    using namespace Constants::Ui;
    const auto [min, max] = capitals_box(2.f * DOT_RADIUS_PX);
    const ImVec2 centre(min.x + DOT_RADIUS_PX, std::round((min.y + max.y) / 2.f));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (on) {
        draw_list->AddCircleFilled(centre, DOT_RADIUS_PX, draw_color(ON_COLOR));
    }
    else {
        draw_list->AddCircle(centre, DOT_RADIUS_PX - 0.5f, draw_color(DIM_TEXT), 0, 1.f);
    }
    ImGui::SameLine(0.f, DOT_GAP_PX);
    ImGui::PushStyleColor(ImGuiCol_Text, u32(on ? TEXT : DIM_TEXT));
    ImGui::TextUnformatted(on ? "On" : "Off");
    ImGui::PopStyleColor();
}

bool begin_rows(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void end_rows() {
    ImGui::EndTable();
}

void row(std::string_view label, std::string_view value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    text(label, fonts().text, Constants::Ui::DIM_TEXT);
    ImGui::TableNextColumn();
    text(value, fonts().mono, Constants::Ui::TEXT);
}

void flag_row(std::string_view label, bool on) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    text(label, fonts().text, Constants::Ui::DIM_TEXT);
    ImGui::TableNextColumn();
    on_off(on);
}

void gap_row() {
    ImGui::TableNextRow(ImGuiTableRowFlags_None, Constants::Ui::ROW_GAP_PX);
}
}  // namespace Ui
