#include "Graphics/Graphics.hpp"

#include <GL/gl.h>
#include <algorithm>

#include "AssetManager/AssetManager.hpp"
#include "Constants/Constants.hpp"
#include "Logger/Distance.hpp"
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
// Named bodies' markers: points as wide as their texture coordinate's x, drawn as circles
constexpr std::string_view marker_vertex_shader = R"glsl(
#version 130
void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_PointSize = gl_MultiTexCoord0.x;  // the marker's diameter in pixels
    gl_FrontColor = gl_Color;
}
)glsl";
constexpr std::string_view marker_fragment_shader = R"glsl(
#version 130
void main() {
    if (length(gl_PointCoord - vec2(0.5, 0.5)) > 0.5) discard;  // circle
    gl_FragColor = gl_Color;
}
)glsl";
constexpr std::string_view fade_fragment_shader = R"glsl(
#version 130
uniform sampler2D texture;
uniform float decay;
void main() {
    gl_FragColor = texture2D(texture, gl_TexCoord[0].xy) - decay;  // Fade rgb too (premultiplied)
}
)glsl";

// The selection as the stats panel shows it
static std::optional<StatsDisplayedData::Selection> panel_selection(
        const std::optional<Selector::SelectionStats>& selection) {
    if (!selection) {
        return std::nullopt;
    }
    return StatsDisplayedData::Selection{.num_selected = selection->n,
            .total_mass = selection->total_mass,
            .center_of_mass = selection->center_of_mass,
            .weighted_velocity = selection->weighted_velocity};
}

 Graphics::Graphics(const Config& cfg, const Bodies& bodies)
        : graphics_cfg(cfg.graphics), bodies(bodies), body_positions_cache(bodies.n),
          body_vertex_array(sf::PrimitiveType::Points, bodies.n),
          window(sf::VideoMode(sf::Vector2u(graphics_cfg.resolution)), "N-Body Sim"),
          vp(sf::Vector2f(graphics_cfg.resolution), graphics_cfg.pixel_scale),
          selector(bodies, body_positions_cache, body_vertex_array),
          tooltip_text(AssetManager::instance().get<sf::Font>("fonts/UbuntuMono-R.ttf"), "",
                  Constants::Graphics::TOOLTIP_CHARACTER_SIZE) {
    window.setFramerateLimit(graphics_cfg.fps);
    window.setVerticalSyncEnabled(graphics_cfg.vsync_enabled);

    for (uint64_t i = 0; i < body_vertex_array.getVertexCount(); i++) {
        body_vertex_array[i].color = Constants::Graphics::BODY_COLOR;
    }
    selector.set_show(selection_show);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE);  // gl_PointCoord is undefined without it (compatibility profile)
    if (!body_shader.loadFromMemory(body_vertex_shader, body_fragment_shader)
            || !fade_shader.loadFromMemory(fade_fragment_shader, sf::Shader::Type::Fragment)
            || !marker_shader.loadFromMemory(marker_vertex_shader, marker_fragment_shader)) {
        throw std::runtime_error("Failed to load shaders");
    }
    body_shader.setUniform("pointDiameter", static_cast<float>(body_diameter_pixels));
    fade_shader.setUniform("texture", sf::Shader::CurrentTexture);
    tooltip_box.setFillColor(Constants::Graphics::TOOLTIP_BACKGROUND_COLOR);
    panel_manager.register_panel(&config_panel, PanelManager::Position::TOP_LEFT);
    panel_manager.register_panel(&stats_panel, PanelManager::Position::TOP_LEFT);
    panel_manager.register_panel(&commands_panel, PanelManager::Position::TOP_RIGHT);
    panel_manager.register_panel(&inspector_panel, PanelManager::Position::TOP_RIGHT);
    panel_manager.register_panel(&action_log_panel, PanelManager::Position::BOTTOM_RIGHT);
    config_panel.set_visible(graphics_cfg.show_config_panel);
    stats_panel.set_visible(graphics_cfg.show_stats_panel);
    commands_panel.set_visible(graphics_cfg.show_commands_panel);
    inspector_panel.set_visible(graphics_cfg.show_inspector_panel);
    action_log_panel.set_visible(graphics_cfg.show_action_log_panel);

    {
        auto write_handle = config_panel.write_handle();
        write_handle->timestep_s = cfg.sim.timestep;
        write_handle->algorithm = cfg.sim.simtype_str;
        write_handle->theta = cfg.sim.theta;
        write_handle->show_theta = cfg.sim.simtype != Config::Simulation::SimType::ALL_PAIRS;
        write_handle->softening_factor = cfg.sim.softening_factor;
        write_handle->softening_length_m = cfg.sim.softening_length_m;
        write_handle->threads = cfg.sim.threads;
        write_handle->show_threads = cfg.sim.simtype == Config::Simulation::SimType::BARNES_HUT;
        write_handle->viewport_m = vp.get_rect().size;
        write_handle->viewport_px = sf::Vector2<uint32_t>{vp.get_window_res()};
        write_handle->vsync = graphics_cfg.vsync_enabled;
        write_handle->grid = show_grid;
        write_handle->max_fps = graphics_cfg.fps;
        write_handle->selection_show = selection_show;
        write_handle->selection_show_center_of_mass = selection_show_center_of_mass;
        write_handle->selection_follow_center_of_mass = follow_selected;
        write_handle->selection_show_orbit = selection_show_orbit;
        write_handle->show_labels = show_labels;
        write_handle->scale_named_by_mass = scale_named_by_mass;
    }

    {
        auto write_handle = stats_panel.write_handle();
        write_handle->iteration = 0;
        write_handle->iter_per_sec = 0;
        write_handle->frame = 0;
        write_handle->fps = 0;
        write_handle->elapsed_s = 0;
        write_handle->simulated_time_s = 0;
        write_handle->simulation_rate = std::numeric_limits<double>::quiet_NaN();
    }
    update_inspector_panel();

    // No primaries until the first frame has the bodies' positions
    primaries.assign(named_bodies.indices().size(), -1);
    reset_trails(true);
}

