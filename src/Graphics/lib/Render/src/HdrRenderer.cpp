#include "Render/HdrRenderer.hpp"

#include <GL/gl.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <string>

#include "Constants/Constants.hpp"
#include "Render/BodyVertex.hpp"
#include "Render/Colors.hpp"
#include "Render/FloatTarget.hpp"
#include "Render/ToneMath.hpp"
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
out vec3 deposit;    // HDR, so not gl_FrontColor, which is clamped to [0, 1]
out float selected;  // 1 for a highlighted body, which the tonemap shows in the selection colour
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
        selected = 0.0;
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
    selected = float(flags & 1);
    if (selected > 0.0 && color_mode != 4) {
        c = select_color;
    }
    deposit = c * brightness;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_PointSize = point_size;
}
)glsl";
constexpr std::string_view deposit_fragment_shader = R"glsl(
uniform int kernel;         // 1: tent (bodies 1 px wide), 2: disc
uniform float point_size;   // the points' size in pixels: 2 for the tent, the disc's diameter + 2
uniform float disc_radius;  // kernel 2: the body's radius in pixels
in vec3 deposit;
in float selected;

void main() {
    float w;
    if (kernel == 1) {  // bilinear weights of the 2x2 pixels around the body: they sum to 1
        vec2 d = abs(gl_PointCoord - 0.5) * point_size;
        w = max(0.0, 1.0 - d.x) * max(0.0, 1.0 - d.y);
    }
    else {  // anti-aliased disc
        w = clamp(disc_radius + 0.5 - length(gl_PointCoord - 0.5) * point_size, 0.0, 1.0);
    }
    gl_FragColor = vec4(deposit, selected) * w;  // alpha: how much of the pixel the selection covers
}
)glsl";
// A trail segment, the line from where a body was last frame, as a rectangle along it as wide as
// the body's light reaches: the fragment shader gives it the body's profile across the line, so
// the segment is the body swept along it. Its round ends are the body's own splats, this frame's
// and last frame's (in the trails), so the rectangle stops at the line's ends: fragments are most
// of what trails cost. A disc's segment also carries the light that travels with it, as a
// planet's moons: the light at its head beyond the brightest just outside it (not a crowd's), at
// most what its tail held last frame (not a cluster it just entered). Planets and their moons
// then leave even trails, not beads where their light added up each frame.
constexpr std::string_view segment_geometry_shader = R"glsl(
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
uniform vec2 target_size;      // pixels
uniform float reach;           // how far from its centre the body's light reaches, pixels
uniform bool carries;          // discs: the segment carries the light travelling with the body
uniform float disc_radius;     // pixels
uniform sampler2D deposits;    // this frame's light
uniform sampler2D old_trails;  // last frame's trails
in vec3 deposit[];
in float selected[];
flat out vec3 segment_deposit;
flat out float segment_selected;
flat out vec4 ends;            // the line's, in gl_FragCoord's pixels
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// The luminance of `light` at p's pixel, clamped into the window
float luminance_at(sampler2D light, vec2 p) {
    ivec2 pixel = clamp(ivec2(floor(p)), ivec2(0), ivec2(target_size) - 1);
    return dot(texelFetch(light, pixel, 0).rgb, LUMA);
}

// A disc's own luminance at p's pixel: its peak `own` times its coverage of that pixel
float own_at(vec2 p, float own) {
    return own * clamp(disc_radius + 0.5 - length(p - floor(p) - 0.5), 0.0, 1.0);
}

