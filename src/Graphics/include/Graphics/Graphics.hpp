#pragma once

#include <vector>

#include "Config/Config.hpp"
#include "Body/Body.hpp"
#include "ViewPort/ViewPort.hpp"
#include "RLCaller/RLCaller.hpp"
#include "BufferedMeanCalculator/BufferedMeanCalculator.hpp"
#include "Panel/Panel.hpp"
#include "Selector/Selector.hpp"
#include "StopWatch/StopWatch.hpp"
#include "Constants/Constants.hpp"
#include "Panel/PanelManager.hpp"
#include "Panel/ConfigPanel.hpp"
#include "Panel/StatsPanel.hpp"
#include "Panel/CommandsPanel.hpp"


class Graphics {
public:
    struct Stats {
        double timestamp_s;
        uint64_t frame;
        float fps;
        sf::Vector2<double> viewport_m;
        sf::Vector2<uint32_t> viewport_px;
    };

    Graphics(const Config::Graphics &graphics_cfg, const Bodies &bodies);
    Stats get_stats() const;
    sf::RenderWindow &get_window();
    CommandsPanel &get_commands_panel();
    ConfigPanel &get_config_panel();
    StatsPanel &get_stats_panel();

    void resize_view(sf::Vector2f new_size);
    void zoom_view(double delta);
    void grab_view();
    void release_view();
    void grab_select();
    void release_select(bool skip_select = false);
    void body_size_increase();
    void body_size_decrease();

    void set_grid(bool enabled);
    void draw_frame();

private:
    const Bodies &bodies;
    StopWatch sw;
    sf::VertexArray body_vertex_array;
    sf::RenderWindow window;
    ViewPort vp;
    Selector selector;
    uint64_t frame = 0;
    bool show_grid;
    std::optional<sf::Vector2i> opt_view_grabbed_pos{};
    std::optional<sf::Vector2i> opt_select_grabbed_pos{};
    sf::Shader body_shader{};
    PanelManager panel_manager{};
    ConfigPanel config_panel{Constants::Graphics::CONFIG_PANEL_RES};
    StatsPanel stats_panel{Constants::Graphics::STATS_PANEL_RES};
    CommandsPanel commands_panel{Constants::Graphics::COMMANDS_PANEL_RES};
    BufferedMeanCalculator<float, 60> fps_calculator{};
    Stats stats{};
    RLCaller stats_update_rate_limiter{Constants::Graphics::STATS_UPDATE_TIMER};
    uint8_t body_diameter_pixels = Constants::Graphics::INIT_BODY_PIXEL_DIAMETER;

    void pan_if_view_grabbed();
    void draw_grid();
    void draw_bodies();
    void draw_selector();
    void update_stats();
};
