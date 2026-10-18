#include "Text/Text.hpp"

#include <cstdint>
#include <string>

#include "AssetManager/AssetManager.hpp"


namespace Text {
const sf::Font& font() {
    return AssetManager::instance().get<sf::Font>("fonts/UbuntuMono-R.ttf");
}

sf::String from_utf8(std::string_view utf8) {
    constexpr char32_t REPLACEMENT = 0xFFFD;
    std::u32string decoded;
    decoded.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        const auto lead = static_cast<uint8_t>(utf8[i]);
        // The sequence's length, the code point's bits in its lead byte, and the smallest code
        // point that needs that length
        size_t length = 1;
        char32_t code_point = lead, smallest = 0;
        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            code_point = lead & 0x1F;
            smallest = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            code_point = lead & 0x0F;
            smallest = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            code_point = lead & 0x07;
            smallest = 0x10000;
        }
        else if (lead >= 0x80) {
            decoded += REPLACEMENT;  // a continuation byte, or no lead byte at all
            i++;
            continue;
        }
        size_t k = 1;
        for (; k < length && i + k < utf8.size()
                && (static_cast<uint8_t>(utf8[i + k]) & 0xC0) == 0x80;
                k++) {
            code_point = (code_point << 6) | (static_cast<uint8_t>(utf8[i + k]) & 0x3F);
        }
        // Cut short, overlong, a surrogate or beyond Unicode: one U+FFFD, and a sequence cut short
        // goes on from the byte that cut it
        const bool valid = k == length && code_point >= smallest && code_point <= 0x10FFFF
                           && !(code_point >= 0xD800 && code_point <= 0xDFFF);
        decoded += valid ? code_point : REPLACEMENT;
        i += k;
    }
    return sf::String(decoded);
}
}  // namespace Text