void main() {
    vec2 a = (gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w * 0.5 + 0.5) * target_size;
    vec2 b = (gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w * 0.5 + 0.5) * target_size;
    float own = dot(deposit[1], LUMA);
    float carried = 1.0;
    if (carries && own > 0.0) {
        float r = reach + 1.0;  // past its own light
        float around = max(max(luminance_at(deposits, b + vec2(r, 0.0)),
                                   luminance_at(deposits, b - vec2(r, 0.0))),
                max(luminance_at(deposits, b + vec2(0.0, r)),
                        luminance_at(deposits, b - vec2(0.0, r))));
        carried = max(1.0, min((luminance_at(deposits, b) - around) / own_at(b, own),
                                   luminance_at(old_trails, a) / own_at(a, own)));
    }
    vec2 along = b != a ? normalize(b - a) : vec2(1.0, 0.0);
    vec2 across = vec2(-along.y, along.x) * reach;
    vec2 corners[4] = vec2[4](a - across, a + across, b - across, b + across);
    for (int k = 0; k < 4; k++) {
        gl_Position = vec4(corners[k] / target_size * 2.0 - 1.0, 0.0, 1.0);
        segment_deposit = deposit[1] * carried;
        segment_selected = selected[1];
        ends = vec4(a, b);
        EmitVertex();
    }
    EndPrimitive();
}
)glsl";
// The body's profile at the pixel's distance from the segment: its tent across the line (bodies
// 1 px wide) or its anti-aliased disc
constexpr std::string_view segment_fragment_shader = R"glsl(
uniform int kernel;         // 1: tent, 2: disc
uniform float disc_radius;  // kernel 2: the body's radius in pixels
flat in vec3 segment_deposit;
flat in float segment_selected;
flat in vec4 ends;

void main() {
    vec2 p = gl_FragCoord.xy - ends.xy;
    vec2 line = ends.zw - ends.xy;
    float d = length(p - line * clamp(dot(p, line) / max(dot(line, line), 1e-6), 0.0, 1.0));
    float w = kernel == 1 ? max(0.0, 1.0 - d) : clamp(disc_radius + 0.5 - d, 0.0, 1.0);
    if (w <= 0.0) {
        discard;
    }
    gl_FragColor = vec4(segment_deposit, segment_selected) * w;
}
)glsl";
// The phosphor, drawn as a sprite of the previous trails. A pixel holds the light it was given
// last and that light's age (alpha, seconds), or -1 - age where it showed the selection. It is
// shown at 1 - age / fade time of that light's display level (the selection's: 1), so that every
// trail fades out in the fade time. This frame's light takes the pixel if it is shown at least as
// bright, or if it holds TRAIL_TAKEOVER_MIN of a body's light of another colour and that body is
// shown at least as bright: a trail laid over another's replaces it where it half covers pixels,
// laid over it as far as it covers them (a body's light covers a pixel), while a trail overlapping
// itself keeps its brightest light.
constexpr std::string_view trail_fragment_shader = R"glsl(
uniform sampler2D trails;
uniform sampler2D deposits;   // this frame's light
uniform sampler2D swept;      // this frame's trail segments
uniform sampler2D exposure;   // 1x1: the auto white point
uniform float locked_white;   // > 0 while locked: used instead
uniform float ev_scale;       // 2^EV
uniform float beta;           // tone curve softening, bodies/px
uniform float frame_time;     // seconds since the last frame, 0 paused
uniform float fade_time;      // seconds
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec4 old = texture2D(trails, uv);
    vec4 light = max(texture2D(deposits, uv), texture2D(swept, uv));
    float white = (locked_white > 0.0 ? locked_white
                                      : texelFetch(exposure, ivec2(0, 0), 0).r) / ev_scale;
    float A = asinh(max(white, 1e-6) / beta);
    bool old_selected = old.a < 0.0;
    float age = (old_selected ? -1.0 - old.a : old.a) + frame_time;
    float old_level = age < fade_time
            ? (old_selected ? 1.0 : min(asinh(dot(old.rgb, LUMA) / beta) / A, 1.0))
                      * (1.0 - age / fade_time)
            : 0.0;
    float I = dot(light.rgb, LUMA);
    bool selected = light.a * SELECT_COVERAGE_GAIN >= 1.0;
    float level = selected ? 1.0 : min(asinh(I / beta) / A, 1.0);
    vec3 hue = light.rgb / max(max(light.r, max(light.g, light.b)), 1e-30);
    vec3 old_hue = old.rgb / max(max(old.r, max(old.g, old.b)), 1e-30);
    bool other = any(greaterThan(abs(hue - old_hue), vec3(TRAIL_HUE_TOLERANCE)));
    if (other && I >= TRAIL_TAKEOVER_MIN) {
        level = max(level, min(asinh(max(I, 1.0) / beta) / A, 1.0));
    }
    if (I > 0.0 && level >= old_level) {
        // Another colour's light as it shows now, under what this light leaves uncovered
        vec3 under = other && !old_selected && old_level > 0.0
                ? old.rgb * (beta * sinh(old_level * A) / dot(old.rgb, LUMA) * max(0.0, 1.0 - I))
                : vec3(0.0);
        gl_FragColor = vec4(light.rgb + under, selected ? -1.0 : 0.0);
    }
    else {
        gl_FragColor = old_level > 0.0 ? vec4(old.rgb, old_selected ? -1.0 - age : age) : vec4(0.0);
    }
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
// brightness turning white near it, or in Density mode the luminance through the colormap, and
// the selected bodies in the selection colour at full brightness. It covers the grid as much as
// it is bright: entirely from the grid's own brightness up.
constexpr std::string_view tonemap_fragment_shader = R"glsl(
uniform sampler2D image;
uniform bool trails;                        // the image is the trails, see the trail pass
uniform float fade_time;                    // trails: seconds
uniform sampler2D highlights;               // this frame's light: alpha, the selection's coverage
uniform vec3 select_color;                  // display
uniform sampler2D exposure;
uniform float locked_white;
uniform float ev_scale;
uniform float beta;
uniform float desaturation;                 // top share of the display range blended to white
uniform bool density;                       // Density mode
uniform vec3 density_stops[DENSITY_STOPS];  // display (sRGB-encoded) values
uniform bool bloom;
uniform sampler2D bloom_image;              // half resolution, smooth
uniform float bloom_strength;
uniform float frame;                        // frame counter, moves the dither
uniform float grid_level;                   // the grid's display level
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

