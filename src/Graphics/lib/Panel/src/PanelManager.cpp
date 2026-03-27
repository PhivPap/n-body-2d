#include "Panel/PanelManager.hpp"

#include "Logger/Logger.hpp"


void PanelManager::register_panel(Panel* panel, Position position) {
    panels.emplace_back(panel, position);
}

void PanelManager::draw(sf::RenderTarget& target, sf::RenderStates states) const {
    const auto window_size = target.getView().getSize();
    sf::Vector2f top_left_limit = {0.f, 0.f};
    sf::Vector2f bottom_right_limit = window_size;
    sf::Vector2f top_right_limit = {window_size.x, 0.f};
    sf::Vector2f bottom_left_limit = {0.f, window_size.y};
    for (const auto [panel, position] : panels) {
        if (!panel->is_visible()) {
            continue;
        }

        sf::Vector2f offset;
        const auto panel_size = panel->get_size();
        switch (position) {
            case Position::TOP_LEFT: {
                offset = sf::Vector2f(PANEL_MARGIN, PANEL_MARGIN + top_left_limit.y);
                const auto new_tl_limit = sf::Vector2f{
                        std::max(top_left_limit.x, panel_size.x + PANEL_MARGIN), 
                        offset.y + panel_size.y};
                
                if (new_tl_limit.x > top_right_limit.x || 
                        new_tl_limit.y > bottom_left_limit.y || 
                        (new_tl_limit.x > bottom_right_limit.x &&
                        new_tl_limit.y > bottom_right_limit.y)) {
                    continue;
                }

                top_left_limit = new_tl_limit;
                break;
            }
            
            case Position::TOP_RIGHT: {
                offset = sf::Vector2f(window_size.x - panel_size.x - PANEL_MARGIN, PANEL_MARGIN + top_right_limit.y);
                const auto new_tr_limit = sf::Vector2f{
                        std::min(top_right_limit.x, offset.x - PANEL_MARGIN),
                        offset.y + panel_size.y};
                
                if (new_tr_limit.x < top_left_limit.x || 
                        new_tr_limit.y > bottom_right_limit.y ||
                        (new_tr_limit.x < bottom_left_limit.x &&
                        new_tr_limit.y > bottom_left_limit.y)) {
                    continue;
                }

                top_right_limit = new_tr_limit;
                break;
            }

            case Position::BOTTOM_LEFT: {
                offset = sf::Vector2f(PANEL_MARGIN, bottom_left_limit.y - panel_size.y - PANEL_MARGIN);
                const auto new_bl_limit = sf::Vector2f{
                        std::max(bottom_left_limit.x, panel_size.x + PANEL_MARGIN),
                        offset.y - PANEL_MARGIN};

                if (new_bl_limit.x > bottom_right_limit.x ||
                        new_bl_limit.y < top_left_limit.y ||
                        (new_bl_limit.x > top_right_limit.x &&
                        new_bl_limit.y < top_right_limit.y)) {
                    continue;
                }

                bottom_left_limit = new_bl_limit;
                break;
            }

            case Position::BOTTOM_RIGHT: {
                offset = sf::Vector2f(window_size.x - panel_size.x - PANEL_MARGIN, bottom_right_limit.y - panel_size.y - PANEL_MARGIN);
                const auto new_br_limit = sf::Vector2f{
                        std::min(bottom_right_limit.x, offset.x - PANEL_MARGIN),
                        offset.y - PANEL_MARGIN};

                if (new_br_limit.x < bottom_left_limit.x || 
                        new_br_limit.y < top_right_limit.y ||
                        (new_br_limit.x < top_left_limit.x &&
                        new_br_limit.y < top_left_limit.y)) {
                    continue;
                }

                bottom_right_limit = new_br_limit;
                break;
            }
        }
        panel->setPosition(offset);
        target.draw(*panel, states);
    }
}
