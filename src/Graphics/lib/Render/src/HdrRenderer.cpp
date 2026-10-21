#include "Render/HdrRenderer.hpp"

#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "Constants/Constants.hpp"
#include "Render/Colors.hpp"
#include "Render/FloatTarget.hpp"
#include "fmt/format.h"


namespace {
// A body (a point), or an end of its trail segment (a line). The vertex colour holds the body's
// render group (r) and flags (g, see BodyVertex.hpp), the texture coordinates its velocity
// relative to the reference frame (m/s). Only the Speed and Doppler modes read the velocities,
// with VELOCITY defined: the driver only transfers the vertex attributes a shader reads, and the
// transfer of 1M vertices costs a GPU simulation as much as the rest of the renderer.
constexpr std::string_view deposit_vertex_shader = R"glsl(
uniform vec4 group_style[MAX_RENDER_GROUPS];  // rgb linear, unit luminance; a brightness, 0: hidden
uniform int color_mode;                       // ColorMode: White, Groups, Speed, Doppler, Density
uniform vec3 select_color;                    // linear, unit luminance
uniform float point_size;                     // pixels
#ifdef VELOCITY
uniform vec3 speed_stops[MAX_RAMP_STOPS];     // linear
uniform int speed_stop_count;
uniform vec3 doppler_stops[MAX_RAMP_STOPS];   // linear
uniform int doppler_stop_count;
uniform float velocity_scale;                 // m/s, > 0
#endif
out vec3 deposit;  // HDR, so not gl_FrontColor, which is clamped to [0, 1]
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

#ifdef VELOCITY
// Ramp colour at t in [0, 1], of unit luminance
vec3 ramp(vec3 stops[MAX_RAMP_STOPS], int count, float t) {
    float x = clamp(t, 0.0, 1.0) * float(count - 1);
    int k = min(int(x), count - 2);
    vec3 c = mix(stops[k], stops[k + 1], x - float(k));
    return c / dot(c, LUMA);
}
#endif

void main() {
    int group = min(int(gl_Color.r * 255.0 + 0.5), MAX_RENDER_GROUPS - 1);
    int flags = int(gl_Color.g * 255.0 + 0.5);
    vec4 style = group_style[group];
    if (!(style.a > 0.0)) {  // hidden: outside the clip volume
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        deposit = vec3(0.0);
        return;
    }
    vec3 c = vec3(1.0);  // White and Density: every body alike (white has unit luminance)
    float brightness = 1.0;
    if (color_mode == 1 || color_mode == 2 || color_mode == 3) {
        brightness = style.a;
        if (color_mode == 1) {
            c = style.rgb;
        }
#ifdef VELOCITY
        else if (color_mode == 2) {
            c = ramp(speed_stops, speed_stop_count, length(gl_MultiTexCoord0.xy) / velocity_scale);
        }
        else {  // up the screen (v.y < 0) recedes from an observer below the window: red end
            c = ramp(doppler_stops, doppler_stop_count,
                    0.5 - 0.5 * gl_MultiTexCoord0.y / velocity_scale);
        }
#endif
    }
    if ((flags & 1) != 0 && color_mode != 4) {
        c = select_color;
    }
    deposit = c * brightness;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_PointSize = point_size;
}
)glsl";
constexpr std::string_view deposit_fragment_shader = R"glsl(
uniform int kernel;         // 0: flat (trail segments), 1: tent (1 px bodies), 2: disc
uniform float point_size;   // the points' size in pixels: 2 for the tent, the disc's diameter + 2
uniform float disc_radius;  // kernel 2: the body's radius in pixels
in vec3 deposit;

