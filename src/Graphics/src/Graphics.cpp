#include "Graphics/Graphics.hpp"

#include <GL/gl.h>
#include <algorithm>

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Time.hpp"


constexpr std::string_view body_vertex_shader = R"glsl(
#version 130
uniform float pointDiameter;
void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_PointSize = pointDiameter;
    gl_FrontColor = gl_Color;  // Pass color if using
}
)glsl";
constexpr std::string_view body_fragment_shader = R"glsl(
#version 130
uniform float pointDiameter;
void main() {
    if (pointDiameter > 1.0) {
        vec2 coord = gl_PointCoord - vec2(0.5, 0.5);
        if (length(coord) > 0.5) discard;  // Circle clip (discard outer pixels)
    }
    gl_FragColor = gl_Color;  // Use vertex color or set fixed
}
)glsl";
constexpr std::string_view fade_fragment_shader = R"glsl(
#version 130
uniform sampler2D texture;
uniform vec4 decay;
void main() {
    gl_FragColor = texture2D(texture, gl_TexCoord[0].xy) - decay;
}
)glsl";


Graphics::Graphics(const Config::Graphics& graphics_cfg, const Bodies& bodies)
        : bodies(bodies), body_positions_cache(bodies.n),
          window(sf::VideoMode(sf::Vector2u(graphics_cfg.resolution)), "N-Body Sim"),
          vp(sf::Vector2f(graphics_cfg.resolution), graphics_cfg.pixel_scale),
          body_vertex_array(sf::PrimitiveType::Points, bodies.n),
          selector(bodies, body_positions_cache, body_vertex_array),
          show_grid(graphics_cfg.show_grid), follow_selected(graphics_cfg.follow_selected),
          selection_show(graphics_cfg.selection_show),
          selection_show_center_of_mass(graphics_cfg.selection_show_center_of_mass) {
    window.setFramerateLimit(graphics_cfg.fps);
    window.setVerticalSyncEnabled(graphics_cfg.vsync_enabled);

    for (uint64_t i = 0; i < body_vertex_array.getVertexCount(); i++) {
        body_vertex_array[i].color = Constants::Graphics::BODY_COLOR;
    }

    glEnable(GL_PROGRAM_POINT_SIZE);
    if (!body_shader.loadFromMemory(body_vertex_shader, body_fragment_shader)
            || !fade_shader.loadFromMemory(fade_fragment_shader, sf::Shader::Type::Fragment)) {
        throw std::runtime_error("Failed to load shaders");
    }
    body_shader.setUniform("pointDiameter", static_cast<float>(body_diameter_pixels));
    fade_shader.setUniform("texture", sf::Shader::CurrentTexture);
    panel_manager.register_panel(&config_panel, PanelManager::Position::TOP_LEFT);
    panel_manager.register_panel(&stats_panel, PanelManager::Position::TOP_LEFT);
    panel_manager.register_panel(&commands_panel, PanelManager::Position::TOP_RIGHT);
    panel_manager.register_panel(&action_log_panel, PanelManager::Position::BOTTOM_RIGHT);

    reset_trails(true);
}

Graphics::Stats Graphics::get_stats() const {
    return stats;
}

sf::RenderWindow& Graphics::get_window() {
    return window;
}

CommandsPanel& Graphics::get_commands_panel() {
    return commands_panel;
}

ConfigPanel& Graphics::get_config_panel() {
    return config_panel;
}

StatsPanel& Graphics::get_stats_panel() {
    return stats_panel;
}

void Graphics::pan_if_view_grabbed() {
    if (opt_view_grabbed_pos) {
        const sf::Vector2i new_cursor_pos = sf::Mouse::getPosition(window);
        vp.pan(sf::Vector2f(*opt_view_grabbed_pos - new_cursor_pos));
        opt_view_grabbed_pos = new_cursor_pos;
        reset_trails();
    }
}

