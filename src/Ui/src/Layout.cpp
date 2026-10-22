#include "Ui/Layout.hpp"

#include <algorithm>

#include "Constants/Constants.hpp"


namespace Ui {
std::vector<std::optional<sf::Vector2f>> place_panels(sf::Vector2f window_size,
        std::span<const PanelSlot> panels) {
    constexpr float MARGIN = Constants::Ui::PANEL_MARGIN_PX;
    // How far each corner's stack reaches into the window
    sf::Vector2f top_left_limit = {0.f, 0.f};
    sf::Vector2f bottom_right_limit = window_size;
    sf::Vector2f top_right_limit = {window_size.x, 0.f};
    sf::Vector2f bottom_left_limit = {0.f, window_size.y};
    std::vector<std::optional<sf::Vector2f>> positions(panels.size());
    for (size_t i = 0; i < panels.size(); i++) {
        const auto [corner, size, visible] = panels[i];
        if (!visible) {
            continue;
        }
        sf::Vector2f offset;
        switch (corner) {
        case Corner::TOP_LEFT: {
            offset = sf::Vector2f(MARGIN, MARGIN + top_left_limit.y);
            const auto new_tl_limit = sf::Vector2f{
                    std::max(top_left_limit.x, size.x + MARGIN), offset.y + size.y};

            if (new_tl_limit.x > top_right_limit.x || new_tl_limit.y > bottom_left_limit.y
                    || (new_tl_limit.x > bottom_right_limit.x
                            && new_tl_limit.y > bottom_right_limit.y)) {
                continue;
            }

            top_left_limit = new_tl_limit;
            break;
        }

        case Corner::TOP_RIGHT: {
            offset = sf::Vector2f(window_size.x - size.x - MARGIN, MARGIN + top_right_limit.y);
            const auto new_tr_limit = sf::Vector2f{
                    std::min(top_right_limit.x, offset.x - MARGIN), offset.y + size.y};

            if (new_tr_limit.x < top_left_limit.x || new_tr_limit.y > bottom_right_limit.y
                    || (new_tr_limit.x < bottom_left_limit.x
                            && new_tr_limit.y > bottom_left_limit.y)) {
                continue;
            }

            top_right_limit = new_tr_limit;
            break;
        }

        case Corner::BOTTOM_LEFT: {
            offset = sf::Vector2f(MARGIN, bottom_left_limit.y - size.y - MARGIN);
            const auto new_bl_limit = sf::Vector2f{
                    std::max(bottom_left_limit.x, size.x + MARGIN), offset.y - MARGIN};

            if (new_bl_limit.x > bottom_right_limit.x || new_bl_limit.y < top_left_limit.y
                    || (new_bl_limit.x > top_right_limit.x && new_bl_limit.y < top_right_limit.y)) {
                continue;
            }

            bottom_left_limit = new_bl_limit;
            break;
        }

        case Corner::BOTTOM_RIGHT: {
            offset = sf::Vector2f(window_size.x - size.x - MARGIN,
                    bottom_right_limit.y - size.y - MARGIN);
            const auto new_br_limit = sf::Vector2f{
                    std::min(bottom_right_limit.x, offset.x - MARGIN), offset.y - MARGIN};

            if (new_br_limit.x < bottom_left_limit.x || new_br_limit.y < top_right_limit.y
                    || (new_br_limit.x < top_left_limit.x && new_br_limit.y < top_left_limit.y)) {
                continue;
            }

            bottom_right_limit = new_br_limit;
            break;
        }
        }
        positions[i] = offset;
    }
    return positions;
}
}  // namespace Ui
