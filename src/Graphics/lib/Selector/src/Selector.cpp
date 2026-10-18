#include "Selector/Selector.hpp"

#include "Constants/Constants.hpp"


Selector::Selector(const Bodies& bodies, 
        const std::vector<sf::Vector2<double>>& body_positions_cache, 
        sf::VertexArray& body_vertex_array)
        : bodies(bodies), body_positions_cache(body_positions_cache), 
        body_vertex_array(body_vertex_array), mask(bodies.n, 0) {
    assert(body_vertex_array.getVertexCount() == bodies.n);
    selected_body_indices.reserve(bodies.n);
}

void Selector::select(const sf::Rect<float>& region) {
    selected_body_indices.clear();
    for (uint64_t i = 0; i < bodies.n; i++) {
        if (region.contains(body_vertex_array[i].position)) {
            selected_body_indices.push_back(i);
            mask[i] = 1;
            body_vertex_array[i].color =
                    show ? Constants::Graphics::SELECT_COLOR : Constants::Graphics::BODY_COLOR;
        }
        else {
            mask[i] = 0;
            body_vertex_array[i].color = Constants::Graphics::BODY_COLOR;
        }
    }
}

// Only touches the previously selected bodies, unlike select()
void Selector::select_body(uint32_t index) {
    assert(index < bodies.n);
    for (const uint32_t previous : selected_body_indices) {
        mask[previous] = 0;
        body_vertex_array[previous].color = Constants::Graphics::BODY_COLOR;
    }
    selected_body_indices.assign(1, index);
    mask[index] = 1;
    body_vertex_array[index].color =
            show ? Constants::Graphics::SELECT_COLOR : Constants::Graphics::BODY_COLOR;
}

void Selector::set_show(bool show) {
    this->show = show;
    for (const uint32_t index : selected_body_indices) {
        body_vertex_array[index].color =
                show ? Constants::Graphics::SELECT_COLOR : Constants::Graphics::BODY_COLOR;
    }
}

bool Selector::has_selection() const {
    return !selected_body_indices.empty();
}

uint32_t Selector::num_selected() const {
    return static_cast<uint32_t>(selected_body_indices.size());
}

std::optional<uint32_t> Selector::selected_body() const {
    return selected_body_indices.size() == 1 ? std::optional(selected_body_indices.front())
                                             : std::nullopt;
}

bool Selector::is_selected(uint32_t index) const {
    return mask[index] != 0;
}

const std::vector<uint8_t>& Selector::selection_mask() const {
    return mask;
}

Selector::SelectionStats Selector::compute_stats() const {
    double total_mass = 0.0;
    sf::Vector2<double> center_of_mass{0.0, 0.0};
    sf::Vector2<double> weighted_velocity{0.0, 0.0};
    uint32_t heaviest = 0;
    double heaviest_mass = -1.0;

    for (const uint32_t index : selected_body_indices) {
        const double mass = bodies.mass(index);
        total_mass += mass;
        center_of_mass += mass * body_positions_cache[index];
        weighted_velocity += mass * bodies.vel(index);
        if (mass > heaviest_mass) {
            heaviest = index;
            heaviest_mass = mass;
        }
    }

    if (total_mass > 0.0) {
        center_of_mass /= total_mass;
        weighted_velocity /= total_mass;
    }
    else if (!selected_body_indices.empty()) {
        // Massless bodies: plain means
        for (const uint32_t index : selected_body_indices) {
            center_of_mass += body_positions_cache[index];
            weighted_velocity += bodies.vel(index);
        }
        center_of_mass /= static_cast<double>(selected_body_indices.size());
        weighted_velocity /= static_cast<double>(selected_body_indices.size());
    }

    return SelectionStats{
        .n = static_cast<uint32_t>(selected_body_indices.size()),
        .total_mass = total_mass,
        .center_of_mass = center_of_mass,
        .weighted_velocity = weighted_velocity,
        .heaviest = heaviest
    };
}
