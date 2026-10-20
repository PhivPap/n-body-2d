#include "Render/RenderGroups.hpp"

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"
#include "Render/Colors.hpp"
#include "StopWatch/StopWatch.hpp"


std::vector<sf::Glsl::Vec4> style_table(std::span<const RenderGroup> groups) {
    using Constants::Graphics::MAX_RENDER_GROUPS;
    std::vector<sf::Glsl::Vec4> table(MAX_RENDER_GROUPS, sf::Glsl::Vec4(0.f, 0.f, 0.f, 0.f));
    for (size_t k = 0; k < std::min<size_t>(groups.size(), MAX_RENDER_GROUPS); k++) {
        const sf::Vector3f color = Colors::unit_luminance(Colors::to_linear(groups[k].color));
        table[k] = {color.x, color.y, color.z, groups[k].visible ? groups[k].brightness : 0.f};
    }
    return table;
}


RenderGroups::RenderGroups(const Bodies& bodies, const IdGroups& id_groups, const Styles& styles)
        : group_of_body(bodies.n) {
    using namespace Constants::Graphics;
    const StopWatch sw;
    constexpr uint32_t NONE = UINT32_MAX;

    // A style key names an id group ("G1" is the group of "G1-5"), an exact id, or both. Only keys
    // with a trailing number need looking up as ids: a body whose id has none is in the group of
    // that name.
    std::vector<const Styles::value_type*> keys;
    std::unordered_map<std::string_view, uint32_t> group_keys, id_keys;
    std::unordered_set<std::string_view> id_key_groups;  // the id groups of those ids
    for (const auto& entry : styles) {
        const std::string_view key = entry.first;
        group_keys.emplace(key, static_cast<uint32_t>(keys.size()));
        if (id_group(key) != key) {
            id_keys.emplace(key, static_cast<uint32_t>(keys.size()));
            id_key_groups.insert(id_group(key));
        }
        keys.push_back(&entry);
    }
    std::vector<uint32_t> group_of_key(keys.size(), NONE);
    std::vector<uint32_t> group_of_id_group(id_groups.names.size(), NONE);

    // The legend's rows: the populations, largest first, then by name
    std::vector<uint32_t> populations;
    for (uint32_t g = 0; g < id_groups.names.size(); g++) {
        if (id_groups.population[g]) {
            populations.push_back(g);
        }
    }
    std::sort(populations.begin(), populations.end(), [&id_groups](uint32_t a, uint32_t b) {
        const auto& sizes = id_groups.sizes;
        return sizes[a] != sizes[b] ? sizes[a] > sizes[b] : id_groups.names[a] < id_groups.names[b];
    });
    for (uint32_t row = 0; row < populations.size(); row++) {
        const uint32_t g = populations[row];
        const auto key = group_keys.find(id_groups.names[g]);
        const auto* style = key == group_keys.end() ? nullptr : &keys[key->second]->second;
        if (row < LEGEND_GROUPS_MAX) {
            group_of_id_group[g] = static_cast<uint32_t>(table.size());
            table.push_back({.name = std::string(id_groups.names[g]), .count = 0,
                    .color = style ? style->color : GROUP_PALETTE[row],
                    .brightness = style ? style->brightness : 1.f, .in_legend = true});
        }
        else {
            if (row == LEGEND_GROUPS_MAX) {
                table.push_back({.name = "Other", .count = 0, .color = OTHER_GROUP_COLOR,
                        .brightness = 1.f, .in_legend = true});
            }
            group_of_id_group[g] = LEGEND_GROUPS_MAX;
            if (style) {
                Log::warning("Style `{}` left out: its population is in Other, after the "
                        "legend's {} rows", key->first, LEGEND_GROUPS_MAX);
            }
        }
        if (style) {
            group_of_key[key->second] = group_of_id_group[g];
        }
    }
    legend_groups.resize(table.size());
    std::iota(legend_groups.begin(), legend_groups.end(), 0);
    const auto default_group = static_cast<uint32_t>(table.size());
    table.push_back({.name = "Named", .count = 0, .color = NAMED_BODY_COLOR, .brightness = 1.f,
            .in_legend = false});

    // A style key's group, made when its first body appears while the table has room
    const auto group_of_style = [&](uint32_t key) {
        if (group_of_key[key] == NONE) {
            const auto& [name, style] = *keys[key];
            if (table.size() < MAX_RENDER_GROUPS) {
                group_of_key[key] = static_cast<uint32_t>(table.size());
                table.push_back({.name = name, .count = 0, .color = style.color,
                        .brightness = style.brightness, .in_legend = false});
            }
            else {
                Log::warning("Style `{}` left out: there is room for {} render groups", name,
                        MAX_RENDER_GROUPS);
                group_of_key[key] = default_group;
            }
        }
        return group_of_key[key];
    };
    // Exact ids are only looked up in the id groups that have some, so ids are hashed once
    std::vector<uint8_t> has_id_keys(id_groups.names.size());
    for (size_t g = 0; g < id_groups.names.size(); g++) {
        has_id_keys[g] = id_key_groups.contains(id_groups.names[g]);
    }
    for (uint64_t i = 0; i < bodies.n; i++) {
        const uint32_t g = id_groups.of_body[i];
        uint32_t group = NONE;
        if (has_id_keys[g]) {
            if (const auto key = id_keys.find(bodies.id(i)); key != id_keys.end()) {
                group = group_of_style(key->second);
            }
        }
        if (group == NONE) {
            if (group_of_id_group[g] == NONE) {
                const auto key = group_keys.find(id_groups.names[g]);
                group_of_id_group[g] =
                        key == group_keys.end() ? default_group : group_of_style(key->second);
            }
            group = group_of_id_group[g];
        }
        group_of_body[i] = static_cast<uint8_t>(group);
        table[group].count++;
    }
    for (uint32_t key = 0; key < keys.size(); key++) {
        if (group_of_key[key] == NONE) {
            unmatched.push_back(keys[key]->first);
        }
    }
    update_visibility();
    Log::debug("Render groups: {}, {} in the legend: [{}]", table.size(), legend_groups.size(),
            sw);
}

const std::vector<uint8_t>& RenderGroups::of_body() const {
    return group_of_body;
}

const std::vector<RenderGroup>& RenderGroups::groups() const {
    return table;
}

const std::vector<uint32_t>& RenderGroups::legend() const {
    return legend_groups;
}

const std::vector<std::string>& RenderGroups::unmatched_styles() const {
    return unmatched;
}

const std::vector<uint8_t>& RenderGroups::visibility() const {
    return drawn;
}

bool RenderGroups::toggle(uint32_t row) {
    if (row >= legend_groups.size()) {
        return false;
    }
    RenderGroup& group = table[legend_groups[row]];
    group.visible = !group.visible;
    update_visibility();
    return true;
}

bool RenderGroups::solo(uint32_t row) {
    if (row >= legend_groups.size()) {
        return false;
    }
    bool changed = false;
    for (uint32_t r = 0; r < legend_groups.size(); r++) {
        RenderGroup& group = table[legend_groups[r]];
        changed |= group.visible != (r == row);
        group.visible = r == row;
    }
    update_visibility();
    return changed;
}

bool RenderGroups::show_all() {
    bool changed = false;
    for (const uint32_t g : legend_groups) {
        changed |= !table[g].visible;
        table[g].visible = true;
    }
    update_visibility();
    return changed;
}

void RenderGroups::update_visibility() {
    drawn.resize(table.size());
    for (size_t k = 0; k < table.size(); k++) {
        drawn[k] = table[k].visible && table[k].brightness > 0.f;
    }
}
