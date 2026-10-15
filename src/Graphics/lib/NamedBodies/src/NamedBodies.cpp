#include "NamedBodies/NamedBodies.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "AssetManager/AssetManager.hpp"
#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"
#include "StopWatch/StopWatch.hpp"


namespace {
// An id as a label: decoded as UTF-8, and cut to LABEL_MAX_CHARS code points
sf::String label_string(const std::string& id) {
    using Constants::Graphics::LABEL_MAX_CHARS;
    sf::String label = sf::String::fromUtf8(id.begin(), id.end());
    if (label.getSize() > LABEL_MAX_CHARS) {
        label = label.substring(0, LABEL_MAX_CHARS - 3) + "...";
    }
    return label;
}

sf::Text make_label(const std::string& id) {
    using namespace Constants::Graphics;
    sf::Text text(AssetManager::instance().get<sf::Font>("fonts/UbuntuMono-R.ttf"),
            label_string(id), LABEL_CHARACTER_SIZE);
    text.setOutlineThickness(1.f);
    text.setOutlineColor(LABEL_OUTLINE_COLOR);
    return text;
}

// Whether a marker of `diameter` pixels at `pos` shows in the window
bool marker_visible(sf::Vector2f pos, float diameter, sf::Vector2f window_size) {
    const float radius = diameter / 2.f;
    return pos.x >= -radius && pos.x <= window_size.x + radius && pos.y >= -radius
           && pos.y <= window_size.y + radius;
}

// Whether two boxes overlap, touching edges aside
bool overlap(const sf::FloatRect& a, const sf::FloatRect& b) {
    return a.position.x < b.position.x + b.size.x && b.position.x < a.position.x + a.size.x
           && a.position.y < b.position.y + b.size.y && b.position.y < a.position.y + a.size.y;
}
}  // namespace


std::string_view id_group(std::string_view id) {
    size_t end = id.size();
    while (end > 0 && id[end - 1] >= '0' && id[end - 1] <= '9') {
        end--;
    }
    if (end < id.size() && end > 0 && (id[end - 1] == '-' || id[end - 1] == '_')) {
        end--;
    }
    return id.substr(0, end);
}

std::vector<uint32_t> find_named_bodies(const Bodies& bodies, uint32_t max_group_size,
        uint32_t max_count) {
    // Views into the bodies' ids
    std::unordered_map<std::string_view, uint32_t> group_sizes;
    for (uint64_t i = 0; i < bodies.n; i++) {
        group_sizes[id_group(bodies.id(i))]++;
    }
    std::vector<uint32_t> named;
    for (uint64_t i = 0; i < bodies.n; i++) {
        if (group_sizes[id_group(bodies.id(i))] <= max_group_size) {
            named.push_back(static_cast<uint32_t>(i));
        }
    }
    std::stable_sort(named.begin(), named.end(),
            [&bodies](uint32_t a, uint32_t b) { return bodies.mass(a) > bodies.mass(b); });
    if (named.size() > max_count) {
        Log::info("{} bodies qualify as named; labelling the {} heaviest", named.size(),
                max_count);
        named.resize(max_count);
    }
    return named;
}

float marker_diameter(double mass, double max_mass, float base_px, bool scale_by_mass) {
    using namespace Constants::Graphics;
    double scale = 0.0;  // of the extra size
    if (scale_by_mass && mass > 0.0 && max_mass > 0.0) {
        scale = std::clamp(1.0 + std::log10(mass / max_mass) / NAMED_MARKER_MASS_DECADES, 0.0,
                1.0);
    }
    return base_px + NAMED_MARKER_MIN_EXTRA_PX
           + static_cast<float>(scale) * (NAMED_MARKER_MAX_EXTRA_PX - NAMED_MARKER_MIN_EXTRA_PX);
}

std::optional<uint32_t> pick_marker(sf::Vector2f cursor, std::span<const uint32_t> named,
        const double* mass, const sf::VertexArray& body_vertices, float base_px,
        bool scale_by_mass) {
    const double max_mass = named.empty() ? 0.0 : mass[named.front()];
    std::optional<uint32_t> picked;
    float picked_gap = 0.f;
    for (const uint32_t index : named) {
        if (picked && mass[index] < mass[*picked]) {
            break;  // so are the bodies after it
        }
        // From the marker's edge, negative inside it
        const float gap = (body_vertices[index].position - cursor).length()
                          - marker_diameter(mass[index], max_mass, base_px, scale_by_mass) / 2.f;
        if (gap <= Constants::Graphics::HOVER_MARGIN_PX && (!picked || gap < picked_gap)) {
            picked = index;
            picked_gap = gap;
        }
    }
    return picked;
}

std::vector<std::optional<sf::Vector2f>> place_labels(std::span<const LabelBox> boxes,
        sf::Vector2f window_size, uint32_t max_labels, float gap_px) {
    using Constants::Graphics::LABEL_SPACING_PX;
    std::vector<std::optional<sf::Vector2f>> positions(boxes.size());
    std::vector<sf::FloatRect> placed;
    for (size_t i = 0; i < boxes.size() && placed.size() < max_labels; i++) {
        const LabelBox& box = boxes[i];
        const float offset = box.clearance_px + gap_px;
        // Right (centred vertically), left, above (centred horizontally), below
        const std::array<sf::Vector2f, 4> spots = {
                sf::Vector2f(box.anchor.x + offset, box.anchor.y - box.size.y / 2.f),
                sf::Vector2f(box.anchor.x - offset - box.size.x, box.anchor.y - box.size.y / 2.f),
                sf::Vector2f(box.anchor.x - box.size.x / 2.f, box.anchor.y - offset - box.size.y),
                sf::Vector2f(box.anchor.x - box.size.x / 2.f, box.anchor.y + offset)};
        for (const sf::Vector2f spot : spots) {
            const bool inside = spot.x >= 0.f && spot.y >= 0.f
                                && spot.x + box.size.x <= window_size.x
                                && spot.y + box.size.y <= window_size.y;
            const sf::FloatRect grown(spot - sf::Vector2f(LABEL_SPACING_PX, LABEL_SPACING_PX),
                    box.size + sf::Vector2f(2.f * LABEL_SPACING_PX, 2.f * LABEL_SPACING_PX));
            if (inside && std::none_of(placed.begin(), placed.end(),
                                  [&grown](const sf::FloatRect& r) { return overlap(grown, r); })) {
                positions[i] = spot;
                placed.emplace_back(spot, box.size);
                break;
            }
        }
    }
    return positions;
}


