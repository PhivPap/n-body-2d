#include "Graphics/Graphics.hpp"

#include <GL/gl.h>
#include <algorithm>
#include <numbers>

#include "Constants/Constants.hpp"
#include "Logger/Logger.hpp"
#include "Logger/Time.hpp"
#include "Text/Text.hpp"


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

namespace {
// The heaviest of the bodies offered to it, the nearest of equal masses
struct HeaviestBody {
    std::optional<uint32_t> index;
    double mass = -1.0;
    float distance_squared = 0.f;

    void offer(uint32_t i, double body_mass, float body_distance_squared) {
        if (body_mass > mass || (body_mass == mass && body_distance_squared < distance_squared)) {
            index = i;
            mass = body_mass;
            distance_squared = body_distance_squared;
        }
    }
};

// Offers `heaviest` the bodies begin..end whose vertices are within `radius` of `point`
void find_bodies_near(HeaviestBody& heaviest, sf::Vector2f point, float radius,
        const sf::VertexArray& vertices, const double* mass, uint64_t begin, uint64_t end) {
    for (uint64_t i = begin; i < end; i++) {
        const float distance_squared = (vertices[i].position - point).lengthSquared();
        if (distance_squared < radius * radius) {
            heaviest.offer(static_cast<uint32_t>(i), mass[i], distance_squared);
        }
    }
}
}  // namespace

 Graphics::Graphics(const Config& cfg, const Bodies& bodies)
        : graphics_cfg(cfg.graphics), bodies(bodies), body_positions_cache(bodies.n),
          body_velocities_cache(bodies.n), body_vertex_array(sf::PrimitiveType::Points, bodies.n),
          window(sf::VideoMode(sf::Vector2u(graphics_cfg.resolution)), "N-Body Sim"),
          vp(sf::Vector2f(graphics_cfg.resolution), graphics_cfg.pixel_scale),
          selector(bodies, body_positions_cache, body_vertex_array),
          tooltip_text(Text::font(), "", Constants::Graphics::TOOLTIP_CHARACTER_SIZE),
          apsis_label(Text::font(), "", Constants::Graphics::APSIS_CHARACTER_SIZE) {
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
    constexpr float apsis_radius = Constants::Graphics::APSIS_MARKER_RADIUS_PX;
    apsis_marker.setOrigin({apsis_radius, apsis_radius});
    apsis_marker.setFillColor(Constants::Graphics::ORBIT_COLOR);
    apsis_label.setFillColor(Constants::Graphics::ORBIT_COLOR);
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
        write_handle->show_named_orbits = show_named_orbits;
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

    // No primaries until an orbit needs them (body_orbit())
    primaries.assign(named_bodies.indices().size(), -1);
    reset_trails(true);
}

sf::RenderWindow& Graphics::get_window() {
    return window;
}

void Graphics::pan_if_view_grabbed() {
    if (opt_view_grabbed_pos) {
        const sf::Vector2i new_cursor_pos = sf::Mouse::getPosition(window);
        vp.pan(sf::Vector2f(*opt_view_grabbed_pos - new_cursor_pos));
        opt_view_grabbed_pos = new_cursor_pos;
        reset_trails();
    }
}

// How far the cursor went from a right press, which stays a click until it passes
// CLICK_MAX_DRAG_PX
void Graphics::update_select_drag() {
    if (opt_select_grabbed_pos) {
        select_grab_max_drag_px = std::max(select_grab_max_drag_px,
                sf::Vector2f(sf::Mouse::getPosition(window) - *opt_select_grabbed_pos).length());
    }
}

bool Graphics::is_box_selecting() const {
    return opt_select_grabbed_pos
           && select_grab_max_drag_px > Constants::Graphics::CLICK_MAX_DRAG_PX;
}

// Bodies under the cursor are looked for while it is in the window and no pan or box selection is
// going on. XWayland may not update the cursor position once it left the window.
void Graphics::update_hover_cursor() {
    hover_cursor = std::nullopt;
    if (!cursor_in_window || opt_view_grabbed_pos || is_box_selecting()) {
        return;
    }
    const sf::Vector2i cursor = sf::Mouse::getPosition(window);
    const sf::Vector2u size = window.getSize();
    if (cursor.x >= 0 && cursor.y >= 0 && static_cast<uint32_t>(cursor.x) < size.x
            && static_cast<uint32_t>(cursor.y) < size.y) {
        hover_cursor = sf::Vector2f(cursor);
    }
}

// The velocities of the bodies whose orbits the frame works out, read right after the positions:
// at a million steps a second, a moon's velocity read a few hundred microseconds later is some
// way further round its orbit, which then looks eccentric. The named bodies' (the attractors' and
// A's) and the selected body's; update_hover() reads the hovered body's, which only the tooltip's
// distance and speed show. While the simulation runs, velocities still lag positions by half a
// step, which the elements reflect slightly.
void Graphics::snapshot_velocities() {
    for (const uint32_t index : named_bodies.indices()) {
        body_velocities_cache[index] = bodies.vel(index);
    }
    if (const auto selected = selector.selected_body()) {
        body_velocities_cache[*selected] = bodies.vel(*selected);
    }
}

