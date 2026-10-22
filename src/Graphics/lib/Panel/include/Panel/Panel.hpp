#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "SFML/System/Vector2.hpp"


// A read-only panel drawn with Dear ImGui, which the mouse goes through: a title row (its title,
// and its key on the right), then its contents
class Panel {
public:
    // No title row for an empty title. `key` must be unique among the panels.
    Panel(std::string title, std::string key, float width);
    virtual ~Panel() = default;
    bool is_visible() const;
    void set_visible(bool visible);
    // Visible, and with something to show
    virtual bool is_shown() const;
    sf::Vector2f get_size() const;  // when it was last drawn, 0x0 before
    // Draws it this frame, its top-left corner at `position`
    void draw(sf::Vector2f position);

protected:
    virtual void draw_contents() = 0;
    // After the title, if any
    virtual std::string_view subtitle() const;
    // How opaque the panel is, background included
    virtual float alpha() const;

private:
    std::string title;
    std::string key;
    std::string window_id;  // Dear ImGui's
    float width;
    bool visible = true;
    sf::Vector2f size{};
};

// A panel showing a DisplayedData, which its owner fills in through data()
template <typename DisplayedData>
class DataPanel : public Panel {
public:
    using Panel::Panel;
    DisplayedData& data() {
        return displayed_data;
    }

protected:
    DisplayedData displayed_data{};
};

// Numbers as panels show them
namespace PanelFormat {
std::string with_thousands(uint64_t n);  // "296,297"
// Three significant digits, with a k, M or G prefix from a thousand on: "358", "1.31 M"
std::string si(double x);
}  // namespace PanelFormat
