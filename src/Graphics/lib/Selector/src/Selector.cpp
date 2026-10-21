#include "Selector/Selector.hpp"

#include "Orbit/Orbit.hpp"
#include "Render/BodyVertex.hpp"


Selector::Selector(const Bodies& bodies, 
        const std::vector<sf::Vector2<double>>& body_positions_cache, 
        sf::VertexArray& body_vertex_array)
        : bodies(bodies), body_positions_cache(body_positions_cache), 
        body_vertex_array(body_vertex_array), mask(bodies.n, 0) {
    assert(body_vertex_array.getVertexCount() == bodies.n);
    selected_body_indices.reserve(bodies.n);
}

void Selector::select(const sf::Rect<float>& region, const std::vector<uint8_t>& group_visible) {
    selected_body_indices.clear();
    for (uint64_t i = 0; i < bodies.n; i++) {
        sf::Vertex& vertex = body_vertex_array[i];
        const bool selected =
                region.contains(vertex.position) && group_visible[BodyVertex::group(vertex)];
        if (selected) {
            selected_body_indices.push_back(i);
        }
        mask[i] = selected;
        BodyVertex::set_highlight(vertex, show && selected);
    }
}

// Only touches the previously selected bodies, unlike select()
void Selector::select_body(uint32_t index) {
    assert(index < bodies.n);
    for (const uint32_t previous : selected_body_indices) {
        mask[previous] = 0;
        BodyVertex::set_highlight(body_vertex_array[previous], false);
    }
    selected_body_indices.assign(1, index);
    mask[index] = 1;
    BodyVertex::set_highlight(body_vertex_array[index], show);
}

void Selector::set_show(bool show) {
    this->show = show;
    for (const uint32_t index : selected_body_indices) {
        BodyVertex::set_highlight(body_vertex_array[index], show);
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
