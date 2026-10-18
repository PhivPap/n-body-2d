#include "Selector/Selector.hpp"

#include "Constants/Constants.hpp"
#include "Orbit/Orbit.hpp"


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

const std::vector<uint8_t>& Selector::selection_mask() const {
    return mask;
}

// The selection as one body, summed relative to its first body, which keeps the centre of mass
// precise far from the origin
Selector::SelectionStats Selector::compute_stats() const {
    const Orbit::Target target = Orbit::barycentre(selected_body_indices, bodies.mass_data(),
            body_positions_cache.data(), bodies.vel_data());
    return SelectionStats{
        .n = static_cast<uint32_t>(selected_body_indices.size()),
        .total_mass = target.mass,
        .center_of_mass = target.pos,
        .weighted_velocity = target.vel,
        .heaviest = target.heaviest
    };
}