void main() {
    float w = 1.0;
    if (kernel == 1) {  // bilinear weights of the 2x2 pixels around the body: they sum to 1
        vec2 d = abs(gl_PointCoord - 0.5) * point_size;
        w = max(0.0, 1.0 - d.x) * max(0.0, 1.0 - d.y);
    }
    else if (kernel == 2) {  // anti-aliased disc
        w = clamp(disc_radius + 0.5 - length(gl_PointCoord - 0.5) * point_size, 0.0, 1.0);
    }
    gl_FragColor = vec4(deposit * w, 0.0);
}
)glsl";
// The phosphor, drawn as a sprite of the previous trails: each pixel keeps the brightest of its
// faded value and this frame's deposits
constexpr std::string_view trail_fragment_shader = R"glsl(
uniform sampler2D trails;
uniform sampler2D deposits;
uniform sampler2D exposure;   // 1x1: the auto white point
uniform float locked_white;   // > 0 while locked: used instead
uniform float ev_scale;       // 2^EV
uniform float beta;           // tone curve softening, bodies/px
uniform float fade;           // display levels removed this frame: frame time / fade time, 0 paused
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec3 t = texture2D(trails, uv).rgb;
    float I = dot(t, LUMA);
    if (I > 0.0 && fade > 0.0) {  // lower the display level linearly, keeping the hue
        float white = (locked_white > 0.0 ? locked_white
                                          : texelFetch(exposure, ivec2(0, 0), 0).r) / ev_scale;
        float A = asinh(max(white, 1e-6) / beta);
        float level = asinh(I / beta) / A - fade;
        t *= level > 0.0 ? beta * sinh(level * A) / I : 0.0;
    }
    gl_FragColor = vec4(max(t, texture2D(deposits, uv).rgb), 0.0);
}
)glsl";
// The image's maximum luminance in each BLOCK x BLOCK block
constexpr std::string_view reduce_fragment_shader = R"glsl(
uniform sampler2D image;
uniform ivec2 image_size;
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

void main() {
    ivec2 base = ivec2(gl_FragCoord.xy) * BLOCK;
    float m = 0.0;
    for (int j = 0; j < BLOCK; j++) {
        for (int i = 0; i < BLOCK; i++) {
            ivec2 p = base + ivec2(i, j);
            if (p.x < image_size.x && p.y < image_size.y) {
                m = max(m, dot(texelFetch(image, p, 0).rgb, LUMA));
            }
        }
    }
    gl_FragColor = vec4(m, 0.0, 0.0, 0.0);
}
)glsl";
// One point per block, added into the bin of its maximum's log2
constexpr std::string_view histogram_vertex_shader = R"glsl(
uniform sampler2D blocks;
uniform ivec2 blocks_size;

void main() {
    ivec2 p = ivec2(gl_VertexID % blocks_size.x, gl_VertexID / blocks_size.x);
    float m = texelFetch(blocks, p, 0).r;
    gl_PointSize = 1.0;
    if (!(m > 0.0)) {  // empty (or NaN): not counted
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    float bin = clamp(floor((log2(m) - LOG2_MIN) * BINS_PER_OCTAVE), 0.0, float(BINS - 1));
    gl_Position = vec4((bin + 0.5) / float(BINS) * 2.0 - 1.0, 0.0, 0.0, 1.0);
}
)glsl";
constexpr std::string_view histogram_fragment_shader = R"glsl(
void main() {
    gl_FragColor = vec4(1.0, 0.0, 0.0, 0.0);
}
)glsl";
// The auto white point, into a 1x1 target, followed in log space
constexpr std::string_view white_fragment_shader = R"glsl(
uniform sampler2D histogram;
uniform sampler2D previous;   // 1x1
uniform float quantile;
uniform float min_white;      // bodies/px
uniform float adapt;          // 1 - exp(-frame time / adapt time)
uniform bool snap;            // first frame, resize

void main() {
    float total = 0.0;
    for (int i = 0; i < BINS; i++) {
        total += texelFetch(histogram, ivec2(i, 0), 0).r;
    }
    float old = texelFetch(previous, ivec2(0, 0), 0).r;
    if (total == 0.0) {  // nothing visible: keep the white point
        gl_FragColor = vec4(old > 0.0 ? old : min_white, 0.0, 0.0, 0.0);
        return;
    }
    // The value with (1 - quantile) of the blocks above it, interpolated in its bin in log2
    float target = (1.0 - quantile) * total;
    float above = 0.0;
    float white = min_white;
    for (int i = BINS - 1; i >= 0; i--) {
        float count = texelFetch(histogram, ivec2(i, 0), 0).r;
        if (count > 0.0 && above + count > target) {
            float f = (target - above) / count;
            white = exp2(LOG2_MIN + (float(i) + 1.0 - f) / BINS_PER_OCTAVE);
            break;
        }
        above += count;
    }
    white = max(white, min_white);
    float w = snap || !(old > 0.0) ? white : exp2(mix(log2(old), log2(white), adapt));
    gl_FragColor = vec4(w, 0.0, 0.0, 0.0);
}
)glsl";
// The image onto the window: an asinh stretch to the white point, the pixel's hue at full
// brightness turning white near it, or in Density mode the luminance through the colormap
constexpr std::string_view tonemap_fragment_shader = R"glsl(
uniform sampler2D image;
uniform sampler2D exposure;
uniform float locked_white;
uniform float ev_scale;
uniform float beta;
uniform float desaturation;                 // top share of the display range blended to white
uniform bool density;                       // Density mode
uniform vec3 density_stops[DENSITY_STOPS];  // display (sRGB-encoded) values
uniform float frame;                        // frame counter, moves the dither
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

