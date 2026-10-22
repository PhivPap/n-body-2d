#pragma once

#include <string_view>
#include <utility>

#include "SFML/Graphics/Color.hpp"
#include "imgui.h"


// What the panels, the tooltip and the hub are made of. Text lines are as high as the text font's,
// and text in other fonts sits on its baseline.
namespace Ui {
// Read-only windows that the mouse goes through: the panels and the tooltip
constexpr ImGuiWindowFlags OVERLAY_FLAGS = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs
                                           | ImGuiWindowFlags_NoNav
                                           | ImGuiWindowFlags_NoFocusOnAppearing
                                           | ImGuiWindowFlags_NoBringToFrontOnFocus
                                           | ImGuiWindowFlags_NoSavedSettings;

ImU32 u32(sf::Color color);
// For draw lists: with the style's alpha, which ImGui applies to its own colours
ImU32 draw_color(sf::Color color);

// `str` in `font` and `color`, on the text font's baseline
void text(std::string_view str, ImFont* font, sf::Color color);
// The same, right-aligned in what is left of the line (or of the table's cell)
void text_right(std::string_view str, ImFont* font, sf::Color color);
// A key in a keycap
void keycap(std::string_view key);
// Keys as keycaps, separated by spaces: "+", "/" and "–" between them are drawn as dim text,
// e.g. "Ctrl + Left / Right"
void keys(std::string_view spec);
// A hint, such as the keys that change a setting, in small dim mono right-aligned on the current
// line
void hint_right(std::string_view hint);
// Takes `width` of the current line, and returns the box of its capital letters there (min, max),
// for swatches and the like
std::pair<ImVec2, ImVec2> capitals_box(float width);
// A setting's state: a filled dot and "On", or a hollow dim circle and "Off"
void on_off(bool on);

// Rows of dim labels and values in mono, in a table of two columns: rows between
// begin_rows() and end_rows(), which is only called if begin_rows() returned true
bool begin_rows(const char* id);
void end_rows();
void row(std::string_view label, std::string_view value);
void flag_row(std::string_view label, bool on);
void gap_row();  // a little space between groups of rows
}  // namespace Ui