// This calculation guarantees:
// 1. There are at least N grid squares in the smallest window dimension,
// 2. There are at most N*N grid squares in the smallest window dimension.
// Whenever the above conditions break from either zoom or window resizing,
// the grid spacing will adjust accordingly.
void Graphics::draw_grid() {
    if (!show_grid) {
        return;
    }
    const sf::Rect<double> rect = vp.get_rect();
    const sf::Vector2f res = vp.get_window_res();

    auto hline = sf::RectangleShape({res.x, 1});
    hline.setFillColor(Constants::Graphics::GRID_COLOR);

    auto vline = sf::RectangleShape({1, res.y});
    vline.setFillColor(Constants::Graphics::GRID_COLOR);

    const double min_dim = std::min(rect.size.x, rect.size.y);

    auto log = [](double num, double base) { return std::log(num) / std::log(base); };

    const double spacing = std::pow(Constants::Graphics::GRID_SPACING_FACTOR,
            std::floor(log(min_dim, Constants::Graphics::GRID_SPACING_FACTOR)) - 1);

    const double x2 = rect.size.x + rect.position.x;
    double x = ceil(rect.position.x / spacing) * spacing;
    while (x < x2) {
        const float x_ratio = (x - rect.position.x) / rect.size.x;
        vline.setPosition({x_ratio * res.x, 0});
        window.draw(vline);
        x += spacing;
    }

    const double y2 = rect.size.y + rect.position.y;
    double y = ceil(rect.position.y / spacing) * spacing;
    while (y < y2) {
        const float y_ratio = (y - rect.position.y) / rect.size.y;
        hline.setPosition({0, y_ratio * res.y});
        window.draw(hline);
        y += spacing;
    }
}

void Graphics::draw_bodies() {
    sf::Sprite prev(trails_texture1.getTexture());
    trails_texture2.clear(sf::Color::Transparent);
    if (!paused) {
        sf::RenderStates fade_states(sf::BlendNone);
        fade_states.shader = &fade_shader;
        set_fade_shader_decay();
        trails_texture2.draw(prev, fade_states);
    }

    for (uint64_t i = 0; i < bodies.n; i++) {
        body_vertex_array[i].position = vp.coords_to_pos_on_viewport(body_positions_cache[i]);
    }

    trails_texture2.draw(body_vertex_array, sf::RenderStates(&body_shader));
    trails_texture2.display();

    std::swap(trails_texture1, trails_texture2);
    trails_sprite.setTexture(trails_texture1.getTexture(), true);
    window.draw(trails_sprite);
}

void Graphics::update_selection_tracking() {
    if (!selector.has_selection()) {
        stats_panel.write_handle()->opt_selection = std::nullopt;
        cached_selection_stats = std::nullopt;
        return;
    }

    const auto new_selection_stats = selector.compute_stats();

    if (follow_selected && cached_selection_stats) {
        vp.pan_coords(new_selection_stats.center_of_mass - cached_selection_stats->center_of_mass);
    }

    stats_panel.write_handle()->opt_selection = StatsDisplayedData::Selection{
            .num_selected = new_selection_stats.n,
            .total_mass = new_selection_stats.total_mass,
            .center_of_mass = new_selection_stats.center_of_mass,
            .weighted_velocity = new_selection_stats.weighted_velocity};
    cached_selection_stats = new_selection_stats;
}

void Graphics::draw_selection_overlay() {
    if (opt_select_grabbed_pos) {
        const sf::Vector2i new_cursor_pos = sf::Mouse::getPosition(window);
        sf::RectangleShape selector(sf::Vector2f(new_cursor_pos - *opt_select_grabbed_pos));
        selector.setPosition(sf::Vector2f(*opt_select_grabbed_pos));
        selector.setFillColor(sf::Color(255, 0, 0, 64));
        selector.setOutlineColor(Constants::Graphics::SELECT_COLOR);
        selector.setOutlineThickness(1.f);
        window.draw(selector);
    }

    if (cached_selection_stats && selection_show_center_of_mass) {
        const sf::Vector2f CoM_pos =
                vp.coords_to_pos_on_viewport(cached_selection_stats->center_of_mass);
        const float marker_radius = body_diameter_pixels / 2.0f + 2.0f;
        sf::CircleShape CoM_marker(marker_radius);
        CoM_marker.setOrigin({marker_radius, marker_radius});
        CoM_marker.setPosition(CoM_pos);
        CoM_marker.setFillColor(sf::Color(0, 255, 0, 255));
        window.draw(CoM_marker);
    }
}

