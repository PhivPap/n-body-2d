#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "Body/Body.hpp"
#include "Config/Config.hpp"
#include "NamedBodies/NamedBodies.hpp"
#include "SFML/Graphics/Color.hpp"
#include "SFML/Graphics/Glsl.hpp"


// Bodies drawn alike: a legend row (a population, or "Other" for the populations beyond
// LEGEND_GROUPS_MAX), the bodies of a style key, or the default group of every other body
struct RenderGroup {
    std::string name;  // the style key, id group, "Other" or "Named"
    uint32_t count;    // bodies
    sf::Color color;   // sRGB, as configured or from the palette
    float brightness;
    bool visible = true;
    bool in_legend;
};

// The deposit shader's table: per group, its colour in linear RGB at unit luminance and its
// brightness (0 while hidden), padded with zeros (hidden) to MAX_RENDER_GROUPS
std::vector<sf::Glsl::Vec4> style_table(std::span<const RenderGroup> groups);

// Every body's render group. The legend's rows are the populations (IdGroups), largest first: the
// first LEGEND_GROUPS_MAX get the palette's colours and the rest are "Other". Style keys (exact
// ids first, then id groups) restyle legend rows and give other bodies groups of their own, in
// order of first appearance and as long as MAX_RENDER_GROUPS has room. All other bodies are in the
// default group, "Named", drawn white. Only legend rows are shown or hidden.
class RenderGroups {
public:
    using Styles = std::map<std::string, Config::Graphics::GroupStyle>;

    // `id_groups`: group_ids() of `bodies`, with the named bodies' limits
    RenderGroups(const Bodies& bodies, const IdGroups& id_groups, const Styles& styles);
    const std::vector<uint8_t>& of_body() const;  // each body's group
    const std::vector<RenderGroup>& groups() const;
    const std::vector<uint32_t>& legend() const;  // the legend's rows' groups
    const std::vector<std::string>& unmatched_styles() const;  // style keys of no body
    // Per group, 1 if it is drawn: visible, at a brightness above 0
    const std::vector<uint8_t>& visibility() const;
    // Show or hide legend rows (out of range: nothing), returning whether anything changed. solo()
    // shows only that row of the legend's.
    bool toggle(uint32_t row);
    bool solo(uint32_t row);
    bool show_all();

private:
    std::vector<uint8_t> group_of_body;
    std::vector<RenderGroup> table;
    std::vector<uint32_t> legend_groups;
    std::vector<std::string> unmatched;
    std::vector<uint8_t> drawn;

    void update_visibility();
};
