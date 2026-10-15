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

// The heaviest of the named bodies whose marker, grown by HOVER_MARGIN_PX, contains `cursor`;
// among equal masses, the one whose marker edge is nearest to it. `named` lists body indices, most
// massive first, whose markers are sized by marker_diameter() and centred on their vertices.
std::optional<uint32_t> pick_marker(sf::Vector2f cursor, std::span<const uint32_t> named,
        const double* mass, const sf::VertexArray& body_vertices, float base_px,
        bool scale_by_mass);

// A label to place: the marker's centre and radius in window pixels, and the text's size with
// room for a " +" after it
struct LabelBox {
    sf::Vector2f anchor;
    float clearance_px;
    sf::Vector2f size;
};

// Where a label goes: its top-left corner (nullopt: hidden), and whether it hides other labels,
// which a " +" after it shows
struct LabelPlacement {
    std::optional<sf::Vector2f> position;
    bool more = false;
};

// The labels' placements, in the boxes' (priority) order. A label always goes `gap_px` right of
// its marker, centred on it vertically, so labels never jump around. One that would overlap a
// label placed before it is hidden, and the first label it overlaps gets a " +", which the boxes
// leave room for. Labels outside the window are skipped, others may be cut by its edges, and at
// most `max_labels` are placed.
std::vector<LabelPlacement> place_labels(std::span<const LabelBox> boxes,
        sf::Vector2f window_size, uint32_t max_labels, float gap_px);

// The named bodies of a universe: their markers, drawn above the trails and sized by mass, and
// their labels
class NamedBodies {
public:
    explicit NamedBodies(const Bodies& bodies);
    const std::vector<uint32_t>& indices() const;  // most massive first
    bool is_named(uint32_t index) const;
    std::optional<uint32_t> rank(uint32_t index) const;  // a named body's position in indices()
    // A named body's marker diameter in pixels
    float marker_diameter(uint32_t index, float base_px, bool scale_by_mass) const;
    // The heaviest named body under `cursor`, see pick_marker()
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
        float end_x;
    };

    const Bodies& bodies;
    std::vector<uint32_t> named;                     // most massive first
    std::unordered_map<uint32_t, uint32_t> rank_of;  // body index -> position in `named`
    double max_mass = 0.0;
    std::vector<sf::Text> labels;                    // per rank
    std::vector<sf::FloatRect> label_bounds;         // local bounds, computed once
    std::vector<float> label_ends;                   // local x where the text ends, computed once
    sf::Text selected_label;                         // for a selected body that isn't named
    std::optional<uint32_t> selected_label_index;    // whose id it shows
    float selected_label_end = 0.f;
    sf::Text more_label;                             // " +", after a label that hides others
    float more_width = 0.f;                          // from where it starts to the end of the +
    sf::VertexArray markers{sf::PrimitiveType::Points};
    std::vector<LabelCandidate> label_candidates;    // reused every frame
    std::vector<LabelBox> label_boxes;
};
