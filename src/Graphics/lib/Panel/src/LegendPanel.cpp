#include "Panel/LegendPanel.hpp"


namespace {
constexpr uint32_t NAME_MAX_CHARS = 14;
constexpr uint32_t SWATCH_CHARS = 2;  // placeholder spaces
constexpr uint32_t GRADIENT_CHARS = 12;

// "296,297"
std::string with_thousands(uint32_t n) {
    std::string digits = std::to_string(n);
    for (size_t k = digits.size(); k > 3; k -= 3) {
        digits.insert(k - 3, ",");
    }
    return digits;
}

// A white point in bodies/px: "6.79", "401", "1016"
std::string white_text(float white) {
    return white < 999.5f ? fmt::format("{:.3g}", white) : fmt::format("{:.0f}", white);
}

// The box of `chars` characters from character `index` of `text`, from the top of a capital
// letter to the baseline, a pixel short of the next character
sf::FloatRect cell(const sf::Text& text, size_t index, uint32_t chars) {
    const sf::Font& font = text.getFont();
    const uint32_t size = text.getCharacterSize();
    const sf::FloatRect capital = font.getGlyph(U'M', size, false).bounds;
    const float advance = font.getGlyph(U' ', size, false).advance;
    const sf::Vector2f top_left = text.findCharacterPos(index);
    return {{top_left.x, top_left.y + static_cast<float>(size) + capital.position.y},
            {advance * static_cast<float>(chars) - 1.f, capital.size.y}};
}
}  // namespace


LegendPanel::LegendPanel(uint32_t width) : Base(width) {}

void LegendPanel::set_panel_text() {
    const auto& d = displayed_data;
    sf::String txt = Text::from_utf8(fmt::format("Colours: {} (V)\n", d.mode));
    swatch_indices.clear();
    for (size_t k = 0; k < d.rows.size(); k++) {
        const LegendDisplayedData::Row& row = d.rows[k];
        sf::String name = Text::from_utf8(row.name);
        if (name.getSize() > NAME_MAX_CHARS) {
            name = name.substring(0, NAME_MAX_CHARS - 3) + "...";
        }
        name += std::string(NAME_MAX_CHARS - name.getSize(), ' ');
        txt += fmt::format(" {} ", k + 1);
        swatch_indices.push_back(txt.getSize());
        txt += std::string(SWATCH_CHARS + 1, ' ') + name
               + fmt::format(" {:>9}{}\n", with_thousands(row.count), row.hidden ? " hidden" : "");
    }
    gradient_index.reset();
    if (!d.gradient.empty()) {
        txt += " ";
        gradient_index = txt.getSize();
        txt += std::string(GRADIENT_CHARS + 1, ' ') + Text::from_utf8(d.scale) + "\n";
    }
    txt += d.locked_white ? fmt::format(" Exposure: Locked at {}/px {:+.1f} EV\n",
                                    white_text(*d.locked_white), d.exposure_ev)
                          : fmt::format(" Exposure: Auto {:+.1f} EV (E [ ])\n", d.exposure_ev);
    txt += fmt::format(" Stretch: {} (Ctrl Up/Down)\n", d.stretch);
    txt += fmt::format(" Bloom: {} (B)\n", d.bloom ? "ON" : "OFF");
    if (!d.rows.empty()) {
        txt += " 1-9 toggle, Shift solo, 0 all\n";
    }
    text.setString(txt);
}

void LegendPanel::draw_decorations(sf::RenderTexture& target) {
    const auto& d = displayed_data;
    for (size_t k = 0; k < swatch_indices.size(); k++) {
        // Filled, or hollow while hidden
        const sf::FloatRect box = cell(text, swatch_indices[k], SWATCH_CHARS);
        sf::RectangleShape swatch(box.size - sf::Vector2f(2.f, 2.f));
        swatch.setPosition(box.position + sf::Vector2f(1.f, 1.f));
        swatch.setFillColor(d.rows[k].hidden ? sf::Color::Transparent : d.rows[k].swatch);
        swatch.setOutlineColor(d.rows[k].swatch);
        swatch.setOutlineThickness(1.f);
        target.draw(swatch);
    }
    if (gradient_index && d.gradient.size() >= 2) {
        const sf::FloatRect box = cell(text, *gradient_index, GRADIENT_CHARS);
        sf::VertexArray bar(sf::PrimitiveType::TriangleStrip);
        for (size_t k = 0; k < d.gradient.size(); k++) {
            const float x = box.position.x
                            + box.size.x * static_cast<float>(k)
                                      / static_cast<float>(d.gradient.size() - 1);
            bar.append({{x, box.position.y}, d.gradient[k]});
            bar.append({{x, box.position.y + box.size.y}, d.gradient[k]});
        }
        target.draw(bar);
    }
}