// Every named body's primary, which attractors are found from. Primaries change slowly.
void Graphics::update_primaries() {
    primaries = Orbit::find_primaries(named_bodies.indices(), bodies.mass_data(),
            body_positions_cache.data());
}

// A body's orbit, with its satellites, for a named, selected or hovered body: those whose
// velocities the frame read (snapshot_velocities()). Primaries are only refreshed while orbits
// are worked out.
std::optional<Orbit::BodyOrbit> Graphics::body_orbit(uint32_t index) {
    primaries_update_rate_limiter.try_call(std::bind(&Graphics::update_primaries, this));
    return Orbit::body_orbit(index, named_bodies.rank(index), named_bodies.indices(), primaries,
            bodies.mass_data(), body_positions_cache.data(), body_velocities_cache.data());
}

// How far from a body's centre the cursor is on it: bodies drawn bigger give it more room
float Graphics::hover_radius() const {
    using namespace Constants::Graphics;
    return std::max(HOVER_RADIUS_PX, body_diameter_pixels / 2.f + HOVER_MARGIN_PX);
}

// The bodies' window positions, and the heaviest body within hover_radius() of the hover cursor
// (the nearest of equal masses). A vectorized pass tells whether chunks of bodies have one near
// the cursor, in world coordinates with a pixel to spare for the rounding of window positions to
// float, and only those chunks are searched: a search in the pass would keep it from vectorizing.
std::optional<uint32_t> Graphics::transform_bodies() {
    if (bodies.n == 0) {
        return std::nullopt;
    }
    const ScreenTransform transform = vp.screen_transform();
    const sf::Vector2<double>* positions = body_positions_cache.data();
    sf::Vertex* vertices = &body_vertex_array[0];
    if (!hover_cursor) {
        for (uint64_t i = 0; i < bodies.n; i++) {
            vertices[i].position = sf::Vector2f(transform.to_screen(positions[i]));
        }
        return std::nullopt;
    }
    const float radius = hover_radius();
    const sf::Vector2<double> cursor =
            transform.origin + sf::Vector2<double>(*hover_cursor) / transform.pixels_per_meter;
    const double world_radius = (radius + 1.0) / transform.pixels_per_meter;
    const double world_radius_squared = world_radius * world_radius;
    HeaviestBody heaviest;
    for (uint64_t begin = 0; begin < bodies.n; begin += Constants::Graphics::HOVER_CHUNK_SIZE) {
        const uint64_t end = std::min(bodies.n, begin + Constants::Graphics::HOVER_CHUNK_SIZE);
        bool near = false;
        for (uint64_t i = begin; i < end; i++) {
            vertices[i].position = sf::Vector2f(transform.to_screen(positions[i]));
            near |= (positions[i] - cursor).lengthSquared() < world_radius_squared;
        }
        if (near) {
            find_bodies_near(heaviest, *hover_cursor, radius, body_vertex_array, bodies.mass_data(),
                    begin, end);
        }
    }
    return heaviest.index;
}

// The heaviest body drawn under `point`: of the named bodies whose markers it is on, and
// `heaviest_near`, the heaviest body within hover_radius()
std::optional<uint32_t> Graphics::pick_body(sf::Vector2f point,
        std::optional<uint32_t> heaviest_near) const {
    const std::optional<uint32_t> marker =
            named_bodies.pick(point, body_vertex_array, body_diameter_pixels, scale_named_by_mass);
    if (heaviest_near && (!marker || bodies.mass(*heaviest_near) > bodies.mass(*marker))) {
        return heaviest_near;
    }
    return marker;
}