vec3 encode(vec3 c) {  // linear to sRGB
    return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

float noise(vec2 p) {  // interleaved gradient noise, in [0, 1)
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec4 pixel = texture2D(image, uv);
    vec3 c = pixel.rgb;
    float white = (locked_white > 0.0 ? locked_white
                                      : texelFetch(exposure, ivec2(0, 0), 0).r) / ev_scale;
    float A = asinh(max(white, 1e-6) / beta);
    float shown = 1.0;  // a trail's share of its display level, by its age
    bool selected = false;
    if (trails) {  // the light shown at that share, keeping its hue
        selected = pixel.a < 0.0;
        shown = max(0.0, 1.0 - (selected ? -1.0 - pixel.a : pixel.a) / fade_time);
        float I = dot(c, LUMA);
        c *= I > 0.0 ? beta * sinh(min(asinh(I / beta) / A, 1.0) * shown * A) / I : 0.0;
    }
    if (bloom) {
        c += bloom_strength * texture2D(bloom_image, uv).rgb;
    }
    float I = dot(c, LUMA);
    float d = I > 0.0 ? asinh(I / beta) / A : 0.0;
    float level = min(d, 1.0);
    vec3 color = vec3(0.0);  // black stays black: no dither, no colormap
    if (selected || level > 0.0) {
        if (selected) {  // a selected body's trail
            color = select_color * shown;
        }
        else if (density) {
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
    color = mix(color, select_color,
            min(texture2D(highlights, uv).a * SELECT_COVERAGE_GAIN, 1.0));
    gl_FragColor = vec4(color, clamp(max(color.r, max(color.g, color.b)) / grid_level, 0.0, 1.0));
}
)glsl";

// Bloom, into the first, half-size level: the image's 4x4 box average around each of its pixels,
// 4 bilinear taps at the centres of its quarters (the image is filtered smoothly for this pass),
// weighted by a soft threshold relative to the shown white point
constexpr std::string_view bloom_prefilter_fragment_shader = R"glsl(
uniform sampler2D image;
uniform vec2 image_texel;  // in texture coordinates
uniform sampler2D exposure;
uniform float locked_white;
uniform float ev_scale;
uniform float threshold;  // of the white point
uniform float knee;       // over which the weight rises to 1
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

void main() {
    // Half-size pixel x's first quarter is centred at 2x, between image pixels 2x - 1 and 2x
    vec2 uv = floor(gl_FragCoord.xy) * 2.0 * image_texel;
    vec3 c = (texture2D(image, uv).rgb + texture2D(image, uv + vec2(2.0, 0.0) * image_texel).rgb
                     + texture2D(image, uv + vec2(0.0, 2.0) * image_texel).rgb
                     + texture2D(image, uv + 2.0 * image_texel).rgb)
             * 0.25;
    float white = (locked_white > 0.0 ? locked_white
                                      : texelFetch(exposure, ivec2(0, 0), 0).r) / ev_scale;
    float k = clamp((dot(c, LUMA) / max(white, 1e-6) - threshold) / knee, 0.0, 1.0);
    gl_FragColor = vec4(c * k * k, 0.0);
}
)glsl";
// The next smaller level: Jimenez's 13 taps ("Next Generation Post Processing in Call of Duty:
// Advanced Warfare", SIGGRAPH 2014), bilinear, which keep a single bright pixel from flickering
constexpr std::string_view bloom_down_fragment_shader = R"glsl(
uniform sampler2D source;
uniform vec2 texel;        // of the source, in texture coordinates
uniform vec2 target_size;  // pixels

vec3 tap(vec2 uv, float x, float y) {
    return texture2D(source, uv + vec2(x, y) * texel).rgb;
}

void main() {
    vec2 uv = gl_FragCoord.xy / target_size;
    vec3 c = tap(uv, 0.0, 0.0) * 0.125;
    c += (tap(uv, -2.0, -2.0) + tap(uv, 2.0, -2.0) + tap(uv, -2.0, 2.0) + tap(uv, 2.0, 2.0))
         * 0.03125;
    c += (tap(uv, 0.0, -2.0) + tap(uv, -2.0, 0.0) + tap(uv, 2.0, 0.0) + tap(uv, 0.0, 2.0))
         * 0.0625;
    c += (tap(uv, -1.0, -1.0) + tap(uv, 1.0, -1.0) + tap(uv, -1.0, 1.0) + tap(uv, 1.0, 1.0))
         * 0.125;
    gl_FragColor = vec4(c, 0.0);
}
)glsl";
// A smaller level blurred by a 3x3 tent, added into the next larger one
constexpr std::string_view bloom_up_fragment_shader = R"glsl(
uniform sampler2D source;
uniform vec2 texel;
uniform vec2 target_size;