sf::RenderWindow& Graphics::get_window() {
    return window;
}

// The view follows the cursor once it went further from the press than a click can, so that
// clicks, which select bodies, don't move the view or clear the trails
void Graphics::pan_if_view_grabbed() {
    if (!opt_view_grabbed_pos) {
        return;
    }
    const sf::Vector2i new_cursor_pos = sf::Mouse::getPosition(window);
    view_grab_max_drag_px = std::max(view_grab_max_drag_px,
            sf::Vector2f(new_cursor_pos - view_grab_start).length());
    if (new_cursor_pos != *opt_view_grabbed_pos
            && view_grab_max_drag_px > Constants::Graphics::CLICK_MAX_DRAG_PX) {
        vp.pan(sf::Vector2f(*opt_view_grabbed_pos - new_cursor_pos));
        opt_view_grabbed_pos = new_cursor_pos;
        reset_trails();
    }
}

// Bodies under the cursor are looked for while it is in the window and no box selection or pan
// is going on. XWayland may not update the cursor position once it left the window.
void Graphics::update_hover_cursor() {
    hover_cursor = std::nullopt;
    if (!cursor_in_window || opt_select_grabbed_pos
            || (opt_view_grabbed_pos
                    && view_grab_max_drag_px > Constants::Graphics::CLICK_MAX_DRAG_PX)) {
        return;
    }
    const sf::Vector2i cursor = sf::Mouse::getPosition(window);
    const sf::Vector2u size = window.getSize();
    if (cursor.x >= 0 && cursor.y >= 0 && static_cast<uint32_t>(cursor.x) < size.x
            && static_cast<uint32_t>(cursor.y) < size.y) {
        hover_cursor = sf::Vector2f(cursor);
    }
}

// Every named body's primary, which attractors are found from. Primaries change slowly.
void Graphics::update_primaries() {
    primaries = Orbit::find_primaries(named_bodies.indices(), bodies.mass_data(),
            body_positions_cache.data());
}

