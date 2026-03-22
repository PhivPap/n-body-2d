#pragma once

#include "Simulation/Simulation.hpp"

struct Vector2 {
    double x, y;
};

struct Rect {
    Vector2 position;
    Vector2 size;
};

struct QUAD {
    enum class Quadrant : uint8_t {
        TopLeft = 0, TopRight = 1, BotRight = 2, BotLeft = 3
    };
    Rect boundaries;
    double mass;
    Vector2 center_of_mass;
    int32_t start, end;
    bool is_leaf;
    double width_sq;
};

class BarnesHutCuda : public Simulation {
public:
    struct Box { double x_min, x_max, y_min, y_max; };
    
    BarnesHutCuda(const Config::Simulation &sim_cfg, Bodies &bodies);
    virtual ~BarnesHutCuda();
private:
    std::thread sim_thread;
    Box bounding_box;
    const int32_t max_quads;

    // GPU pointers
    double *mass_d = nullptr;
    double *mass_alt_d = nullptr;
    Vector2 *pos_d = nullptr;
    Vector2 *pos_alt_d = nullptr;
    Vector2 *vel_d = nullptr;
    Vector2 *vel_alt_d = nullptr;
    int32_t *idx_d = nullptr;
    int32_t *idx_alt_d = nullptr;
    Box *bounding_box_d = nullptr;
    QUAD *quads_d = nullptr;
    int32_t *work_list_d = nullptr;
    int32_t *work_list_next_d = nullptr;
    int32_t *work_count_d = nullptr;

    void on_run() override;
    void on_pause() override;
    void simulate();

    void init_device_resources();
    void release_device_resources();
    void sync_from_gpu_bodies();
    void compute_bounding_box();
    void build_quad_tree();
    void compute_forces();
    void update_positions();
};