vec3 encode(vec3 c) {  // linear to sRGB
    return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

float noise(vec2 p) {  // interleaved gradient noise, in [0, 1)
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec3 c = texture2D(image, uv).rgb;
    float white = (locked_white > 0.0 ? locked_white
                                      : texelFetch(exposure, ivec2(0, 0), 0).r) / ev_scale;
    float I = dot(c, LUMA);
    float d = I > 0.0 ? asinh(I / beta) / asinh(max(white, 1e-6) / beta) : 0.0;
    float level = min(d, 1.0);
    vec3 color = vec3(0.0);  // black stays black: no dither, no colormap
    if (level > 0.0) {
        if (density) {
            float x = level * float(DENSITY_STOPS - 1);
            int k = min(int(x), DENSITY_STOPS - 2);
            color = mix(density_stops[k], density_stops[k + 1], x - float(k));
        }
        else {  // the hue at full brightness, scaled; near white, towards white
            vec3 hue = encode(c / max(c.r, max(c.g, c.b)));
            color = mix(hue * level, vec3(level),
                    clamp((d - (1.0 - desaturation)) / desaturation, 0.0, 1.0));
        }
        // Triangular dither of one 8-bit step against banding
        vec2 p = gl_FragCoord.xy + 5.588238 * frame;
        color += (noise(p) + noise(p + vec2(47.0, 17.0)) - 1.0) / 255.0;
    }
    gl_FragColor = vec4(color, 1.0);
}
)glsl";

// A shader's source: GLSL 1.30, the constants it shares with Constants.hpp, `defines`, then
// `body`
std::string shader_source(std::string_view body, std::string_view defines = "") {
    using namespace Constants::Graphics;
    return fmt::format("#version 130\n"
                       "{}"
                       "#define MAX_RENDER_GROUPS {}\n"
                       "#define MAX_RAMP_STOPS {}\n"
                       "#define DENSITY_STOPS {}\n"
                       "#define BLOCK {}\n"
                       "#define BINS {}\n"
                       "#define LOG2_MIN {:.6f}\n"
                       "#define BINS_PER_OCTAVE {:.6f}\n",
                   defines, MAX_RENDER_GROUPS, MAX_RAMP_STOPS, std::size(DENSITY_STOPS),
                   EXPOSURE_BLOCK_PX, EXPOSURE_HISTOGRAM_BINS, EXPOSURE_LOG2_MIN,
                   EXPOSURE_BINS_PER_OCTAVE)
           + std::string(body);
}

// A ramp's stops as the deposit shader takes them: linear, padded to MAX_RAMP_STOPS
std::vector<sf::Glsl::Vec3> linear_stops(std::span<const sf::Color> stops) {
    std::vector<sf::Glsl::Vec3> linear(Constants::Graphics::MAX_RAMP_STOPS);
    std::transform(stops.begin(), stops.end(), linear.begin(), [](sf::Color color) {
        return Colors::to_linear(color);
    });
    return linear;
}

// Light adds up. Not sf::BlendAdd, which multiplies by the source's alpha: deposits have none.
const sf::BlendMode additive(sf::BlendMode::Factor::One, sf::BlendMode::Factor::One);

// `shader` drawn without blending
sf::RenderStates replacing(const sf::Shader& shader) {
    sf::RenderStates states(sf::BlendNone);
    states.shader = &shader;
    return states;
}
}  // namespace