NamedBodies::NamedBodies(const Bodies& bodies) : bodies(bodies), selected_label(make_label("")) {
    using namespace Constants::Graphics;
    const StopWatch sw;
    named = find_named_bodies(bodies, NAMED_GROUP_MAX_SIZE, NAMED_BODIES_MAX);
    rank_of.reserve(named.size());
    labels.reserve(named.size());
    label_bounds.reserve(named.size());
    for (uint32_t rank = 0; rank < named.size(); rank++) {
        rank_of.emplace(named[rank], rank);
        labels.push_back(make_label(bodies.id(named[rank])));
        label_bounds.push_back(labels.back().getLocalBounds());
    }
    if (!named.empty()) {
        max_mass = bodies.mass(named.front());
    }
    Log::debug("Named bodies: {} of {}: [{}]", named.size(), bodies.n, sw);
}

const std::vector<uint32_t>& NamedBodies::indices() const {
    return named;
}

bool NamedBodies::is_named(uint32_t index) const {
    return rank_of.contains(index);
}

float NamedBodies::marker_diameter(uint32_t index, float base_px, bool scale_by_mass) const {
    return ::marker_diameter(bodies.mass(index), max_mass, base_px, scale_by_mass);
}

std::optional<uint32_t> NamedBodies::pick(sf::Vector2f cursor,
        const sf::VertexArray& body_vertices, float base_px, bool scale_by_mass) const {
    return pick_marker(cursor, named, bodies.mass_data(), body_vertices, base_px, scale_by_mass);
}

void NamedBodies::draw_markers(sf::RenderTarget& target, const sf::Shader& marker_shader,
        const sf::VertexArray& body_vertices, float base_px, bool scale_by_mass,
        const std::vector<uint8_t>& selection_mask, bool selection_show) {
    using namespace Constants::Graphics;
    const sf::Vector2f window_size = target.getView().getSize();
    markers.clear();
    for (const uint32_t index : named) {
        const sf::Vector2f pos = body_vertices[index].position;
        const float diameter = marker_diameter(index, base_px, scale_by_mass);
        if (marker_visible(pos, diameter, window_size)) {
            const sf::Color color =
                    selection_show && selection_mask[index] ? SELECT_COLOR : NAMED_BODY_COLOR;
            // The marker shader reads the diameter from the texture coordinates
            markers.append({pos, color, {diameter, 0.f}});
        }
    }
    target.draw(markers, sf::RenderStates(&marker_shader));
}

void NamedBodies::draw_labels(sf::RenderTarget& target, const sf::VertexArray& body_vertices,
        float base_px, bool scale_by_mass, std::optional<uint32_t> selected_body,
        const std::vector<uint8_t>& selection_mask, bool selection_show) {
    using namespace Constants::Graphics;
    const sf::Vector2f window_size = target.getView().getSize();
    label_candidates.clear();
    label_boxes.clear();
    const auto add_candidate = [&](uint32_t index, sf::Text& text, const sf::FloatRect& bounds,
                                       float diameter) {
        const sf::Vector2f pos = body_vertices[index].position;
        if (marker_visible(pos, diameter, window_size)) {
            label_candidates.push_back({index, &text, bounds});
            label_boxes.push_back({pos, diameter / 2.f, bounds.size});
        }
    };

    if (selected_body) {
        if (const auto it = rank_of.find(*selected_body); it != rank_of.end()) {
            add_candidate(*selected_body, labels[it->second], label_bounds[it->second],
                    marker_diameter(*selected_body, base_px, scale_by_mass));
        }
        else {
            if (selected_label_index != selected_body) {
                selected_label.setString(label_string(bodies.id(*selected_body)));
                selected_label_index = selected_body;
            }
            add_candidate(*selected_body, selected_label, selected_label.getLocalBounds(),
                    base_px);
        }
    }
    for (uint32_t rank = 0; rank < named.size(); rank++) {
        if (named[rank] != selected_body) {
            add_candidate(named[rank], labels[rank], label_bounds[rank],
                    marker_diameter(named[rank], base_px, scale_by_mass));
        }
    }

    const auto positions = place_labels(label_boxes, window_size, LABELS_MAX, LABEL_GAP_PX);
    for (size_t i = 0; i < positions.size(); i++) {
        if (!positions[i]) {
            continue;
        }
        const LabelCandidate& candidate = label_candidates[i];
        // Whole pixels keep the text crisp
        candidate.text->setPosition({std::round(positions[i]->x - candidate.bounds.position.x),
                std::round(positions[i]->y - candidate.bounds.position.y)});
        candidate.text->setFillColor(
                selection_show && selection_mask[candidate.index] ? SELECT_COLOR : LABEL_COLOR);
        target.draw(*candidate.text);
    }
}
