#pragma once

#include <optional>
#include <vector>

#include "ActionLog/ActionLog.hpp"
#include "Body/Body.hpp"
#include "BufferedMeanCalculator/BufferedMeanCalculator.hpp"
#include "Config/Config.hpp"
#include "Constants/Constants.hpp"
#include "NamedBodies/NamedBodies.hpp"
#include "Orbit/Orbit.hpp"
#include "Panel/ActionLogPanel.hpp"
#include "Panel/CommandsPanel.hpp"
#include "Panel/ConfigPanel.hpp"
#include "Panel/InspectorPanel.hpp"
#include "Panel/LegendPanel.hpp"
#include "Panel/Panel.hpp"
#include "Panel/PanelManager.hpp"
#include "Panel/StatsPanel.hpp"
#include "RLCaller/RLCaller.hpp"
#include "Render/HdrRenderer.hpp"
#include "Render/RenderGroups.hpp"
#include "Selector/Selector.hpp"
#include "StopWatch/StopWatch.hpp"
#include "ViewPort/ViewPort.hpp"


class Graphics {
public:
    // What the panels show of the simulation
    struct SimulationStats {
        uint64_t iteration;
        float ips;
        double real_elapsed_s;
        double simulated_elapsed_s;
        double timestep_s;
    };

    // Draws on `window`, which it applies the config's frame rate and vsync to
    Graphics(const Config& cfg, const Bodies& bodies, sf::RenderWindow& window);
    sf::RenderWindow& get_window();
    void resize_view(sf::Vector2f new_size);
    void zoom_view(double delta);
    void grab_view();
    void release_view();
    // A right press released within CLICK_MAX_DRAG_PX is a click, which selects the body drawn
    // under the press (or clears the selection on empty space); dragged further, it selects a box.
    // `position`: the event's.
    void grab_select(sf::Vector2i position);
    void release_select(sf::Vector2i position);
    void body_size_increase();
    void body_size_decrease();
    void trails_length_increase();
    void trails_length_decrease();
    void toggle_grid();
    void toggle_selection_show();
    void toggle_selection_show_center_of_mass();
    void toggle_follow_selected();
    void toggle_selection_orbit();
    void toggle_named_orbits();
    void toggle_labels();
    void toggle_mass_scaled_markers();
    void cycle_color_mode();
    void toggle_exposure_lock();
    void exposure_increase();
    void exposure_decrease();
    void toggle_bloom();
    void stretch_increase();
    void stretch_decrease();
    // Legend rows, 0 for key 1; others do nothing
    void toggle_group(uint32_t row);
    void solo_group(uint32_t row);
    void show_all_groups();
    void center_on_selection_center_of_mass();
    void toggle_config_panel();
    void toggle_stats_panel();
    void toggle_commands_panel();
    void toggle_action_log_panel();
    void toggle_inspector_panel();
    void toggle_legend_panel();
    void set_cursor_in_window(bool in_window);
    void notify_paused();
    void notify_resumed();
    void notify_timestep_changed(double old_dt, double new_dt);
    void update_simulation_stats(const SimulationStats& sim_stats);
    // Draws the frame, which the caller displays: Dear ImGui's windows go over it
    void draw_frame();

private:
    struct Stats {
        double timestamp_s;
        uint64_t frame;
        float fps;
    };