// The attractor of a target, and its orbit around it
template <typename Excluded>
std::optional<Graphics::OrbitView> Graphics::find_orbit(const Orbit::Target& target,
        Excluded&& excluded) const {
    const auto& named = named_bodies.indices();
    const Orbit::Vec* pos = body_positions_cache.data();
    const auto attractor = Orbit::find_attractor(target, named, primaries, bodies.mass_data(), pos,
            bodies.vel_data(), excluded);
    if (!attractor) {
        return std::nullopt;
    }
    const auto elements = Orbit::elements(target.pos - attractor->pos,
            target.vel - attractor->vel,
            Constants::Simulation::G * (target.mass + attractor->mass));
    if (!elements) {
        return std::nullopt;
    }
    return OrbitView{.attractor = *attractor, .elements = *elements,
            .retrograde = Orbit::retrograde(elements->angular_momentum, *attractor, named,
                    primaries, pos, bodies.vel_data())};
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

    nearest_body_in_hover_radius = std::nullopt;
    if (hover_cursor) {
        // Also looks for the body nearest to the cursor, in the same pass over the bodies
        const sf::Vector2f cursor = *hover_cursor;
        float nearest_distance_squared =
                Constants::Graphics::HOVER_RADIUS_PX * Constants::Graphics::HOVER_RADIUS_PX;
        uint64_t nearest = bodies.n;
        for (uint64_t i = 0; i < bodies.n; i++) {
            const sf::Vector2f pos = vp.coords_to_pos_on_viewport(body_positions_cache[i]);
            body_vertex_array[i].position = pos;
            const float distance_squared = (pos - cursor).lengthSquared();
            if (distance_squared < nearest_distance_squared) {
                nearest_distance_squared = distance_squared;
                nearest = i;
            }
        }
        if (nearest < bodies.n) {
            nearest_body_in_hover_radius = static_cast<uint32_t>(nearest);
        }
    }
    else {
        for (uint64_t i = 0; i < bodies.n; i++) {
            body_vertex_array[i].position = vp.coords_to_pos_on_viewport(body_positions_cache[i]);
        }
    }

    trails_texture2.draw(body_vertex_array, sf::RenderStates(&body_shader));
    trails_texture2.display();

    std::swap(trails_texture1, trails_texture2);
    trails_sprite.setTexture(trails_texture1.getTexture(), true);
    // Bodies were alpha blended onto a transparent texture, so it holds premultiplied alpha
    window.draw(trails_sprite,
            sf::BlendMode(sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha));
}

// Named bodies' markers take precedence over the body nearest to the cursor
void Graphics::update_hover() {
    hovered_body = std::nullopt;
    if (!hover_cursor) {
        return;
    }
    hovered_body = named_bodies.pick(*hover_cursor, body_vertex_array, body_diameter_pixels,
            scale_named_by_mass);
    if (!hovered_body) {
        hovered_body = nearest_body_in_hover_radius;
    }
    hovered_orbit = std::nullopt;
    if (hovered_body) {
        const uint32_t index = *hovered_body;
        const Orbit::Target target{bodies.mass(index), body_positions_cache[index],
                bodies.vel(index)};
        hovered_orbit = find_orbit(target, [index](uint32_t i) { return i == index; });
    }
}

// Markers above the trails, so that big bodies don't paint wide trails, then labels
void Graphics::draw_named_bodies() {
    const float base_px = body_diameter_pixels;
    named_bodies.draw_markers(window, marker_shader, body_vertex_array, base_px,
            scale_named_by_mass, selector.selection_mask(), selection_show);
    if (show_labels) {
        const std::optional<uint32_t> selected_body =
                selector.num_selected() == 1 && cached_selection_stats
                        ? std::optional(cached_selection_stats->heaviest)
                        : std::nullopt;
        named_bodies.draw_labels(window, body_vertex_array, base_px, scale_named_by_mass,
                selected_body, selector.selection_mask(), selection_show);
    }
}

// The stats panel shows the selection's stats at the panel refresh rate
void Graphics::update_selection_tracking() {
    if (!selector.has_selection()) {
        cached_selection_stats = std::nullopt;
        return;
    }

    const auto new_selection_stats = selector.compute_stats();

    if (follow_selected && cached_selection_stats) {
        vp.pan_coords(new_selection_stats.center_of_mass - cached_selection_stats->center_of_mass);
    }

    cached_selection_stats = new_selection_stats;
}

// The selection's orbit, treating it as one body: velocities lag the positions by half a step
// while the simulation runs, which the elements reflect, slightly
void Graphics::update_selection_orbit() {
    selection_orbit = std::nullopt;
    if (!cached_selection_stats) {
        return;
    }
    const Selector::SelectionStats& selection = *cached_selection_stats;
    selection_orbit = find_orbit(
            {selection.total_mass, selection.center_of_mass, selection.weighted_velocity},
            [this](uint32_t i) { return selector.is_selected(i); });
}

