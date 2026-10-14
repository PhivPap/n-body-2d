#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Body/Body.hpp"
#include "SFML/Graphics.hpp"


// The id without a trailing number and one '-' or '_' before it: "ring-12" -> "ring",
// "S55" -> "S", "G1-d123" -> "G1-d", "Kepler-16b" -> "Kepler-16b", "123" -> ""
std::string_view id_group(std::string_view id);

// Bodies whose id group has at most `max_group_size` members, most massive first (ties: lower
// index first), at most `max_count` of them. Ids that share a group number the members of a
// population (ring particles, stars of a galaxy); the others name a body.
std::vector<uint32_t> find_named_bodies(const Bodies& bodies, uint32_t max_group_size,
        uint32_t max_count);

// Marker diameter in pixels: base + 2 for bodies 9 or more orders of magnitude lighter than the
// heaviest named body (or massless, or without scaling), up to base + 12 for the heaviest
float marker_diameter(double mass, double max_mass, float base_px, bool scale_by_mass);

// A label to place: the marker's centre and radius in window pixels, and the text's size
struct LabelBox {
    sf::Vector2f anchor;
    float clearance_px;
    sf::Vector2f size;
};

// Top-left corners of the labels that fit, in the boxes' (priority) order, nullopt for skipped
// ones. A label goes `gap_px` right of, left of, above or below its marker, at the first of these
// spots that lies inside the window and keeps clear of the labels placed before it.
std::vector<std::optional<sf::Vector2f>> place_labels(std::span<const LabelBox> boxes,
        sf::Vector2f window_size, uint32_t max_labels, float gap_px);

// The named bodies of a universe: their markers, drawn above the trails and sized by mass, and
// their labels
class NamedBodies {
public:
    explicit NamedBodies(const Bodies& bodies);
    const std::vector<uint32_t>& indices() const;  // most massive first
    bool is_named(uint32_t index) const;
    // A named body's marker diameter in pixels
    float marker_diameter(uint32_t index, float base_px, bool scale_by_mass) const;
    // The named body whose marker edge is nearest to `cursor`, within HOVER_MARGIN_PX
    std::optional<uint32_t> pick(sf::Vector2f cursor, const sf::VertexArray& body_vertices,
            float base_px, bool scale_by_mass) const;
    // Heaviest first, so that lighter bodies such as moons stay visible on top
    void draw_markers(sf::RenderTarget& target, const sf::Shader& marker_shader,
            const sf::VertexArray& body_vertices, float base_px, bool scale_by_mass,
            const std::vector<uint8_t>& selection_mask, bool selection_show);
    // `selected_body`: the selection's only body, labelled first even if it isn't named
    void draw_labels(sf::RenderTarget& target, const sf::VertexArray& body_vertices,
            float base_px, bool scale_by_mass, std::optional<uint32_t> selected_body,
            const std::vector<uint8_t>& selection_mask, bool selection_show);

private:
    // A label that draw_labels() may place this frame
    struct LabelCandidate {
        uint32_t index;
        sf::Text* text;
        sf::FloatRect bounds;
    };

    const Bodies& bodies;
    std::vector<uint32_t> named;                     // most massive first
    std::unordered_map<uint32_t, uint32_t> rank_of;  // body index -> position in `named`
    double max_mass = 0.0;
    std::vector<sf::Text> labels;                    // per rank
    std::vector<sf::FloatRect> label_bounds;         // local bounds, computed once
    sf::Text selected_label;                         // for a selected body that isn't named
    std::optional<uint32_t> selected_label_index;    // whose id it shows
    sf::VertexArray markers{sf::PrimitiveType::Points};
    std::vector<LabelCandidate> label_candidates;    // reused every frame
    std::vector<LabelBox> label_boxes;
};
