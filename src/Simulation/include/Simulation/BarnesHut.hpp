#pragma once

#include <barrier>

#include "Simulation/Simulation.hpp"


class BarnesHut : public Simulation {
public:
    BarnesHut(const Config::Simulation& sim_cfg, Bodies& bodies);
    ~BarnesHut() override;

private:
    const uint16_t n_threads;
    const double theta_sq;
    Quadtree qtree;
    std::vector<std::thread> workers;
    const uint64_t worker_chunk;
    const uint64_t master_offset;
    std::barrier<> sync_point;
    std::atomic<bool> worker_stop;
    double step_kick = 0.0;  // of the current step, for the workers
    double step_drift = 0.0;
    bool tree_built = false;
    std::mutex step_error_mtx;
    std::exception_ptr step_error;
    StopWatch sw_tree{StopWatch::State::PAUSED};
    StopWatch sw_vel{StopWatch::State::PAUSED};
    StopWatch sw_pos{StopWatch::State::PAUSED};

    void on_run() override;
    void on_pause() override;
    void step(double kick, double drift) override;
    void worker_task(uint32_t worker_id);
    void record_step_error();
    void update_positions(uint64_t begin_idx, uint64_t end_idx);
    void update_velocities(uint64_t begin_idx, uint64_t end_idx);
    void update_velocity(uint64_t body_idx);
    sf::Vector2<double> body_to_quad_acceleration(uint64_t body_idx, const Quad& quad);
};