void Graphics::update_inspector_panel() {
    // Panel values must not be NaN
    const auto not_nan = [](double x) {
        return std::isnan(x) ? std::numeric_limits<double>::infinity() : x;
    };
    auto write_handle = inspector_panel.write_handle();
    write_handle->body = std::nullopt;
    write_handle->mass_kg = 0.0;
    write_handle->speed_m_s = 0.0;
    write_handle->orbit = std::nullopt;
    if (!cached_selection_stats) {
        return;
    }
    const Selector::SelectionStats& selection = *cached_selection_stats;
    const std::string& heaviest = bodies.id(selection.heaviest);
    write_handle->body = selection.n == 1
                                 ? heaviest
                                 : fmt::format("{} (+{} more)", heaviest, selection.n - 1);
    write_handle->mass_kg = selection.total_mass;
    write_handle->speed_m_s = not_nan(selection.weighted_velocity.length());
    if (!selection_orbit) {
        return;
    }
    const Orbit::Elements& el = selection_orbit->elements;
    const auto closed = [&el](double x) { return el.closed() ? std::optional(x) : std::nullopt; };
    write_handle->orbit = InspectorDisplayedData::Orbit{
            .attractor = bodies.id(selection_orbit->attractor.primary),
            .inner_count = selection_orbit->attractor.inner_count,
            .distance_m = el.distance,
            .relative_speed_m_s = el.speed,
            .eccentricity = el.eccentricity,
            .periapsis_m = el.periapsis,
            .semi_major_axis_m = closed(el.semi_major_axis),
            .apoapsis_m = closed(el.apoapsis),
            .period_s = closed(el.period),
            .excess_speed_m_s = el.closed() ? std::nullopt : std::optional(el.excess_speed),
            .time_to_periapsis_s = std::isfinite(el.time_to_periapsis)
                                           ? std::optional(el.time_to_periapsis)
                                           : std::nullopt,
            .clockwise = el.angular_momentum > 0.0,
            .retrograde = selection_orbit->retrograde};
}

