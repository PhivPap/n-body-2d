#include <optional>
#include <vector>

#include "Check.hpp"
#include "Ui/Layout.hpp"

using Ui::Corner;
using Position = std::optional<sf::Vector2f>;


namespace {
constexpr sf::Vector2f WINDOW{1600.f, 900.f};

void test_stacking() {
    const std::vector<Ui::PanelSlot> panels = {{Corner::TOP_LEFT, {340.f, 400.f}, true},
            {Corner::TOP_LEFT, {300.f, 200.f}, true}, {Corner::TOP_RIGHT, {370.f, 380.f}, true},
            {Corner::TOP_RIGHT, {430.f, 300.f}, true},
            {Corner::BOTTOM_RIGHT, {370.f, 120.f}, true},
            {Corner::BOTTOM_LEFT, {340.f, 250.f}, true}};
    const std::vector<Position> positions = Ui::place_panels(WINDOW, panels);
    CHECK(positions.size() == panels.size());
    CHECK(positions[0] == sf::Vector2f(10.f, 10.f));
    CHECK(positions[1] == sf::Vector2f(10.f, 420.f));                 // under the first
    CHECK(positions[2] == sf::Vector2f(1600.f - 370.f - 10.f, 10.f));  // right-aligned
    CHECK(positions[3] == sf::Vector2f(1600.f - 430.f - 10.f, 400.f));
    CHECK(positions[4] == sf::Vector2f(1600.f - 370.f - 10.f, 900.f - 120.f - 10.f));
    // Under the top-left stack, which ends at 620, by a margin: 900 - 250 - 10 = 640
    CHECK(positions[5] == sf::Vector2f(10.f, 640.f));
}

void test_bottom_stacks() {
    // Panels stacked from a bottom corner are 2 margins apart
    const std::vector<Ui::PanelSlot> panels = {{Corner::BOTTOM_LEFT, {200.f, 100.f}, true},
            {Corner::BOTTOM_LEFT, {200.f, 100.f}, true},
            {Corner::BOTTOM_RIGHT, {200.f, 50.f}, true}};
    const std::vector<Position> positions = Ui::place_panels(WINDOW, panels);
    CHECK(positions[0] == sf::Vector2f(10.f, 790.f));
    CHECK(positions[1] == sf::Vector2f(10.f, 670.f));
    CHECK(positions[2] == sf::Vector2f(1390.f, 840.f));
}

void test_not_fitting() {
    // The second panel would reach past the bottom-left one: skipped, and the third, smaller,
    // still fits under the first
    const std::vector<Ui::PanelSlot> panels = {{Corner::BOTTOM_LEFT, {340.f, 300.f}, true},
            {Corner::TOP_LEFT, {340.f, 500.f}, true}, {Corner::TOP_LEFT, {340.f, 600.f}, true},
            {Corner::TOP_LEFT, {340.f, 50.f}, true}};
    const std::vector<Position> positions = Ui::place_panels(WINDOW, panels);
    CHECK(positions[0] == sf::Vector2f(10.f, 590.f));
    CHECK(positions[1] == sf::Vector2f(10.f, 10.f));
    CHECK(!positions[2]);
    CHECK(positions[3] == sf::Vector2f(10.f, 520.f));
}

void test_hidden() {
    // A hidden panel takes no room and has no position
    const std::vector<Ui::PanelSlot> panels = {{Corner::TOP_LEFT, {340.f, 400.f}, false},
            {Corner::TOP_LEFT, {340.f, 200.f}, true}, {Corner::TOP_RIGHT, {370.f, 300.f}, false}};
    const std::vector<Position> positions = Ui::place_panels(WINDOW, panels);
    CHECK(!positions[0]);
    CHECK(positions[1] == sf::Vector2f(10.f, 10.f));
    CHECK(!positions[2]);
}

void test_small_window() {
    // Opposite stacks never overlap: in 640 px, the right panel doesn't fit beside the left one,
    // nor does anything wider than the window
    const sf::Vector2f window{640.f, 400.f};
    const std::vector<Ui::PanelSlot> panels = {{Corner::TOP_LEFT, {340.f, 300.f}, true},
            {Corner::TOP_RIGHT, {370.f, 100.f}, true},
            {Corner::BOTTOM_RIGHT, {270.f, 80.f}, true},
            {Corner::BOTTOM_LEFT, {340.f, 100.f}, true}, {Corner::TOP_RIGHT, {700.f, 50.f}, true}};
    const std::vector<Position> positions = Ui::place_panels(window, panels);
    CHECK(positions[0] == sf::Vector2f(10.f, 10.f));
    CHECK(!positions[1]);
    CHECK(positions[2] == sf::Vector2f(360.f, 310.f));  // beside the left stack, 10 px apart
    CHECK(!positions[3]);                                // the left stack reaches 310
    CHECK(!positions[4]);
}
}  // namespace


int main() {
    test_stacking();
    test_bottom_stacks();
    test_not_fitting();
    test_hidden();
    test_small_window();
    return Check::result();
}
