#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <barrier>

#include "Config/Config.hpp"
#include "Body/Body.hpp"
#include "StopWatch/StopWatch.hpp"
#include "Quadtree/Quadtree.hpp"

class Simulation {
public:
    enum class State : bool { PAUSED, RUNNING };

    Simulation(const Config &cfg, std::vector<Body> &bodies);
    virtual ~Simulation();

    State get_state();
    bool is_finished() const;
    void run();
    void pause();

protected:
    void set_finished();
    virtual void on_run() = 0;
    virtual void on_pause() = 0;

    const Config &cfg;
    std::vector<Body> &bodies;
    uint64_t iteration;
    std::atomic<bool> finished{false};
    
private:
    std::mutex state_mtx;
    State state{State::PAUSED};
    StopWatch sw {StopWatch::State::PAUSED};
};

class AllPairsSim : public Simulation {
private:
    void on_run() override;
    void on_pause() override;
    void simulate();
    void update_positions();
    void update_velocities();
    sf::Vector2<double> gravitational_force(const Body &body_a, const Body &body_b);

    std::thread sim_thread;
    std::atomic<bool> stop;
public:
    AllPairsSim(const Config &cfg, std::vector<Body> &bodies);
    ~AllPairsSim() override;
};

class BarnesHutSim : public Simulation {
private:
    Quadtree qtree;
    std::thread master;
    std::vector<std::thread> workers;
    const uint64_t worker_chunk;
    const uint64_t master_offset;
    std::barrier<> sync_point;
    std::atomic<bool> stop;
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
    void update_velocity(Body &body);
    double compute_softening(const Body &body, const Quad &quad);
    sf::Vector2<double> body_to_quad_force(const Body &body, const Quad &quad);

public:
    BarnesHutSim(const Config &cfg, std::vector<Body> &bodies);
    virtual ~BarnesHutSim();
};