vec3 tap(vec2 uv, float x, float y) {
    return texture2D(source, uv + vec2(x, y) * texel).rgb;
}

void main() {
    vec2 uv = gl_FragCoord.xy / target_size;
    vec3 c = tap(uv, 0.0, 0.0) * 4.0;
    c += (tap(uv, 0.0, -1.0) + tap(uv, -1.0, 0.0) + tap(uv, 1.0, 0.0) + tap(uv, 0.0, 1.0)) * 2.0;
    c += tap(uv, -1.0, -1.0) + tap(uv, 1.0, -1.0) + tap(uv, -1.0, 1.0) + tap(uv, 1.0, 1.0);
    gl_FragColor = vec4(c / 16.0, 0.0);
}
)glsl";

// A shader's source: GLSL 1.50 (OpenGL 3.2, for the geometry shader) with the compatibility
// profile's built-ins, which SFML's drawing feeds, the constants it shares with Constants.hpp,
// `defines`, then `body`
std::string shader_source(std::string_view body, std::string_view defines = "") {
    using namespace Constants::Graphics;
    return fmt::format("#version 150 compatibility\n"
                       "{}"
                       "#define MAX_RENDER_GROUPS {}\n"
                       "#define MAX_RAMP_STOPS {}\n"
                       "#define DENSITY_STOPS {}\n"
                       "#define BLOCK {}\n"
                       "#define BINS {}\n"
                       "#define LOG2_MIN {:.6f}\n"
                       "#define BINS_PER_OCTAVE {:.6f}\n"
                       "#define SELECT_COVERAGE_GAIN {:.6f}\n"
                       "#define TRAIL_TAKEOVER_MIN {:.6f}\n"
                       "#define TRAIL_HUE_TOLERANCE {:.6f}\n",
                   defines, MAX_RENDER_GROUPS, MAX_RAMP_STOPS, std::size(DENSITY_STOPS),
                   EXPOSURE_BLOCK_PX, EXPOSURE_HISTOGRAM_BINS, EXPOSURE_LOG2_MIN,
                   EXPOSURE_BINS_PER_OCTAVE, SELECT_COVERAGE_GAIN, TRAIL_TAKEOVER_MIN,
                   TRAIL_HUE_TOLERANCE)
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
// Trail segments keep the brightest light: trails that cross don't add up
const sf::BlendMode brightest(sf::BlendMode::Factor::One, sf::BlendMode::Factor::One,
        sf::BlendMode::Equation::Max);
// The tone-mapped image over the window, as much as its alpha covers it, keeping the window's alpha
const sf::BlendMode over(sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha,
        sf::BlendMode::Equation::Add, sf::BlendMode::Factor::Zero, sf::BlendMode::Factor::One,
        sf::BlendMode::Equation::Add);

// `shader` drawn without blending
sf::RenderStates replacing(const sf::Shader& shader) {
    sf::RenderStates states(sf::BlendNone);
    states.shader = &shader;
    return states;
}
}  // namespace


