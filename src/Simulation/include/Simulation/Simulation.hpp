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
private:
    enum class State : uint8_t { PAUSED, RUNNING, COMPLETED, TERMINATED };
    State state {State::PAUSED};
    StopWatch sw {StopWatch::State::PAUSED};
    std::thread sim_thread;
    std::mutex state_mtx;
    std::condition_variable state_cv;

    void simulate();
protected:
    const Config &cfg;
    std::vector<Body> &bodies;

    virtual void iteration() = 0;
public:
    Simulation(const Config &cfg, std::vector<Body> &bodies);
    virtual ~Simulation();
    void run();
    void pause();
    void terminate();
    bool completed();
};

class NaiveSim : public Simulation {
private:
    void update_positions();
    void update_velocities();
    sf::Vector2<double> gravitational_force(const Body &body_a, const Body &body_b);
    virtual void iteration();
    
public:
    NaiveSim(const Config &cfg, std::vector<Body> &bodies);
    virtual ~NaiveSim();
};

class BarnesHutSim : public Simulation {
private:
    Quadtree qtree;
    std::vector<std::jthread> workers;
    const uint64_t worker_chunk;
    const uint64_t master_offset;
    std::barrier<> sync_point;
    volatile bool worker_quit = false;
    StopWatch sw_tree{StopWatch::State::PAUSED};
    StopWatch sw_vel{StopWatch::State::PAUSED};
    StopWatch sw_pos{StopWatch::State::PAUSED};

    void worker_task(uint32_t worker_id);
    void update_positions(uint64_t begin_idx, uint64_t end_idx);
    void update_velocities(uint64_t begin_idx, uint64_t end_idx);
    void update_velocity(Body &body);
    double compute_softening(const Body &body, const Quad &quad);
    sf::Vector2<double> body_to_quad_force(const Body &body, const Quad &quad);
    virtual void iteration();

public:
    BarnesHutSim(const Config &cfg, std::vector<Body> &bodies);
    virtual ~BarnesHutSim();
};
