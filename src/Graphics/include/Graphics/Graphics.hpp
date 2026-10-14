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
#include "Panel/Panel.hpp"
#include "Panel/PanelManager.hpp"
#include "Panel/StatsPanel.hpp"
#include "RLCaller/RLCaller.hpp"
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

    Graphics(const Config& cfg, const Bodies& bodies);
    sf::RenderWindow& get_window();
    void resize_view(sf::Vector2f new_size);
    void zoom_view(double delta);
    void grab_view();
    // A release close enough to the press is a click, which selects the body under the cursor
    void release_view(bool allow_click = true);
    void grab_select();
    void release_select(bool skip_select = false);
    void body_size_increase();
    void body_size_decrease();
    void trails_length_increase();
    void trails_length_decrease();
    void toggle_grid();
    void toggle_selection_show();
    void toggle_selection_show_center_of_mass();
    void toggle_follow_selected();
    void toggle_labels();
    void toggle_mass_scaled_markers();
    void center_on_selection_center_of_mass();
    void toggle_config_panel();
    void toggle_stats_panel();
    void toggle_commands_panel();
    void toggle_action_log_panel();
    void toggle_inspector_panel();
    void set_cursor_in_window(bool in_window);
    void notify_paused();
    void notify_resumed();
    void notify_timestep_changed(double old_dt, double new_dt);
    void update_simulation_stats(const SimulationStats& sim_stats);
    void draw_frame();

private:
    struct Stats {
        double timestamp_s;
        uint64_t frame;
        float fps;
    };
    // A target's attractor and elements this frame
    struct OrbitView {
        Orbit::Attractor attractor;
        Orbit::Elements elements;
        bool retrograde;
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
    bool scale_named_by_mass = graphics_cfg.named_bodies_scale_by_mass;
    std::chrono::duration<double> trails_fade = graphics_cfg.trails_fade;
    std::vector<sf::Vector2<double>> body_positions_cache;
    StopWatch sw;
    sf::VertexArray body_vertex_array;
    sf::RenderWindow window;
    sf::RenderTexture trails_texture1, trails_texture2;
    sf::Sprite trails_sprite{trails_texture1.getTexture()};
    ViewPort vp;
    Selector selector;
    NamedBodies named_bodies{bodies};
    std::vector<int32_t> primaries;  // per named body, see Orbit::find_primaries
    RLCaller primaries_update_rate_limiter{Constants::Graphics::PRIMARIES_UPDATE_TIMER};
    std::optional<OrbitView> selection_orbit{}, hovered_orbit{};
    uint64_t frame = 0;
    bool paused = false;
    std::optional<Selector::SelectionStats> cached_selection_stats{};
    std::optional<sf::Vector2i> opt_view_grabbed_pos{};
    std::optional<sf::Vector2i> opt_select_grabbed_pos{};
    // Left click: where the press was, how far the cursor went, the body under it at the press
    sf::Vector2i view_grab_start{};
    float view_grab_max_drag_px = 0.f;
    std::optional<uint32_t> click_candidate{};
    // Hovering: the cursor if bodies under it are looked for this frame, the nearest body within
    // HOVER_RADIUS_PX that draw_bodies() found, and the hovered body
    bool cursor_in_window = true;
    std::optional<sf::Vector2f> hover_cursor{};
    std::optional<uint32_t> nearest_body_in_hover_radius{};
    std::optional<uint32_t> hovered_body{};
    sf::Text tooltip_text;
    sf::RectangleShape tooltip_box{};
    sf::Shader body_shader{};
    sf::Shader fade_shader{};
    sf::Shader marker_shader{};
    PanelManager panel_manager{};
    ConfigPanel config_panel{Constants::Graphics::CONFIG_PANEL_WIDTH};
    StatsPanel stats_panel{Constants::Graphics::STATS_PANEL_WIDTH};
    CommandsPanel commands_panel{Constants::Graphics::COMMANDS_PANEL_WIDTH};
    InspectorPanel inspector_panel{Constants::Graphics::INSPECTOR_PANEL_WIDTH};
    ActionLogPanel action_log_panel{Constants::Graphics::COMMANDS_PANEL_WIDTH};
    ActionLog action_log{};
    BufferedMeanCalculator<float, 60> fps_calculator{};
    Stats stats{};
    RLCaller stats_update_rate_limiter{Constants::Graphics::STATS_UPDATE_TIMER};
    uint8_t body_diameter_pixels = Constants::Graphics::INIT_BODY_PIXEL_DIAMETER;
    StopWatch last_trail_fade_sw;
    float remainder_fractional_fade_chunks = 0.f;

    void pan_if_view_grabbed();
    void update_hover_cursor();
    void update_primaries();
    template <typename Excluded>
    std::optional<OrbitView> find_orbit(const Orbit::Target& target, Excluded&& excluded) const;
    void draw_grid();
    void draw_bodies();
    void update_hover();
    void draw_named_bodies();
    void update_selection_tracking();
    void update_selection_orbit();
    void update_inspector_panel();
    void select_body(uint32_t index);
    void draw_selection_overlay();
    void draw_hover_ring();
    void draw_tooltip();
    void update_stats();
    void draw_ui();
    void reset_trails(bool resize = false);
    void set_fade_shader_decay();
};