void Graphics::update_stats() {
    const auto now = sw.elapsed<std::chrono::seconds, 6>();
    const auto frame_delta = frame - stats.frame;
    const auto dt = now - stats.timestamp_s;
    fps_calculator.register_value(frame_delta / dt);
    stats = Stats{.timestamp_s = now,
            .frame = frame,
            .fps = fps_calculator.get_mean<float>(),
            .viewport_m = vp.get_rect().size,
            .viewport_px = sf::Vector2<uint32_t>{vp.get_window_res()}};
    action_log_panel.write_handle()->assign(action_log.to_string());
}

void Graphics::draw_ui() {
    draw_selection_overlay();
    window.draw(panel_manager);
}

void Graphics::reset_trails(bool resize) {
    last_trail_fade_sw.reset();
    if (resize && (!trails_texture1.resize(window.getSize()) || 
            !trails_texture2.resize(window.getSize()))) {
        throw std::runtime_error("Failed to resize trail textures");
    }
    trails_texture1.clear(sf::Color::Transparent);
    trails_texture2.clear(sf::Color::Transparent);
}

void Graphics::set_fade_shader_decay() {
    const auto elapsed = last_trail_fade_sw.duration<std::chrono::duration<double>>();
    last_trail_fade_sw.reset();

    constexpr float fade_chunk_size = 1.f / 255.f;
    const float fade_fraction = elapsed / Constants::Graphics::TRAIL_FADE;
    const float fractional_fade_chunks = fade_fraction / fade_chunk_size + remainder_fractional_fade_chunks;
    const float whole_fade_chunks = std::floor(fractional_fade_chunks);
    remainder_fractional_fade_chunks = fractional_fade_chunks - whole_fade_chunks;

    fade_shader.setUniform("decay", 
            sf::Glsl::Vec4(0.f, 0.f, 0.f, whole_fade_chunks * fade_chunk_size));
}

void Graphics::resize_view(sf::Vector2f new_size) {
    vp.resize(new_size);
    window.setView(sf::View(sf::Rect<float>{{0.f, 0.f}, new_size}));
    reset_trails(true);
}

void Graphics::zoom_view(double delta) {
    if (delta > 0.0) {
        vp.zoom(ViewPort::Zoom::IN, sf::Vector2f(sf::Mouse::getPosition(window)));
    }
    else if (delta < 0.0) {
        vp.zoom(ViewPort::Zoom::OUT, sf::Vector2f(sf::Mouse::getPosition(window)));
    }
    reset_trails();
}

void Graphics::grab_view() {
    if (opt_select_grabbed_pos) {
        release_select(true);
    }
    static const std::optional grabbed_cursor =
            sf::Cursor::createFromSystem(sf::Cursor::Type::Cross);
    if (grabbed_cursor) {
        window.setMouseCursor(*grabbed_cursor);
    }
    else {
        Log::warning("Failed to create cross cursor");
    }
    opt_view_grabbed_pos = sf::Mouse::getPosition(window);
}

void Graphics::release_view() {
    static const std::optional def_cursor = sf::Cursor::createFromSystem(sf::Cursor::Type::Arrow);
    if (def_cursor) {
        window.setMouseCursor(*def_cursor);
    }
    else {
        Log::warning("Failed to create default cursor");
    }
    opt_view_grabbed_pos = std::nullopt;
}

void Graphics::grab_select() {
    if (opt_view_grabbed_pos) {
        release_view();
    }
    opt_select_grabbed_pos = sf::Mouse::getPosition(window);
}

void Graphics::release_select(bool skip_select) {
    if (skip_select) {
        opt_select_grabbed_pos = std::nullopt;
    }
    if (!opt_select_grabbed_pos) {
        return;
    }
    const sf::Rect<float> region{sf::Vector2f(*opt_select_grabbed_pos),
            sf::Vector2f(sf::Mouse::getPosition(window)) - sf::Vector2f(*opt_select_grabbed_pos)};
    const bool had_selection = selector.has_selection();
    selector.select(region);
    if (const auto num_selected = selector.num_selected(); num_selected > 0) {
        action_log.log(fmt::format("Selected {} bodies", num_selected));
    }
    else if (had_selection) {
        action_log.log("Cleared selection");
    }
    opt_select_grabbed_pos = std::nullopt;
    cached_selection_stats = std::nullopt;
}

