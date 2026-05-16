#pragma once

#include <vector>

#include "Body/Body.hpp"
#include "SFML/Graphics.hpp"


class Selector {
public:
    struct SelectionStats {
        uint32_t n;
        double total_mass;
        sf::Vector2<double> center_of_mass;
        sf::Vector2<double> weighted_velocity;
    };

    Selector() = delete;
    Selector(const Bodies &bodies, sf::VertexArray &body_vertex_array);
    void select(const sf::Rect<float> &region);
    void clear();
    SelectionStats compute_stats() const;

private:
    const Bodies &bodies;
    sf::VertexArray &body_vertex_array;
    std::vector<uint32_t> selected_body_indices;
};
