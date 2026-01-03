#pragma once

#include <barrier>

#include "Simulation/Simulation.hpp"

class BarnesHut : public Simulation {
public:
    BarnesHut(const Config::Simulation &sim_cfg, Bodies &bodies);
    ~BarnesHut() override;
private:
    const uint16_t n_threads;
    Quadtree qtree;
    std::thread master;
    std::vector<std::thread> workers;
    const uint64_t worker_chunk;
    const uint64_t master_offset;
    std::barrier<> sync_point;
    std::atomic<bool> worker_stop;
    StopWatch sw_tree{StopWatch::State::PAUSED};
    StopWatch sw_vel{StopWatch::State::PAUSED};
    StopWatch sw_pos{StopWatch::State::PAUSED};

    void on_run() override;
    void on_pause() override;
    void simulate();
    void worker_task(uint32_t worker_id);
    void update_positions(uint64_t begin_idx, uint64_t end_idx);
    void update_velocities(uint64_t begin_idx, uint64_t end_idx);
    void update_velocity(uint64_t body_idx);
    sf::Vector2<double> body_to_quad_force(uint64_t body_idx, const Quad &quad);
};