void append_trail_segments(const sf::VertexArray& bodies,
        const std::array<uint8_t, 256>& group_visible, std::vector<sf::Vector2f>& previous,
        sf::VertexArray& segments) {
    using namespace Constants::Graphics;
    assert(previous.size() == bodies.getVertexCount());
    // NaN and infinities fail too
    const auto guarded = [](sf::Vector2f pos) {
        return std::abs(pos.x) <= SEGMENT_GUARD_PX && std::abs(pos.y) <= SEGMENT_GUARD_PX;
    };
    for (size_t i = 0; i < previous.size(); i++) {
        const sf::Vertex& vertex = bodies[i];
        const sf::Vector2f from = previous[i];
        previous[i] = vertex.position;
        const float length_squared = (vertex.position - from).lengthSquared();
        if (group_visible[BodyVertex::group(vertex)] && guarded(from) && guarded(vertex.position)
                && length_squared >= SEGMENT_MIN_PX * SEGMENT_MIN_PX
                && length_squared <= SEGMENT_MAX_PX * SEGMENT_MAX_PX) {
            segments.append({from, vertex.color, vertex.texCoords});
            segments.append(vertex);
        }
    }
}


std::optional<float> velocity_quantile(const sf::VertexArray& bodies,
        const std::vector<uint8_t>& group_visible, sf::Vector2f window_size, bool line_of_sight,
        std::vector<float>& samples) {
    using namespace Constants::Graphics;
    const uint64_t n = bodies.getVertexCount();
    const uint64_t count = std::min<uint64_t>(n, VELOCITY_SCALE_SAMPLES);
    samples.clear();
    for (uint64_t k = 0; k < count; k++) {
        const sf::Vertex& vertex = bodies[k * n / count];
        const sf::Vector2f pos = vertex.position;
        if (group_visible[BodyVertex::group(vertex)] && pos.x >= 0.f && pos.y >= 0.f
                && pos.x < window_size.x && pos.y < window_size.y) {
            const float v =
                    line_of_sight ? std::abs(vertex.texCoords.y) : vertex.texCoords.length();
            if (std::isfinite(v)) {
                samples.push_back(v);
            }
        }
    }
    if (samples.size() < VELOCITY_SCALE_MIN_SAMPLES) {
        return std::nullopt;
    }
    return ToneMath::quantile(samples, VELOCITY_SCALE_QUANTILE);
}