HdrRenderer::HdrRenderer(sf::Vector2u size) {
    using namespace Constants::Graphics;
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE);  // gl_PointCoord is undefined without it (compatibility profile)
    if (!deposit_shader.loadFromMemory(shader_source(deposit_vertex_shader),
                shader_source(deposit_fragment_shader))
            || !velocity_deposit_shader.loadFromMemory(
                    shader_source(deposit_vertex_shader, "#define VELOCITY\n"),
                    shader_source(deposit_fragment_shader))
            || !trail_shader.loadFromMemory(shader_source(trail_fragment_shader),
                    sf::Shader::Type::Fragment)
            || !reduce_shader.loadFromMemory(shader_source(reduce_fragment_shader),
                    sf::Shader::Type::Fragment)
            || !histogram_shader.loadFromMemory(shader_source(histogram_vertex_shader),
                    shader_source(histogram_fragment_shader))
            || !white_shader.loadFromMemory(shader_source(white_fragment_shader),
                    sf::Shader::Type::Fragment)
            || !tonemap_shader.loadFromMemory(shader_source(tonemap_fragment_shader),
                    sf::Shader::Type::Fragment)) {
        throw std::runtime_error("Failed to load the renderer's shaders");
    }
    resize_float_target(histogram, {EXPOSURE_HISTOGRAM_BINS, 1});
    for (sf::RenderTexture& target : exposure) {
        resize_float_target(target, {1, 1});
    }
    resize(size);

    set_groups({});
    const sf::Glsl::Vec3 select_color = Colors::unit_luminance(Colors::to_linear(SELECT_COLOR));
    deposit_shader.setUniform("select_color", select_color);
    velocity_deposit_shader.setUniform("select_color", select_color);
    const auto speed_stops = linear_stops(SPEED_STOPS);
    velocity_deposit_shader.setUniformArray("speed_stops", speed_stops.data(), speed_stops.size());
    velocity_deposit_shader.setUniform("speed_stop_count",
            static_cast<int>(std::size(SPEED_STOPS)));
    const auto doppler_stops = linear_stops(DOPPLER_STOPS);
    velocity_deposit_shader.setUniformArray("doppler_stops", doppler_stops.data(),
            doppler_stops.size());
    velocity_deposit_shader.setUniform("doppler_stop_count",
            static_cast<int>(std::size(DOPPLER_STOPS)));
    trail_shader.setUniform("trails", sf::Shader::CurrentTexture);
    trail_shader.setUniform("deposits", deposits.getTexture());
    trail_shader.setUniform("beta", TONE_SOFTENING);
    histogram_shader.setUniform("blocks", blocks.getTexture());
    white_shader.setUniform("histogram", histogram.getTexture());
    white_shader.setUniform("quantile", AUTO_WHITE_QUANTILE);
    white_shader.setUniform("min_white", AUTO_WHITE_MIN);
    tonemap_shader.setUniform("image", sf::Shader::CurrentTexture);
    tonemap_shader.setUniform("beta", TONE_SOFTENING);
    tonemap_shader.setUniform("desaturation", HIGHLIGHT_DESATURATION);
    std::vector<sf::Glsl::Vec3> density_stops;
    for (const sf::Color color : DENSITY_STOPS) {
        density_stops.emplace_back(color.r / 255.f, color.g / 255.f, color.b / 255.f);
    }
    tonemap_shader.setUniformArray("density_stops", density_stops.data(), density_stops.size());
}

void HdrRenderer::resize(sf::Vector2u size) {
    using Constants::Graphics::EXPOSURE_BLOCK_PX;
    if (size.x == 0 || size.y == 0) {
        return;
    }
    resize_float_target(deposits, size);
    for (sf::RenderTexture& target : trails) {
        resize_float_target(target, size);
    }
    const sf::Vector2u block_count((size.x + EXPOSURE_BLOCK_PX - 1) / EXPOSURE_BLOCK_PX,
            (size.y + EXPOSURE_BLOCK_PX - 1) / EXPOSURE_BLOCK_PX);
    resize_float_target(blocks, block_count);
    block_points.resize(block_count.x * block_count.y);
    reset_trails();
    snap_exposure = true;
}

void HdrRenderer::reset_trails() {
    trails_cleared = true;
}

void HdrRenderer::set_groups(std::span<const RenderGroup> groups) {
    const std::vector<sf::Glsl::Vec4> table = style_table(groups);
    deposit_shader.setUniformArray("group_style", table.data(), table.size());
    velocity_deposit_shader.setUniformArray("group_style", table.data(), table.size());
}