    const Config::Graphics& graphics_cfg;
    const Bodies& bodies;
    // Display options, changed at runtime
    bool show_grid = graphics_cfg.show_grid;
    bool selection_show = graphics_cfg.selection_show;
    bool selection_show_center_of_mass = graphics_cfg.selection_show_center_of_mass;
    bool follow_selected = graphics_cfg.follow_selected;
    bool selection_show_orbit = graphics_cfg.selection_show_orbit;
    bool show_labels = graphics_cfg.named_bodies_show_labels;
    bool show_named_orbits = false;  // not in the config
    bool scale_named_by_mass = graphics_cfg.named_bodies_scale_by_mass;
    std::chrono::duration<double> trails_fade = graphics_cfg.trails_fade;
    Config::Graphics::ColorMode color_mode = graphics_cfg.color_mode;
    double exposure_ev = graphics_cfg.exposure_ev;
    std::optional<float> locked_white{};  // bodies/px, while the exposure is locked
    bool bloom = graphics_cfg.bloom;
    int stretch_steps = 0;  // faint light lifted STRETCH_FACTOR times a step, not in the config
    std::vector<sf::Vector2<double>> body_positions_cache;
    // Velocities read right after the positions, for the bodies whose orbits are worked out this
    // frame: see snapshot_velocities()
    std::vector<sf::Vector2<double>> body_velocities_cache;
    StopWatch sw;
    sf::VertexArray body_vertex_array;
    sf::RenderWindow& window;
    HdrRenderer renderer{window.getSize()};
    StopWatch frame_sw;  // since the last frame, for the trail fade and the exposure
    // Speed and Doppler: velocities relative to the followed selection's, else to the
    // barycentre's, coloured up to velocity_scale (m/s), measured on screen
    sf::Vector2<double> barycentre_velocity;
    float velocity_scale = 1.f;
    bool velocity_scale_known = false;
    RLCaller velocity_scale_rate_limiter{Constants::Graphics::VELOCITY_SCALE_UPDATE_TIMER};
    StopWatch velocity_scale_sw;  // since its last measurement
    std::vector<float> velocity_samples;
    ViewPort vp;
    Selector selector;
    IdGroups id_groups = group_ids(bodies, Constants::Graphics::NAMED_GROUP_MAX_SIZE,
            Constants::Graphics::NAMED_SEQUENCE_MAX_SIZE);
    NamedBodies named_bodies{bodies, id_groups};
    RenderGroups render_groups{bodies, id_groups, graphics_cfg.group_styles};
    std::vector<sf::Color> marker_colors;  // per named body, see update_marker_colors()
    std::vector<int32_t> primaries;  // per named body, see Orbit::find_primaries
    RLCaller primaries_update_rate_limiter{Constants::Graphics::PRIMARIES_UPDATE_TIMER};
    std::optional<Orbit::BodyOrbit> selection_orbit{};
    uint64_t frame = 0;
    bool paused = false;
    std::optional<Selector::SelectionStats> cached_selection_stats{};
    // The selection changed: the inspector shows it once the frame has worked out its orbit
    bool inspector_update_pending = false;
    std::optional<sf::Vector2i> opt_view_grabbed_pos{};
    std::optional<sf::Vector2i> opt_select_grabbed_pos{};
    float select_grab_max_drag_px = 0.f;  // how far the cursor went from the right press
    // Hovering: the cursor if bodies under it are looked for this frame, and the hovered body
    bool cursor_in_window = true;
    std::optional<sf::Vector2f> hover_cursor{};
    std::optional<uint32_t> hovered_body{};
    sf::Vector2f tooltip_size{};  // when it was last drawn
    // Orbits' segments and dashes (the selection's, every named body's), reused every frame
    std::vector<Orbit::Segment> orbit_segments;
    sf::VertexArray orbit_lines{sf::PrimitiveType::Lines};
    sf::VertexArray named_orbit_lines{sf::PrimitiveType::Lines};
    sf::CircleShape apsis_marker{Constants::Graphics::APSIS_MARKER_RADIUS_PX};
    sf::Text apsis_label;
    sf::Shader marker_shader{};
    PanelManager panel_manager{};
    ConfigPanel config_panel{Constants::Graphics::CONFIG_PANEL_WIDTH};
    StatsPanel stats_panel{Constants::Graphics::STATS_PANEL_WIDTH};
    CommandsPanel commands_panel{Constants::Graphics::COMMANDS_PANEL_WIDTH};
    InspectorPanel inspector_panel{Constants::Graphics::INSPECTOR_PANEL_WIDTH};
    ActionLogPanel action_log_panel{Constants::Graphics::COMMANDS_PANEL_WIDTH};
    LegendPanel legend_panel{Constants::Graphics::LEGEND_PANEL_WIDTH};
    ActionLog action_log{};
    BufferedMeanCalculator<float, 60> fps_calculator{};
    Stats stats{};
    RLCaller stats_update_rate_limiter{Constants::Graphics::STATS_UPDATE_TIMER};
    uint8_t body_diameter_pixels = Constants::Graphics::INIT_BODY_PIXEL_DIAMETER;

    void pan_if_view_grabbed();
    void update_select_drag();
    bool is_box_selecting() const;
    void cancel_select();
    void update_hover_cursor();
    void snapshot_velocities();
    void update_primaries();
    std::optional<Orbit::BodyOrbit> body_orbit(uint32_t index);
    float hover_radius() const;
    std::optional<uint32_t> transform_bodies();
    std::optional<uint32_t> pick_body(sf::Vector2f point,
            std::optional<uint32_t> heaviest_near) const;
    void update_hover(std::optional<uint32_t> heaviest_near_cursor);
    void draw_grid();
    sf::Vector2<double> reference_velocity() const;
    void write_velocities();
    void update_velocity_scale();
    void draw_bodies();
    void update_marker_colors();
    bool append_orbit(const Orbit::BodyOrbit& orbit, const ScreenTransform& transform,
            sf::Color color, sf::VertexArray& lines);
    void draw_named_orbits();
    void draw_selection_orbit();
    void draw_named_bodies();
    void update_selection_tracking();
    void update_selection_orbit();
    void update_inspector_panel();
    void update_legend_panel();
    void update_groups();
    void draw_selection_overlay();
    void draw_hover_ring();
    void draw_tooltip();
    void update_stats();
    void draw_ui();
    void reset_trails(bool resize = false);
};
