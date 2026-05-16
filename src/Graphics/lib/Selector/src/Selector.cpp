#include "Selector/Selector.hpp"

#include "Constants/Constants.hpp"

Selector::Selector(const Bodies &bodies, sf::VertexArray &body_vertex_array) : bodies(bodies), 
        body_vertex_array(body_vertex_array) {
    assert(body_vertex_array.getVertexCount() == bodies.n);
    selected_body_indices.reserve(bodies.n);
}

void Selector::select(const sf::Rect<float> &region) {
    selected_body_indices.clear();
    for (uint64_t i = 0; i < bodies.n; i++) {
        if (region.contains(body_vertex_array[i].position)) {
            selected_body_indices.push_back(i);
            body_vertex_array[i].color = Constants::Graphics::SELECT_COLOR;
        }
        else {
            body_vertex_array[i].color = Constants::Graphics::BODY_COLOR;
        }
    }
}

void Selector::clear() {
    for (const uint32_t index : selected_body_indices) {
        body_vertex_array[index].color = Constants::Graphics::BODY_COLOR;
    }
    selected_body_indices.clear();
}

Selector::SelectionStats Selector::compute_stats() const {
    double total_mass = 0.0;
    sf::Vector2<double> center_of_mass{0.0, 0.0};
    sf::Vector2<double> weighted_velocity{0.0, 0.0};

    for (const uint32_t index : selected_body_indices) {
        const double mass = bodies.mass(index);
        total_mass += mass;
        center_of_mass += mass * bodies.pos(index);
        weighted_velocity += mass * bodies.vel(index);
    }

    if (total_mass > 0.0) {
        center_of_mass /= total_mass;
        weighted_velocity /= total_mass;
    }
    
    return SelectionStats{
        .n = static_cast<uint32_t>(selected_body_indices.size()),
        .total_mass = total_mass,
        .center_of_mass = center_of_mass,
        .weighted_velocity = weighted_velocity
    };
}