void HdrRenderer::draw(sf::RenderTarget& window, const sf::VertexArray& bodies,
        const Frame& frame) {
    using ColorMode = Config::Graphics::ColorMode;
    using namespace Constants::Graphics;
    const float ev_scale = static_cast<float>(std::exp2(frame.exposure_ev));

    sf::Shader& shader = frame.color_mode == ColorMode::SPEED
                                 || frame.color_mode == ColorMode::DOPPLER
                                 ? velocity_deposit_shader
                                 : deposit_shader;
    shader.setUniform("color_mode", static_cast<int>(frame.color_mode));
    if (&shader == &velocity_deposit_shader) {
        shader.setUniform("velocity_scale",
                std::max(frame.velocity_scale, static_cast<float>(VELOCITY_SCALE_MIN)));
    }
    // A tent of 2x2 pixels for bodies 1 px wide, else a disc
    const bool tent = frame.body_diameter_px <= 1;
    const float point_size = tent ? 2.f : frame.body_diameter_px + 2.f;
    shader.setUniform("kernel", tent ? 1 : 2);
    shader.setUniform("point_size", point_size);
    shader.setUniform("disc_radius", frame.body_diameter_px / 2.f);
    sf::RenderStates deposit_states(additive);
    deposit_states.shader = &shader;
    deposits.clear(sf::Color::Transparent);
    deposits.draw(bodies, deposit_states);
    deposits.display();

    image = &deposits;
    if (frame.trails_fade_s > 0.0) {
        draw_trails(frame, ev_scale);
    }
    else {
        trails_shown = false;
    }
    measure_exposure(frame);

    const float locked_white = frame.locked_white.value_or(0.f);
    tonemap_shader.setUniform("exposure", exposure[exposure_index].getTexture());
    tonemap_shader.setUniform("locked_white", locked_white);
    tonemap_shader.setUniform("ev_scale", ev_scale);
    tonemap_shader.setUniform("density", frame.color_mode == ColorMode::DENSITY);
    tonemap_shader.setUniform("frame", static_cast<float>(frame_count++ % 1024));
    sf::RenderStates tonemap_states(additive);
    tonemap_states.shader = &tonemap_shader;
    window.draw(sf::Sprite(image->getTexture()), tonemap_states);
}

// trail = max(faded previous trail, deposits), per channel. They start anew when turned on.
void HdrRenderer::draw_trails(const Frame& frame, float ev_scale) {
    sf::RenderTexture& previous = trails[trail_index];
    if (!trails_shown || trails_cleared) {
        previous.clear(sf::Color::Transparent);
        previous.display();
    }
    trails_shown = true;
    trails_cleared = false;
    trail_index = 1 - trail_index;
    sf::RenderTexture& next = trails[trail_index];
    trail_shader.setUniform("exposure", exposure[exposure_index].getTexture());
    trail_shader.setUniform("locked_white", frame.locked_white.value_or(0.f));
    trail_shader.setUniform("ev_scale", ev_scale);
    trail_shader.setUniform("fade",
            frame.paused ? 0.f : static_cast<float>(frame.frame_time_s / frame.trails_fade_s));
    next.draw(sf::Sprite(previous.getTexture()), replacing(trail_shader));
    next.display();
    image = &next;
}

// The auto white point of the image, all on the GPU: block maxima, their histogram, then the
// white point, followed from the previous one
void HdrRenderer::measure_exposure(const Frame& frame) {
    using namespace Constants::Graphics;
    reduce_shader.setUniform("image", image->getTexture());
    reduce_shader.setUniform("image_size", sf::Glsl::Ivec2(sf::Vector2i(image->getSize())));
    blocks.draw(sf::RectangleShape(sf::Vector2f(blocks.getSize())), replacing(reduce_shader));
    blocks.display();

    histogram_shader.setUniform("blocks_size", sf::Glsl::Ivec2(sf::Vector2i(blocks.getSize())));
    sf::RenderStates histogram_states(additive);
    histogram_states.shader = &histogram_shader;
    histogram.clear(sf::Color::Transparent);
    histogram.draw(block_points, histogram_states);
    histogram.display();

    const double adapt_s = std::chrono::duration<double>(EXPOSURE_ADAPT_TIME).count();
    white_shader.setUniform("previous", exposure[exposure_index].getTexture());
    white_shader.setUniform("adapt",
            static_cast<float>(1.0 - std::exp(-frame.frame_time_s / adapt_s)));
    white_shader.setUniform("snap", snap_exposure);
    exposure_index = 1 - exposure_index;
    exposure[exposure_index].draw(sf::RectangleShape({1.f, 1.f}), replacing(white_shader));
    exposure[exposure_index].display();
    snap_exposure = false;
}

float HdrRenderer::read_white_point() {
    return read_pixels(exposure[exposure_index], {{0, 0}, {1, 1}}).at(0);
}

std::vector<float> HdrRenderer::read_image(sf::IntRect rect) {
    return read_pixels(*image, rect);
}
