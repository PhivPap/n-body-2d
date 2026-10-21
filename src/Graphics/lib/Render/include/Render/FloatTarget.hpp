#pragma once

#include <vector>

#include "SFML/Graphics/Rect.hpp"
#include "SFML/Graphics/RenderTexture.hpp"


// Float render targets, which SFML doesn't make: a render texture whose texture is re-specified
// as RGBA32F (RGBA16F stops counting at 2048 bodies a pixel), which the framebuffer SFML made
// keeps attached. Raw GL, so call resetGLStates() on the window afterwards: SFML's state cache
// could skip a texture bind.

// Sizes `target` and makes it an RGBA32F target cleared to 0, again after any resize(), which
// re-creates its texture as RGBA8. Throws std::runtime_error without float render targets.
void resize_float_target(sf::RenderTexture& target, sf::Vector2u size);
// A rectangle of a target's float pixels (RGBA), rows top to bottom in SFML's (y down)
// coordinates. Stalls until the GPU is done: for tests and the exposure lock.
std::vector<float> read_pixels(sf::RenderTexture& target, sf::IntRect rect);
