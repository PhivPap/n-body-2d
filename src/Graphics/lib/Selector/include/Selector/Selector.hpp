#pragma once

#include <optional>
#include <vector>

#include "Body/Body.hpp"
#include "SFML/Graphics.hpp"


class Selector {
public:
    struct SelectionStats {
        uint32_t n;
        double total_mass;
        sf::Vector2<double> center_of_mass;     // the plain mean position when total_mass == 0
        sf::Vector2<double> weighted_velocity;  // the plain mean velocity when total_mass == 0
        uint32_t heaviest;                      // the most massive selected body (first on ties)
    };

    Selector() = delete;
    Selector(const Bodies& bodies, const std::vector<sf::Vector2<double>>& body_positions_cache, 
            sf::VertexArray& body_vertex_array);
    void select(const sf::Rect<float>& region);
    void select_body(uint32_t index);  // replaces the selection with one body
    void set_show(bool show);
    bool has_selection() const;
    uint32_t num_selected() const;
    std::optional<uint32_t> selected_body() const;  // the selection's only body
    const std::vector<uint8_t>& selection_mask() const;  // per body, 1 = selected
    SelectionStats compute_stats() const;  // of a selection that isn't empty

private:
    const Bodies& bodies;
    const std::vector<sf::Vector2<double>>& body_positions_cache;
    sf::VertexArray& body_vertex_array;
    std::vector<uint32_t> selected_body_indices;
    std::vector<uint8_t> mask;
    bool show = true;  // selected bodies get SELECT_COLOR
};