HdrRenderer::HdrRenderer(sf::Vector2u size) {
    using namespace Constants::Graphics;
    if (!sf::Shader::isGeometryAvailable()) {
        throw std::runtime_error("HDR rendering needs OpenGL 3.2 (geometry shaders)");
    }
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE);  // gl_PointCoord is undefined without it (compatibility profile)
    const std::string vertex = shader_source(deposit_vertex_shader);
    const std::string velocity_vertex = shader_source(deposit_vertex_shader, "#define VELOCITY\n");
    const std::string geometry = shader_source(segment_geometry_shader);
    if (!deposit_shader.loadFromMemory(vertex, shader_source(deposit_fragment_shader))
            || !velocity_deposit_shader.loadFromMemory(velocity_vertex,
                    shader_source(deposit_fragment_shader))
            || !segment_shader.loadFromMemory(vertex, geometry,
                    shader_source(segment_fragment_shader))
            || !velocity_segment_shader.loadFromMemory(velocity_vertex, geometry,
                    shader_source(segment_fragment_shader))
            || !trail_shader.loadFromMemory(shader_source(trail_fragment_shader),
                    sf::Shader::Type::Fragment)
            || !reduce_shader.loadFromMemory(shader_source(reduce_fragment_shader),
                    sf::Shader::Type::Fragment)
            || !histogram_shader.loadFromMemory(shader_source(histogram_vertex_shader),
                    shader_source(histogram_fragment_shader))
            || !white_shader.loadFromMemory(shader_source(white_fragment_shader),
                    sf::Shader::Type::Fragment)
            || !tonemap_shader.loadFromMemory(shader_source(tonemap_fragment_shader),
                    sf::Shader::Type::Fragment)
            || !bloom_prefilter_shader.loadFromMemory(
                    shader_source(bloom_prefilter_fragment_shader), sf::Shader::Type::Fragment)
            || !bloom_down_shader.loadFromMemory(shader_source(bloom_down_fragment_shader),
                    sf::Shader::Type::Fragment)
            || !bloom_up_shader.loadFromMemory(shader_source(bloom_up_fragment_shader),
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
    for (sf::Shader* shader : light_shaders()) {
        shader->setUniform("select_color", select_color);
    }
    const auto speed_stops = linear_stops(SPEED_STOPS);
    const auto doppler_stops = linear_stops(DOPPLER_STOPS);
    for (sf::Shader* shader : {&velocity_deposit_shader, &velocity_segment_shader}) {
        shader->setUniformArray("speed_stops", speed_stops.data(), speed_stops.size());
        shader->setUniform("speed_stop_count", static_cast<int>(std::size(SPEED_STOPS)));
        shader->setUniformArray("doppler_stops", doppler_stops.data(), doppler_stops.size());
        shader->setUniform("doppler_stop_count", static_cast<int>(std::size(DOPPLER_STOPS)));
    }
    trail_shader.setUniform("trails", sf::Shader::CurrentTexture);
    trail_shader.setUniform("deposits", deposits.getTexture());
    trail_shader.setUniform("swept", swept.getTexture());
    for (sf::Shader* shader : {&segment_shader, &velocity_segment_shader}) {
        shader->setUniform("deposits", deposits.getTexture());
    }
    histogram_shader.setUniform("blocks", blocks.getTexture());
    white_shader.setUniform("histogram", histogram.getTexture());
    white_shader.setUniform("quantile", AUTO_WHITE_QUANTILE);
    white_shader.setUniform("min_white", AUTO_WHITE_MIN);
    tonemap_shader.setUniform("image", sf::Shader::CurrentTexture);
    tonemap_shader.setUniform("highlights", deposits.getTexture());
    tonemap_shader.setUniform("select_color",
            sf::Glsl::Vec3(SELECT_COLOR.r / 255.f, SELECT_COLOR.g / 255.f, SELECT_COLOR.b / 255.f));
    tonemap_shader.setUniform("desaturation", HIGHLIGHT_DESATURATION);
    std::vector<sf::Glsl::Vec3> density_stops;
    for (const sf::Color color : DENSITY_STOPS) {
        density_stops.emplace_back(color.r / 255.f, color.g / 255.f, color.b / 255.f);
    }
    tonemap_shader.setUniformArray("density_stops", density_stops.data(), density_stops.size());
    tonemap_shader.setUniform("bloom_image", bloom_levels[0].getTexture());
    tonemap_shader.setUniform("bloom_strength", BLOOM_STRENGTH);
    // The grid's display level: its lines' grey over the black background
    const float grid_level =
            GRID_COLOR.a / 255.f * std::max({GRID_COLOR.r, GRID_COLOR.g, GRID_COLOR.b}) / 255.f;
    tonemap_shader.setUniform("grid_level", grid_level);
    bloom_prefilter_shader.setUniform("threshold", BLOOM_THRESHOLD);
    bloom_prefilter_shader.setUniform("knee", BLOOM_KNEE);
}

void HdrRenderer::resize(sf::Vector2u size) {
    using Constants::Graphics::EXPOSURE_BLOCK_PX;
    if (size.x == 0 || size.y == 0) {
        return;
    }
    resize_float_target(deposits, size);
    resize_float_target(swept, size);
    for (sf::RenderTexture& target : trails) {
        resize_float_target(target, size);
    }
    const sf::Vector2u block_count((size.x + EXPOSURE_BLOCK_PX - 1) / EXPOSURE_BLOCK_PX,
            (size.y + EXPOSURE_BLOCK_PX - 1) / EXPOSURE_BLOCK_PX);
    resize_float_target(blocks, block_count);
    block_points.resize(block_count.x * block_count.y);
    sf::Vector2u level_size = size;
    for (sf::RenderTexture& level : bloom_levels) {
        level_size = {(level_size.x + 1) / 2, (level_size.y + 1) / 2};
        resize_float_target(level, level_size);
        level.setSmooth(true);  // sampled bilinearly
    }
    reset_trails();
    snap_exposure = true;
}

void HdrRenderer::reset_trails() {
    trails_cleared = true;
}

void HdrRenderer::set_groups(std::span<const RenderGroup> groups) {
    const std::vector<sf::Glsl::Vec4> table = style_table(groups);
    for (sf::Shader* shader : light_shaders()) {
        shader->setUniformArray("group_style", table.data(), table.size());
    }
    for (size_t k = 0; k < group_drawn.size(); k++) {
        group_drawn[k] = k < groups.size() && groups[k].visible && groups[k].brightness > 0.f;
    }
}

void HdrRenderer::draw(sf::RenderTarget& window, const sf::VertexArray& bodies,
        const Frame& frame) {
    using ColorMode = Config::Graphics::ColorMode;
    using namespace Constants::Graphics;
    const float ev_scale = static_cast<float>(std::exp2(frame.exposure_ev));
    for (sf::Shader* shader : {&trail_shader, &tonemap_shader}) {
        shader->setUniform("beta", TONE_SOFTENING / frame.stretch);
    }

    const bool velocities =
            frame.color_mode == ColorMode::SPEED || frame.color_mode == ColorMode::DOPPLER;
    sf::Shader& point_shader = velocities ? velocity_deposit_shader : deposit_shader;
    sf::Shader& line_shader = velocities ? velocity_segment_shader : segment_shader;
    // A tent of 2x2 pixels for bodies 1 px wide, else a disc
    const bool tent = frame.body_diameter_px <= 1;
    for (sf::Shader* s : {&point_shader, &line_shader}) {
        s->setUniform("color_mode", static_cast<int>(frame.color_mode));
        if (velocities) {
            s->setUniform("velocity_scale",
                    std::max(frame.velocity_scale, static_cast<float>(VELOCITY_SCALE_MIN)));
        }
        s->setUniform("kernel", tent ? 1 : 2);
        s->setUniform("disc_radius", frame.body_diameter_px / 2.f);
    }
    point_shader.setUniform("point_size", tent ? 2.f : frame.body_diameter_px + 2.f);
    sf::RenderStates deposit_states(additive);
    deposit_states.shader = &point_shader;
    deposits.clear(sf::Color::Transparent);
    deposits.draw(bodies, deposit_states);
    deposits.display();
    // This frame's light, trails aside, sets the exposure and glows
    measure_exposure(frame);
    const float locked_white = frame.locked_white.value_or(0.f);
    if (frame.bloom) {
        draw_bloom(locked_white, ev_scale);
    }

    image = &deposits;
    update_segments(bodies, frame);
    if (frame.trails_fade_s > 0.0) {
        swept.clear(sf::Color::Transparent);
        if (segments.getVertexCount() > 0) {
            line_shader.setUniform("target_size",
                    sf::Glsl::Vec2(sf::Vector2f(deposits.getSize())));
            line_shader.setUniform("reach", tent ? 1.f : frame.body_diameter_px / 2.f + 0.5f);
            line_shader.setUniform("carries", !tent);
            line_shader.setUniform("old_trails", trails[trail_index].getTexture());
            sf::RenderStates segment_states(brightest);
            segment_states.shader = &line_shader;
            swept.draw(segments, segment_states);
        }
        swept.display();
        draw_trails(frame, ev_scale);
    }
    else {
        trails_shown = false;
    }
    tonemap_shader.setUniform("trails", image != &deposits);
    tonemap_shader.setUniform("fade_time", static_cast<float>(frame.trails_fade_s));
    tonemap_shader.setUniform("bloom", frame.bloom);
    tonemap_shader.setUniform("exposure", exposure[exposure_index].getTexture());
    tonemap_shader.setUniform("locked_white", locked_white);
    tonemap_shader.setUniform("ev_scale", ev_scale);
    tonemap_shader.setUniform("density", frame.color_mode == ColorMode::DENSITY);
    tonemap_shader.setUniform("frame", static_cast<float>(frame_count++ % 1024));
    sf::RenderStates tonemap_states(over);
    tonemap_states.shader = &tonemap_shader;
    window.draw(sf::Sprite(image->getTexture()), tonemap_states);
}

// While trails run, the segments from where the bodies were last frame: none on the first frame
// of trails, or after they were reset (the view jumped), or while paused
void HdrRenderer::update_segments(const sf::VertexArray& bodies, const Frame& frame) {
    segments.clear();
    if (frame.trails_fade_s <= 0.0 || trails_cleared) {
        previous_valid = false;
    }
    if (frame.trails_fade_s <= 0.0 || frame.paused) {
        return;
    }
    if (previous_valid && previous.size() == bodies.getVertexCount()) {
        append_trail_segments(bodies, group_drawn, previous, segments);
    }
    else {
        previous.resize(bodies.getVertexCount());
        for (size_t i = 0; i < previous.size(); i++) {
            previous[i] = bodies[i].position;
        }
    }
    previous_valid = true;
}

// The previous trails, aged, taken where this frame's light or trail segments take over, see the
// trail shader. They start anew when turned on.
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
    trail_shader.setUniform("frame_time",
            frame.paused ? 0.f : static_cast<float>(frame.frame_time_s));
    trail_shader.setUniform("fade_time", static_cast<float>(frame.trails_fade_s));
    next.draw(sf::Sprite(previous.getTexture()), replacing(trail_shader));
    next.display();
    image = &next;
}