// The body under the hover cursor. Its velocity is read now if snapshot_velocities() didn't.
void Graphics::update_hover(std::optional<uint32_t> heaviest_near_cursor) {
    hovered_body = hover_cursor ? pick_body(*hover_cursor, heaviest_near_cursor) : std::nullopt;
    if (hovered_body && !named_bodies.rank(*hovered_body)
            && hovered_body != selector.selected_body()) {
        body_velocities_cache[*hovered_body] = bodies.vel(*hovered_body);
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

    trails_texture2.draw(body_vertex_array, sf::RenderStates(&body_shader));
    trails_texture2.display();

    std::swap(trails_texture1, trails_texture2);
    trails_sprite.setTexture(trails_texture1.getTexture(), true);
    // Bodies were alpha blended onto a transparent texture, so it holds premultiplied alpha
    window.draw(trails_sprite,
            sf::BlendMode(sf::BlendMode::Factor::One, sf::BlendMode::Factor::OneMinusSrcAlpha));
}

// Appends an osculating orbit around its attractor's barycentre to `lines`, dashed, unless it is
// too small to see. It is traced in double precision and clipped to the window, so it stays exact
// at any zoom.
bool Graphics::append_orbit(const Orbit::BodyOrbit& orbit, const ScreenTransform& transform,
        sf::Color color, sf::VertexArray& lines) {
    using namespace Constants::Graphics;
    const Orbit::Elements& el = orbit.elements;
    if (Orbit::extent(el) * transform.pixels_per_meter < ORBIT_MIN_SIZE_PX) {
        return false;
    }
    orbit_segments.clear();
    Orbit::trace(el, orbit.attractor.pos, transform, vp.get_window_res(), orbit_segments,
            ORBIT_TOLERANCE_PX, ORBIT_CLIP_MARGIN_PX);
    Orbit::append_dashes(orbit_segments, ORBIT_DASH_PX, ORBIT_GAP_PX, color, lines);
    return true;
}

// Every named body's orbit, under the selection's, which is drawn in its own colour instead
void Graphics::draw_named_orbits() {
    if (!show_named_orbits) {
        return;
    }
    const std::optional<uint32_t> selected =
            selection_show_orbit && selection_orbit ? selector.selected_body() : std::nullopt;
    const ScreenTransform transform = vp.screen_transform();
    named_orbit_lines.clear();
    for (const uint32_t index : named_bodies.indices()) {
        if (index != selected) {
            if (const auto orbit = body_orbit(index)) {
                append_orbit(*orbit, transform, Constants::Graphics::NAMED_ORBIT_COLOR,
                        named_orbit_lines);
            }
        }
    }
    window.draw(named_orbit_lines);
}

// The selection's orbit, with its apsides
void Graphics::draw_selection_orbit() {
    using namespace Constants::Graphics;
    if (!selection_show_orbit || !selection_orbit) {
        return;
    }
    const Orbit::Elements& el = selection_orbit->elements;
    const Orbit::Vec focus = selection_orbit->attractor.pos;
    const ScreenTransform transform = vp.screen_transform();
    orbit_lines.clear();
    if (!append_orbit(*selection_orbit, transform, ORBIT_COLOR, orbit_lines)) {
        return;
    }
    window.draw(orbit_lines);
    const sf::Vector2f window_size = vp.get_window_res();

    const auto draw_apsis = [&](double nu, const char* name) {
        const Orbit::Vec pos = transform.to_screen(focus + el.position_at(nu));
        if (pos.x >= 0.0 && pos.x <= window_size.x && pos.y >= 0.0 && pos.y <= window_size.y) {
            apsis_marker.setPosition(sf::Vector2f(pos));
            window.draw(apsis_marker);
            const sf::Vector2f label_pos = sf::Vector2f(pos) + APSIS_LABEL_OFFSET_PX;
            apsis_label.setString(name);
            apsis_label.setPosition({std::round(label_pos.x), std::round(label_pos.y)});
            window.draw(apsis_label);
        }
    };
    draw_apsis(0.0, "Pe");
    if (el.closed()) {
        draw_apsis(std::numbers::pi, "Ap");
    }
}

// Markers above the trails, so that big bodies don't paint wide trails, then labels
void Graphics::draw_named_bodies() {
    const float base_px = body_diameter_pixels;
    named_bodies.draw_markers(window, marker_shader, body_vertex_array, base_px,
            scale_named_by_mass, selector.selection_mask(), selection_show);
    if (show_labels) {
        named_bodies.draw_labels(window, body_vertex_array, base_px, scale_named_by_mass,
                selector.selected_body(), selector.selection_mask(), selection_show);
    }
}

// The selection's stats, every frame for the view to follow them, which the inspector shows at
// the panel refresh rate
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

// Only a single body's orbit: bodies selected together may be far apart
void Graphics::update_selection_orbit() {
    const std::optional<uint32_t> body = selector.selected_body();
    selection_orbit = body ? body_orbit(*body) : std::nullopt;
}

void Graphics::update_inspector_panel() {
    // Panel values must not be NaN
    const auto not_nan = [](double x) {
        return std::isnan(x) ? std::numeric_limits<double>::infinity() : x;
    };
    const auto not_nan_vector = [&not_nan](sf::Vector2<double> v) {
        return sf::Vector2<double>(not_nan(v.x), not_nan(v.y));
    };
    auto write_handle = inspector_panel.write_handle();
    write_handle->count = 0;
    write_handle->body.clear();
    write_handle->mass_kg = 0.0;
    write_handle->position_m = {};
    write_handle->velocity_m_s = {};
    write_handle->orbit = std::nullopt;
    if (!cached_selection_stats) {
        return;
    }
    const Selector::SelectionStats& selection = *cached_selection_stats;
    write_handle->count = selection.n;
    write_handle->body = bodies.id(selection.heaviest);
    write_handle->mass_kg = selection.total_mass;
    write_handle->position_m = not_nan_vector(selection.center_of_mass);
    write_handle->velocity_m_s = not_nan_vector(selection.weighted_velocity);
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
            .retrograde = selection_orbit->retrograde,
            .satellite_count = selection_orbit->satellite_count};
}

