#pragma once

#include <cstdint>

#include "SFML/Graphics/Vertex.hpp"


// What a body's vertex holds besides its window position, for the renderer's deposit shader: its
// render group in the colour's red channel and flags in its green one (blue 0, alpha 255), and in
// the Speed and Doppler modes its velocity relative to the reference frame (m/s) in its texture
// coordinates. A default vertex (white) decodes as group 255.
namespace BodyVertex {
constexpr uint8_t HIGHLIGHT = 1;  // selected, with the selection shown: drawn in SELECT_COLOR

// Also clears the flags
inline void set_group(sf::Vertex& vertex, uint8_t group) {
    vertex.color = sf::Color(group, 0, 0, 255);
}

inline uint8_t group(const sf::Vertex& vertex) {
    return vertex.color.r;
}

inline void set_highlight(sf::Vertex& vertex, bool highlight) {
    vertex.color.g = static_cast<uint8_t>(
            highlight ? vertex.color.g | HIGHLIGHT : vertex.color.g & ~HIGHLIGHT);
}

inline bool highlighted(const sf::Vertex& vertex) {
    return vertex.color.g & HIGHLIGHT;
}
}  // namespace BodyVertex
