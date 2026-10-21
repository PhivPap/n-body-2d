#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Config/Config.hpp"
#include "Render/RenderGroups.hpp"
#include "SFML/Graphics.hpp"


// The bodies as light, all on the GPU: added up in float buffers (a body is a 2x2 tent splat, or
// a disc when drawn bigger), kept as phosphor trails, measured for the auto exposure and tone
// mapped onto the window. The exposure is only read back when it is locked.
class HdrRenderer {
public:
    // How a frame is drawn
    struct Frame {
        Config::Graphics::ColorMode color_mode;
        uint8_t body_diameter_px;
        double trails_fade_s;               // 0: no trails
        bool paused;                        // trails freeze
        double frame_time_s;                // wall time since the previous frame, capped
        float velocity_scale;               // m/s, Speed and Doppler
        double exposure_ev;
        std::optional<float> locked_white;  // bodies/px while the exposure is locked
        bool bloom;
    };

    // Throws std::runtime_error without float render targets
    explicit HdrRenderer(sf::Vector2u size);
    // Also clears the trails and snaps the exposure; ignores a zero size (minimized)
    void resize(sf::Vector2u size);
    void reset_trails();
    void set_groups(std::span<const RenderGroup> groups);  // the deposit shader's style table
    // `bodies`: points at window positions, encoded as BodyVertex describes, drawn as light added
    // onto `window`
    void draw(sf::RenderTarget& window, const sf::VertexArray& bodies, const Frame& frame);
    // Read back, which stalls: the auto white point (bodies/px), and a rectangle of the HDR image
    // the last frame showed (the trails, or without them the deposits), as read_pixels() gives it
    float read_white_point();
    std::vector<float> read_image(sf::IntRect rect);

private:
    sf::RenderTexture deposits;                 // this frame's light
    std::array<sf::RenderTexture, 2> trails;    // ping-pong: the previous frame's, this frame's
    sf::RenderTexture blocks;                   // the image's maximum luminance per block
    sf::RenderTexture histogram;                // of the blocks' maxima, in log2 bins
    std::array<sf::RenderTexture, 2> exposure;  // 1x1, ping-pong: the auto white point
    uint32_t trail_index = 0;                   // the trail buffer last drawn
    uint32_t exposure_index = 0;
    sf::RenderTexture* image = &deposits;       // what the last frame showed
    bool trails_shown = false;                  // last frame
    bool trails_cleared = true;                 // the trails start anew
    bool snap_exposure = true;                  // the white point jumps to the measured one
    uint32_t frame_count = 0;                   // moves the dither
    sf::VertexArray block_points{sf::PrimitiveType::Points};  // one per block, for the histogram
    sf::Shader deposit_shader;
    sf::Shader velocity_deposit_shader;  // Speed and Doppler
    sf::Shader trail_shader;
    sf::Shader reduce_shader;
    sf::Shader histogram_shader;
    sf::Shader white_shader;
    sf::Shader tonemap_shader;

    void draw_trails(const Frame& frame, float ev_scale);
    void measure_exposure(const Frame& frame);
};
