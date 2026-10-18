#pragma once

#include <string_view>

#include "SFML/Graphics/Font.hpp"
#include "SFML/System/String.hpp"


// Text in the window: its font, and strings decoded from UTF-8
namespace Text {
// The font of all text in the window
const sf::Font& font();

// `utf8` decoded, with U+FFFD in place of bytes that don't form a valid sequence: SFML's decoder
// trusts lead bytes, so an id in another encoding would swallow the characters after it in a text,
// newlines included
sf::String from_utf8(std::string_view utf8);
}  // namespace Text
