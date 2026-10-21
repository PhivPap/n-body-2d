#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Config/Config.hpp"
#include "Constants/Constants.hpp"
#include "Render/RenderGroups.hpp"
#include "SFML/Graphics.hpp"


// Trail segments for the bodies of drawn groups (`group_visible`, by group index) that moved
// SEGMENT_MIN_PX to SEGMENT_MAX_PX since their `previous` positions, both ends within
// SEGMENT_GUARD_PX of the window's origin: lines appended to `segments`, both ends with the body's
// colour and texture coordinates. `previous` then holds every body's position, hidden ones
// included, so a group shown again draws no line across where it went meanwhile.
void append_trail_segments(const sf::VertexArray& bodies,
        const std::array<uint8_t, 256>& group_visible, std::vector<sf::Vector2f>& previous,
        sf::VertexArray& segments);

// The VELOCITY_SCALE_QUANTILE of the speeds (with `line_of_sight`, of |v_y|) in the texture
// coordinates of up to VELOCITY_SCALE_SAMPLES bodies spread evenly over the indices, of those
// drawn (`group_visible`, by group) inside the window; nullopt with fewer than
// VELOCITY_SCALE_MIN_SAMPLES. `samples`: scratch space.
std::optional<float> velocity_quantile(const sf::VertexArray& bodies,
        const std::vector<uint8_t>& group_visible, sf::Vector2f window_size, bool line_of_sight,
        std::vector<float>& samples);

// The bodies as light, all on the GPU: added up in float buffers (a body is a 2x2 tent splat, or
// a disc when drawn bigger), measured for the auto exposure, kept as phosphor trails and tone
// mapped onto the window, with a glow (bloom) around the brightest light. The exposure is only
// read back when it is locked. While trails run, bodies that moved a pixel or more also sweep
// their light along the line from where they were (a geometry shader: OpenGL 3.2), so that their
// trails are as wide as they are and continuous at any speed.
class HdrRenderer {
public:
    // How a frame is drawn
    struct Frame {
        Config::Graphics::ColorMode color_mode;
        uint8_t body_diameter_px;
        double trails_fade_s;               // 0: no trails
        bool paused;                        // trails freeze, no segments
        double frame_time_s;                // wall time since the previous frame, capped
        float velocity_scale;               // m/s, Speed and Doppler
        double exposure_ev;
        std::optional<float> locked_white;  // bodies/px while the exposure is locked
        bool bloom;
    };

    // Throws std::runtime_error without float render targets or geometry shaders
    explicit HdrRenderer(sf::Vector2u size);
    // Also clears the trails and snaps the exposure; ignores a zero size (minimized)
    void resize(sf::Vector2u size);
    void reset_trails();  // and the next frame draws no trail segments
    void set_groups(std::span<const RenderGroup> groups);  // the light shaders' style table
    // `bodies`: points at window positions, encoded as BodyVertex describes, drawn as light over
    // `window` (the grid), which it covers as much as it is bright
    void draw(sf::RenderTarget& window, const sf::VertexArray& bodies, const Frame& frame);
    // Read back, which stalls: the auto white point (bodies/px), and a rectangle of the HDR image
    // the last frame showed, as read_pixels() gives it: the trails (alpha: their age, see the trail
    // shader), or without them the deposits (alpha: the selection's coverage)
    float read_white_point();
    std::vector<float> read_image(sf::IntRect rect);
    // The last frame's bloom at half resolution, read back likewise: what the tone curve adds,
    // times BLOOM_STRENGTH
    std::vector<float> read_bloom(sf::IntRect rect);

private:
    sf::RenderTexture deposits;                 // this frame's light
    sf::RenderTexture swept;                    // this frame's trail segments, which read it
    std::array<sf::RenderTexture, 2> trails;    // ping-pong: the previous frame's, this frame's
    sf::RenderTexture blocks;                   // the deposits' maximum luminance per block
    sf::RenderTexture histogram;                // of the blocks' maxima, in log2 bins
    std::array<sf::RenderTexture, 2> exposure;  // 1x1, ping-pong: the auto white point
    // Bloom: the light above the threshold, at 1/2 to 1/2^BLOOM_LEVELS of the size, then each
    // level with the blurred smaller ones added
    std::array<sf::RenderTexture, Constants::Graphics::BLOOM_LEVELS> bloom_levels;
    uint32_t trail_index = 0;                   // the trail buffer last drawn
    uint32_t exposure_index = 0;
    sf::RenderTexture* image = &deposits;       // what the last frame showed
    bool trails_shown = false;                  // last frame
    bool trails_cleared = true;                 // the trails start anew
    bool snap_exposure = true;                  // the white point jumps to the measured one
    uint32_t frame_count = 0;                   // moves the dither
    std::array<uint8_t, 256> group_drawn{};     // per group index (any 8-bit one)
    std::vector<sf::Vector2f> previous;         // the bodies' positions last frame
    bool previous_valid = false;                // in this view, while trails run
    sf::VertexArray segments{sf::PrimitiveType::Lines};
    sf::VertexArray block_points{sf::PrimitiveType::Points};  // one per block, for the histogram
    sf::Shader deposit_shader;           // bodies, as points
    sf::Shader velocity_deposit_shader;  // Speed and Doppler
    sf::Shader segment_shader;           // trail segments, as lines
    sf::Shader velocity_segment_shader;
    sf::Shader trail_shader;
    sf::Shader reduce_shader;
    sf::Shader histogram_shader;
    sf::Shader white_shader;
    sf::Shader tonemap_shader;
    sf::Shader bloom_prefilter_shader;
    sf::Shader bloom_down_shader;
    sf::Shader bloom_up_shader;

    std::array<sf::Shader*, 4> light_shaders();  // the four above
    void update_segments(const sf::VertexArray& bodies, const Frame& frame);
    void draw_trails(const Frame& frame, float ev_scale);
    void measure_exposure(const Frame& frame);
    void draw_bloom(float locked_white, float ev_scale);
};