void Graphics::body_size_increase() {
    auto new_body_diameter_pixels = body_diameter_pixels + 1;
    if (new_body_diameter_pixels > Constants::Graphics::BODY_DIAMETER_PIXELS_RANGE.second) {
        Log::warning("Reached maximum body size (pixels), cannot magnify further");
        new_body_diameter_pixels = Constants::Graphics::BODY_DIAMETER_PIXELS_RANGE.second;
    }
    else {
        action_log.log(fmt::format("Body size: {} pixels", new_body_diameter_pixels));
    }
    body_diameter_pixels = new_body_diameter_pixels;
    body_shader.setUniform("pointDiameter", static_cast<float>(body_diameter_pixels));
}

void Graphics::body_size_decrease() {
    // If decreasing further, unsigned wrap-around will be an issue
    auto new_body_diameter_pixels = body_diameter_pixels - 1;
    if (new_body_diameter_pixels < Constants::Graphics::BODY_DIAMETER_PIXELS_RANGE.first) {
        Log::warning("Reached minimum body size (pixels), cannot reduce further");
        new_body_diameter_pixels = Constants::Graphics::BODY_DIAMETER_PIXELS_RANGE.first;
    }
    else {
        action_log.log(fmt::format("Body size: {} pixels", new_body_diameter_pixels));
    }
    body_diameter_pixels = new_body_diameter_pixels;
    body_shader.setUniform("pointDiameter", static_cast<float>(body_diameter_pixels));
}

void Graphics::toggle_grid() {
    show_grid = !show_grid;
    config_panel.write_handle()->grid = show_grid;
    action_log.log(fmt::format("Grid: {}", show_grid ? "Enabled" : "Disabled"));
}

void Graphics::toggle_selection_show() {
    selection_show = !selection_show;
    config_panel.write_handle()->selection_show = selection_show;
    action_log.log(fmt::format("Selection overlay: {}", selection_show ? "Enabled" : "Disabled"));
}

void Graphics::toggle_selection_show_center_of_mass() {
    selection_show_center_of_mass = !selection_show_center_of_mass;
    config_panel.write_handle()->selection_show_center_of_mass = selection_show_center_of_mass;
    action_log.log(fmt::format("CoM marker: {}", selection_show_center_of_mass ? "Enabled" : "Disabled"));
}

void Graphics::toggle_follow_selected() {
    follow_selected = !follow_selected;
    config_panel.write_handle()->selection_follow_center_of_mass = follow_selected;
    action_log.log(fmt::format("Selection follow: {}", follow_selected ? "Enabled" : "Disabled"));
}

void Graphics::center_on_selection_center_of_mass() {
    if (cached_selection_stats) {
        vp.center_on_coords(cached_selection_stats->center_of_mass);
        action_log.log("Centered on selection CoM");
    }
}

void Graphics::toggle_config_panel() {
    config_panel.set_visible(!config_panel.is_visible());
}

void Graphics::toggle_stats_panel() {
    stats_panel.set_visible(!stats_panel.is_visible());
}

void Graphics::toggle_commands_panel() {
    commands_panel.set_visible(!commands_panel.is_visible());
}

void Graphics::toggle_action_log_panel() {
    action_log_panel.set_visible(!action_log_panel.is_visible());
}

void Graphics::notify_paused() {
    paused = true;
    action_log.log("Simulation paused");
}

void Graphics::notify_resumed() {
    paused = false;
    action_log.log("Simulation resumed");
}

void Graphics::notify_timestep_changed(double old_dt, double new_dt) {
    config_panel.write_handle()->timestep_s = new_dt;
    using Time = Log::Time;
    action_log.log(fmt::format("Dt: {} -> {}", Time::from<Time::Unit::S>(old_dt),
            Time::from<Time::Unit::S>(new_dt)));
}

void Graphics::draw_frame() {
    window.clear(Constants::Graphics::BG_COLOR);
    pan_if_view_grabbed();
    std::memcpy(body_positions_cache.data(), bodies.pos_data(),
            bodies.n * sizeof(sf::Vector2<double>));
    update_selection_tracking();
    draw_grid();
    draw_bodies();
    draw_ui();
    window.display();
    frame++;
    stats_update_rate_limiter.try_call(std::bind(&Graphics::update_stats, this));
}