// Like release_select(), for a clicked body
void Graphics::select_body(uint32_t index) {
    selector.select_body(index);
    action_log.log(fmt::format("Selected {}", bodies.id(index)));
    cached_selection_stats = selector.compute_stats();
    stats_panel.write_handle()->opt_selection = panel_selection(cached_selection_stats);
    update_selection_orbit();
    update_inspector_panel();
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

void Graphics::draw_hover_ring() {
    if (!hovered_body) {
        return;
    }
    const uint32_t index = *hovered_body;
    const float diameter = named_bodies.is_named(index)
                                   ? named_bodies.marker_diameter(index, body_diameter_pixels,
                                             scale_named_by_mass)
                                   : body_diameter_pixels;
    const float radius = diameter / 2.f + Constants::Graphics::HOVER_MARGIN_PX;
    sf::CircleShape ring(radius);
    ring.setOrigin({radius, radius});
    ring.setPosition(body_vertex_array[index].position);
    ring.setFillColor(sf::Color::Transparent);
    ring.setOutlineColor(Constants::Graphics::HOVER_COLOR);
    ring.setOutlineThickness(1.5f);
    window.draw(ring);
}

// The hovered body's id, mass, and what it orbits or else its speed, next to the cursor
void Graphics::draw_tooltip() {
    using namespace Constants::Graphics;
    if (!hovered_body || !hover_cursor) {
        return;
    }
    const uint32_t index = *hovered_body;
    std::string text = fmt::format("{}\nMass: {:.4g}kg\n", bodies.id(index), bodies.mass(index));
    if (hovered_orbit) {
        const Orbit::Attractor& attractor = hovered_orbit->attractor;
        text += fmt::format("Around {}{}: {}, {}/s", bodies.id(attractor.primary),
                attractor.inner_count > 0 ? fmt::format(" (+{} inner)", attractor.inner_count) : "",
                Log::Distance::from(hovered_orbit->elements.distance),
                Log::Distance::from(hovered_orbit->elements.speed));
    }
    else {
        const double speed = bodies.vel(index).length();
        text += fmt::format("Speed: {}",
                std::isfinite(speed) ? fmt::format("{}/s", Log::Distance::from(speed)) : "-");
    }
    tooltip_text.setString(sf::String::fromUtf8(text.begin(), text.end()));

    // Below right of the cursor, or left of it or above it if it doesn't fit there
    const sf::FloatRect bounds = tooltip_text.getLocalBounds();
    const sf::Vector2f padding(TOOLTIP_PADDING_PX, TOOLTIP_PADDING_PX);
    const sf::Vector2f size = bounds.size + 2.f * padding;
    const sf::Vector2f window_size = window.getView().getSize();
    sf::Vector2f pos = *hover_cursor + sf::Vector2f(TOOLTIP_OFFSET_PX, TOOLTIP_OFFSET_PX);
    if (pos.x + size.x > window_size.x) {
        pos.x = hover_cursor->x - TOOLTIP_OFFSET_PX - size.x;
    }
    if (pos.y + size.y > window_size.y) {
        pos.y = hover_cursor->y - TOOLTIP_OFFSET_PX - size.y;
    }
    pos = {std::round(pos.x), std::round(pos.y)};
    tooltip_box.setSize(size);
    tooltip_box.setPosition(pos);
    const sf::Vector2f text_pos = pos + padding - bounds.position;
    tooltip_text.setPosition({std::round(text_pos.x), std::round(text_pos.y)});
    window.draw(tooltip_box);
    window.draw(tooltip_text);
}

void Graphics::update_stats() {
    const auto now = sw.elapsed<std::chrono::seconds, 6>();
    const auto frame_delta = frame - stats.frame;
    const auto dt = now - stats.timestamp_s;
    fps_calculator.register_value(frame_delta / dt);
    stats = Stats{.timestamp_s = now, .frame = frame, .fps = fps_calculator.get_mean<float>()};
    action_log_panel.write_handle()->assign(action_log.to_string());
}

void Graphics::draw_ui() {
    draw_selection_overlay();
    draw_hover_ring();
    window.draw(panel_manager);
    draw_tooltip();
}

void Graphics::reset_trails(bool resize) {
    last_trail_fade_sw.reset();
    remainder_fractional_fade_chunks = 0.f;
    if (resize && (!trails_texture1.resize(window.getSize()) || 
            !trails_texture2.resize(window.getSize()))) {
        throw std::runtime_error("Failed to resize trail textures");
    }
    trails_texture1.clear(sf::Color::Transparent);
    trails_texture2.clear(sf::Color::Transparent);
}

void Graphics::set_fade_shader_decay() {
    if (trails_fade.count() <= 0) {
        fade_shader.setUniform("decay", 1.f);
        return;
    }

    const auto elapsed = last_trail_fade_sw.duration<std::chrono::duration<double>>();
    last_trail_fade_sw.reset();

    constexpr float fade_chunk_size = 1.f / 255.f;
    const float fade_fraction = elapsed / trails_fade;
    const float fractional_fade_chunks = fade_fraction / fade_chunk_size + remainder_fractional_fade_chunks;
    const float whole_fade_chunks = std::floor(fractional_fade_chunks);
    remainder_fractional_fade_chunks = fractional_fade_chunks - whole_fade_chunks;

    fade_shader.setUniform("decay", whole_fade_chunks * fade_chunk_size);
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
    view_grab_start = *opt_view_grabbed_pos;
    view_grab_max_drag_px = 0.f;
    click_candidate = hovered_body;
}

void Graphics::release_view(bool allow_click) {
    static const std::optional def_cursor = sf::Cursor::createFromSystem(sf::Cursor::Type::Arrow);
    if (def_cursor) {
        window.setMouseCursor(*def_cursor);
    }
    else {
        Log::warning("Failed to create default cursor");
    }
    if (allow_click && opt_view_grabbed_pos && click_candidate) {
        // The cursor may have moved since the last frame
        const float drag = std::max(view_grab_max_drag_px,
                sf::Vector2f(sf::Mouse::getPosition(window) - view_grab_start).length());
        if (drag <= Constants::Graphics::CLICK_MAX_DRAG_PX) {
            select_body(*click_candidate);
        }
    }
    opt_view_grabbed_pos = std::nullopt;
    click_candidate = std::nullopt;
}

// A right press during a left press cancels the pan, which isn't a click
void Graphics::grab_select() {
    if (opt_view_grabbed_pos) {
        release_view(false);
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
    cached_selection_stats = selector.has_selection() ? std::optional(selector.compute_stats())
                                                      : std::nullopt;
    stats_panel.write_handle()->opt_selection = panel_selection(cached_selection_stats);
    update_selection_orbit();
    update_inspector_panel();
}

void Graphics::body_size_increase() {
    auto new_body_diameter_pixels = body_diameter_pixels + 1;
    if (new_body_diameter_pixels > Constants::Graphics::BODY_DIAMETER_PIXELS_RANGE.second) {
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
        new_body_diameter_pixels = Constants::Graphics::BODY_DIAMETER_PIXELS_RANGE.first;
    }
    else {
        action_log.log(fmt::format("Body size: {} pixels", new_body_diameter_pixels));
    }
    body_diameter_pixels = new_body_diameter_pixels;
    body_shader.setUniform("pointDiameter", static_cast<float>(body_diameter_pixels));
}

void Graphics::trails_length_increase() {
    trails_fade += Constants::Graphics::TRAILS_FADE_CHANGE_STEP;
    action_log.log(fmt::format("Trails length: {}", 
            Log::Time::from<Log::Time::Unit::S>(trails_fade.count())));
}

void Graphics::trails_length_decrease() {
    trails_fade -= Constants::Graphics::TRAILS_FADE_CHANGE_STEP;
    if (trails_fade.count() < 0) {
        trails_fade = std::chrono::duration<double>::zero();
    }
    else {
        action_log.log(fmt::format("Trails length: {}", 
                Log::Time::from<Log::Time::Unit::S>(trails_fade.count())));
    }
}

void Graphics::toggle_grid() {
    show_grid = !show_grid;
    config_panel.write_handle()->grid = show_grid;
    action_log.log(fmt::format("Grid: {}", show_grid ? "Enabled" : "Disabled"));
}

void Graphics::toggle_selection_show() {
    selection_show = !selection_show;
    selector.set_show(selection_show);
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

void Graphics::toggle_labels() {
    show_labels = !show_labels;
    config_panel.write_handle()->show_labels = show_labels;
    action_log.log(fmt::format("Labels: {}", show_labels ? "Enabled" : "Disabled"));
}

void Graphics::toggle_mass_scaled_markers() {
    scale_named_by_mass = !scale_named_by_mass;
    config_panel.write_handle()->scale_named_by_mass = scale_named_by_mass;
    action_log.log(fmt::format("Mass-scaled markers: {}",
            scale_named_by_mass ? "Enabled" : "Disabled"));
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

void Graphics::toggle_inspector_panel() {
    inspector_panel.set_visible(!inspector_panel.is_visible());
}

void Graphics::set_cursor_in_window(bool in_window) {
    cursor_in_window = in_window;
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

void Graphics::update_simulation_stats(const SimulationStats& sim_stats) {
    {
        auto write_handle = config_panel.write_handle();
        write_handle->viewport_m = vp.get_rect().size;
        write_handle->viewport_px = sf::Vector2<uint32_t>{vp.get_window_res()};
    }

    {
        auto write_handle = stats_panel.write_handle();
        write_handle->iteration = sim_stats.iteration;
        write_handle->iter_per_sec = sim_stats.ips;
        write_handle->frame = stats.frame;
        write_handle->fps = stats.fps;
        write_handle->elapsed_s = sim_stats.real_elapsed_s;
        write_handle->simulated_time_s = sim_stats.simulated_elapsed_s;
        write_handle->simulation_rate = sim_stats.ips * sim_stats.timestep_s;
        write_handle->opt_selection = panel_selection(cached_selection_stats);
    }
    update_inspector_panel();
}

void Graphics::draw_frame() {
    window.clear(Constants::Graphics::BG_COLOR);
    pan_if_view_grabbed();
    update_hover_cursor();
    std::memcpy(body_positions_cache.data(), bodies.pos_data(),
            bodies.n * sizeof(sf::Vector2<double>));
    primaries_update_rate_limiter.try_call(std::bind(&Graphics::update_primaries, this));
    update_selection_tracking();
    update_selection_orbit();
    draw_grid();
    draw_bodies();
    update_hover();
    draw_named_bodies();
    draw_ui();
    window.display();
    frame++;
    stats_update_rate_limiter.try_call(std::bind(&Graphics::update_stats, this));
}
