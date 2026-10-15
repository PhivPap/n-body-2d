#include "NamedBodies/NamedBodies.hpp"

#include <algorithm>
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

// Where a text's last character ends, in its local coordinates
float text_end(const sf::Text& text) {
    return text.findCharacterPos(text.getString().getSize()).x - text.getPosition().x;
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

std::vector<LabelPlacement> place_labels(std::span<const LabelBox> boxes,
        sf::Vector2f window_size, uint32_t max_labels, float gap_px) {
    using Constants::Graphics::LABEL_SPACING_PX;
    const sf::FloatRect window({0.f, 0.f}, window_size);
    const sf::Vector2f spacing(LABEL_SPACING_PX, LABEL_SPACING_PX);
    std::vector<LabelPlacement> placements(boxes.size());
    std::vector<size_t> placed;  // positions in `boxes`
    for (size_t i = 0; i < boxes.size() && placed.size() < max_labels; i++) {
        const LabelBox& box = boxes[i];
        const sf::Vector2f spot(box.anchor.x + box.clearance_px + gap_px,
                box.anchor.y - box.size.y / 2.f);
        if (!overlap({spot, box.size}, window)) {
            continue;
        }
        const sf::FloatRect grown(spot - spacing, box.size + 2.f * spacing);
        const auto hiding = std::find_if(placed.begin(), placed.end(), [&](size_t j) {
            return overlap(grown, {*placements[j].position, boxes[j].size});
        });
        if (hiding == placed.end()) {
            placements[i].position = spot;
            placed.push_back(i);
        }
        else {
            placements[*hiding].more = true;
        }
    }
    return placements;
}


NamedBodies::NamedBodies(const Bodies& bodies)
        : bodies(bodies), selected_label(make_label("")), more_label(make_label(" +")) {
    using namespace Constants::Graphics;
    const StopWatch sw;
    named = find_named_bodies(bodies, NAMED_GROUP_MAX_SIZE, NAMED_BODIES_MAX);
    rank_of.reserve(named.size());
    labels.reserve(named.size());
    label_bounds.reserve(named.size());
    label_ends.reserve(named.size());
    for (uint32_t rank = 0; rank < named.size(); rank++) {
        rank_of.emplace(named[rank], rank);
        labels.push_back(make_label(bodies.id(named[rank])));
        label_bounds.push_back(labels.back().getLocalBounds());
        label_ends.push_back(text_end(labels.back()));
    }
    const sf::FloatRect more_bounds = more_label.getLocalBounds();
    more_width = more_bounds.position.x + more_bounds.size.x;
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
                                       float end_x, float diameter) {
        const sf::Vector2f pos = body_vertices[index].position;
        if (marker_visible(pos, diameter, window_size)) {
            label_candidates.push_back({index, &text, bounds, end_x});
            // Up to the end of a " +" after the text
            label_boxes.push_back({pos, diameter / 2.f,
                    {end_x + more_width - bounds.position.x, bounds.size.y}});
        }
    };

    if (selected_body) {
        if (const auto it = rank_of.find(*selected_body); it != rank_of.end()) {
            const uint32_t rank = it->second;
            add_candidate(*selected_body, labels[rank], label_bounds[rank], label_ends[rank],
                    marker_diameter(*selected_body, base_px, scale_by_mass));
        }
        else {
            if (selected_label_index != selected_body) {
                selected_label.setString(label_string(bodies.id(*selected_body)));
                selected_label_end = text_end(selected_label);
                selected_label_index = selected_body;
            }
            add_candidate(*selected_body, selected_label, selected_label.getLocalBounds(),
                    selected_label_end, base_px);
        }
    }
    for (uint32_t rank = 0; rank < named.size(); rank++) {
        if (named[rank] != selected_body) {
            add_candidate(named[rank], labels[rank], label_bounds[rank], label_ends[rank],
                    marker_diameter(named[rank], base_px, scale_by_mass));
        }
    }

    const auto placements = place_labels(label_boxes, window_size, LABELS_MAX, LABEL_GAP_PX);
    for (size_t i = 0; i < placements.size(); i++) {
        if (!placements[i].position) {
            continue;
        }
        const LabelCandidate& candidate = label_candidates[i];
        // Whole pixels keep the text crisp
        const sf::Vector2f pos(std::round(placements[i].position->x - candidate.bounds.position.x),
                std::round(placements[i].position->y - candidate.bounds.position.y));
        const sf::Color color =
                selection_show && selection_mask[candidate.index] ? SELECT_COLOR : LABEL_COLOR;
        candidate.text->setPosition(pos);
        candidate.text->setFillColor(color);
        target.draw(*candidate.text);
        if (placements[i].more) {
            more_label.setPosition({pos.x + std::round(candidate.end_x), pos.y});
            more_label.setFillColor(color);
            target.draw(more_label);
        }
    }
}