void Graphics::draw_selection_overlay() {
    if (is_box_selecting()) {
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
    const float diameter = named_bodies.rank(index)
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
    std::string text = fmt::format("{}\nMass: {}\n", bodies.id(index),
            InspectorFormat::mass(bodies.mass(index)));
    if (const auto orbit = body_orbit(index)) {
        text += fmt::format("Around {}: {}, {}",
                InspectorFormat::attractor(bodies.id(orbit->attractor.primary),
                        orbit->attractor.inner_count),
                InspectorFormat::distance(orbit->elements.distance),
                InspectorFormat::speed(orbit->elements.speed));
    }
    else {
        text += fmt::format("Speed: {}", InspectorFormat::speed(bodies.vel(index).length()));
    }
    tooltip_text.setString(Text::from_utf8(text));

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
    cancel_select();
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

// A right press during a left press cancels the pan
void Graphics::grab_select(sf::Vector2i position) {
    if (opt_view_grabbed_pos) {
        release_view();
    }
    opt_select_grabbed_pos = position;
    select_grab_max_drag_px = 0.f;
}

// A left press during a right press cancels the selection
void Graphics::cancel_select() {
    opt_select_grabbed_pos = std::nullopt;
}

void Graphics::release_select(sf::Vector2i position) {
    if (!opt_select_grabbed_pos) {
        return;
    }
    const sf::Vector2i press = *opt_select_grabbed_pos;
    select_grab_max_drag_px =
            std::max(select_grab_max_drag_px, sf::Vector2f(position - press).length());
    const bool click = !is_box_selecting();
    opt_select_grabbed_pos = std::nullopt;
    const bool had_selection = selector.has_selection();
    std::optional<uint32_t> clicked;
    if (click) {
        // What the window shows under the press, which may not have been hovered: the last frame's
        // vertices, searched once
        HeaviestBody heaviest;
        find_bodies_near(heaviest, sf::Vector2f(press), hover_radius(), body_vertex_array,
                bodies.mass_data(), 0, bodies.n);
        clicked = pick_body(sf::Vector2f(press), heaviest.index);
    }
    if (clicked) {
        selector.select_body(*clicked);
        action_log.log(fmt::format("Selected {}", bodies.id(*clicked)));
    }
    else {
        // A click on empty space selects nothing, like an empty box
        const sf::Rect<float> box(sf::Vector2f(press), sf::Vector2f(position - press));
        selector.select(click ? sf::Rect<float>{} : box);
        if (const auto num_selected = selector.num_selected(); num_selected > 0) {
            action_log.log(fmt::format("Selected {} bodies", num_selected));
        }
        else if (had_selection) {
            action_log.log("Cleared selection");
        }
    }
    cached_selection_stats = selector.has_selection() ? std::optional(selector.compute_stats())
                                                      : std::nullopt;
    // The next frame works out the orbit, with velocities read with its positions
    selection_orbit = std::nullopt;
    inspector_update_pending = true;
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

void Graphics::toggle_selection_orbit() {
    selection_show_orbit = !selection_show_orbit;
    config_panel.write_handle()->selection_show_orbit = selection_show_orbit;
    action_log.log(fmt::format("Selection orbit: {}",
            selection_show_orbit ? "Enabled" : "Disabled"));
}

void Graphics::toggle_named_orbits() {
    show_named_orbits = !show_named_orbits;
    config_panel.write_handle()->show_named_orbits = show_named_orbits;
    action_log.log(fmt::format("Named orbits: {}", show_named_orbits ? "Enabled" : "Disabled"));
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
    }
    update_inspector_panel();
}

void Graphics::draw_frame() {
    window.clear(Constants::Graphics::BG_COLOR);
    pan_if_view_grabbed();
    update_select_drag();
    update_hover_cursor();
    std::memcpy(body_positions_cache.data(), bodies.pos_data(),
            bodies.n * sizeof(sf::Vector2<double>));
    snapshot_velocities();
    update_selection_tracking();
    update_hover(transform_bodies());
    update_selection_orbit();
    if (inspector_update_pending) {
        update_inspector_panel();
        inspector_update_pending = false;
    }
    draw_grid();
    draw_bodies();
    draw_named_orbits();
    draw_selection_orbit();
    draw_named_bodies();
    draw_ui();
    window.display();
    frame++;
    stats_update_rate_limiter.try_call(std::bind(&Graphics::update_stats, this));
}
