#include "Render/FloatTarget.hpp"

#include <GL/gl.h>
#include <GL/glext.h>
#include <algorithm>
#include <stdexcept>

#include "SFML/Window/Context.hpp"


void resize_float_target(sf::RenderTexture& target, sf::Vector2u size) {
    const auto fail = [] {
        throw std::runtime_error("HDR rendering needs OpenGL 3.0 float render targets (RGBA32F)");
    };
    if (!target.resize(size) || !target.setActive(true)) {
        fail();
    }
    // GL 3.0, so <GL/gl.h> doesn't declare it: loaded once, with a context active
    static const auto check_framebuffer_status = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(
            sf::Context::getFunction("glCheckFramebufferStatus"));
    if (!check_framebuffer_status) {
        fail();
    }
    sf::Texture::bind(&target.getTexture());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, static_cast<GLsizei>(size.x),
            static_cast<GLsizei>(size.y), 0, GL_RGBA, GL_FLOAT, nullptr);
    GLint format = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    sf::Texture::bind(nullptr);
    if (format != GL_RGBA32F
            || check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fail();
    }
    target.clear(sf::Color::Transparent);
    target.display();
}

std::vector<float> read_pixels(sf::RenderTexture& target, sf::IntRect rect) {
    const auto row_floats = static_cast<size_t>(4 * rect.size.x);
    std::vector<float> rows(row_floats * rect.size.y);
    if (!target.setActive(true)) {
        return {};
    }
    // SFML's render textures are upside down in GL terms: view row y is GL row H - 1 - y
    const int height = static_cast<int>(target.getSize().y);
    glReadPixels(rect.position.x, height - rect.position.y - rect.size.y, rect.size.x,
            rect.size.y, GL_RGBA, GL_FLOAT, rows.data());
    std::vector<float> flipped(rows.size());
    for (int r = 0; r < rect.size.y; r++) {
        std::copy_n(&rows[row_floats * (rect.size.y - 1 - r)], row_floats,
                &flipped[row_floats * r]);
    }
    return flipped;
}