// The auto white point of this frame's light, all on the GPU: block maxima, their histogram, then
// the white point, followed from the previous one
void HdrRenderer::measure_exposure(const Frame& frame) {
    using namespace Constants::Graphics;
    reduce_shader.setUniform("image", deposits.getTexture());
    reduce_shader.setUniform("image_size", sf::Glsl::Ivec2(sf::Vector2i(deposits.getSize())));
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

// The bloom pyramid: this frame's light above the threshold, downsampled level by level, then
// each level blurred into the next larger one, so that the first holds them all
void HdrRenderer::draw_bloom(float locked_white, float ev_scale) {
    const auto pass = [](sf::RenderTexture& target, const sf::Shader& shader,
                              const sf::BlendMode& blend) {
        sf::RenderStates states(blend);
        states.shader = &shader;
        target.draw(sf::RectangleShape(sf::Vector2f(target.getSize())), states);
        target.display();
    };
    const auto set_sizes = [](sf::Shader& shader, const sf::RenderTexture& source,
                                   const sf::RenderTexture& target) {
        const sf::Vector2f source_size(source.getSize());
        shader.setUniform("source", source.getTexture());
        shader.setUniform("texel", sf::Glsl::Vec2(1.f / source_size.x, 1.f / source_size.y));
        shader.setUniform("target_size", sf::Glsl::Vec2(sf::Vector2f(target.getSize())));
    };
    const sf::Vector2f image_size(deposits.getSize());
    bloom_prefilter_shader.setUniform("image", deposits.getTexture());
    bloom_prefilter_shader.setUniform("image_texel",
            sf::Glsl::Vec2(1.f / image_size.x, 1.f / image_size.y));
    bloom_prefilter_shader.setUniform("exposure", exposure[exposure_index].getTexture());
    bloom_prefilter_shader.setUniform("locked_white", locked_white);
    bloom_prefilter_shader.setUniform("ev_scale", ev_scale);
    deposits.setSmooth(true);  // the other passes sample it 1:1
    pass(bloom_levels[0], bloom_prefilter_shader, sf::BlendNone);
    deposits.setSmooth(false);
    for (size_t k = 1; k < bloom_levels.size(); k++) {
        set_sizes(bloom_down_shader, bloom_levels[k - 1], bloom_levels[k]);
        pass(bloom_levels[k], bloom_down_shader, sf::BlendNone);
    }
    for (size_t k = bloom_levels.size() - 1; k > 0; k--) {
        set_sizes(bloom_up_shader, bloom_levels[k], bloom_levels[k - 1]);
        pass(bloom_levels[k - 1], bloom_up_shader, additive);
    }
}

std::array<sf::Shader*, 4> HdrRenderer::light_shaders() {
    return {&deposit_shader, &velocity_deposit_shader, &segment_shader, &velocity_segment_shader};
}

float HdrRenderer::read_white_point() {
    return read_pixels(exposure[exposure_index], {{0, 0}, {1, 1}}).at(0);
}

std::vector<float> HdrRenderer::read_image(sf::IntRect rect) {
    return read_pixels(*image, rect);
}

std::vector<float> HdrRenderer::read_bloom(sf::IntRect rect) {
    return read_pixels(bloom_levels[0], rect);
}
