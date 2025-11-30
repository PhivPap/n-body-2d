#pragma once

#include "SFML/System.hpp"
#include "SFML/Graphics.hpp"

class Panel : public sf::Drawable, public sf::Transformable {
public:
    struct DisplayedData {
        double timestep_s;
        std::string algorithm;
        double theta;
        uint32_t threads;
        sf::Vector2<double> viewport_m;
        sf::Vector2<uint32_t> viewport_px;
        bool vsync;
        bool grid;
        uint32_t max_fps;

        uint64_t iteration;
        double iter_per_sec;
        uint64_t frame;
        float fps;
        double elapsed_s;
        double simulated_time_s;

        std::string fmt_timestep() const;
        std::string fmt_algorithm() const;
        std::string fmt_theta() const;
        std::string fmt_threads() const;
        std::string fmt_viewport_m() const;
        std::string fmt_viewport_px() const;
        std::string fmt_vsync() const;
        std::string fmt_grid() const;
        std::string fmt_max_fps() const;
        std::string fmt_iteration() const;
        std::string fmt_iter_per_sec() const;
        std::string fmt_frame() const;
        std::string fmt_fps() const;
        std::string fmt_elapsed() const;
        std::string fmt_sim_time() const;
        std::string fmt_sim_rate() const;
        std::string to_string();
    };

    Panel(sf::Vector2u size);
    void set_visible(bool visible);
    void update_displayed_data(DisplayedData &&displayed_data);
private:
    sf::RenderTexture texture;
    sf::Sprite sprite;
    sf::Text text;
    DisplayedData displayed_data;
    bool visible;

    void bake();
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override;
};
