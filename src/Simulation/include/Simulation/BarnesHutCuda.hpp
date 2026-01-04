#pragma once

#include "Simulation/Simulation.hpp"

class BarnesHutCuda : public Simulation {
public:
    struct Box { double x_min, x_max, y_min, y_max; };
    
    BarnesHutCuda(const Config::Simulation &sim_cfg, Bodies &bodies);
    virtual ~BarnesHutCuda();
private:
    std::thread sim_thread;
    Box bounding_box;
    double inv_extent;

    // GPU pointers
    double *mass_d = nullptr;
    sf::Vector2<double> *pos_d = nullptr;
    sf::Vector2<double> *vel_d = nullptr;
    Box *bounding_box_d = nullptr;
    sf::Vector2<double> *pos_sorted_d = nullptr;
    uint32_t *morton_d = nullptr;
    uint32_t *indices = nullptr;
    // Quad *quad = nullptr; 

    void on_run() override;
    void on_pause() override;
    void simulate();

    void init_device_resources();
    void release_device_resources();
    void sync_from_gpu_bodies();
    void compute_bounding_box();
    void compute_morton_codes();
    void sort_by_morton_code();
};
