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
#include "RLCaller/RLCaller.hpp"
#include "BufferedMeanCalculator/BufferedMeanCalculator.hpp"
#include "Constants/Constants.hpp"

class Simulation {
public:
    enum class State : bool { PAUSED, RUNNING };

    struct Stats {
        uint64_t iteration = 0;
        float ips = 0.0;
        double real_elapsed_s = 0;
        double simulated_elapsed_s = 0;
    };

    Simulation(const Config::Simulation &sim_cfg, std::vector<Body> &bodies);
    virtual ~Simulation();

    State get_state();
    Stats get_stats();
    bool is_finished() const;
    void run();
    void pause();

protected:
    static double compute_plummer_softening(const std::vector<Body> &bodies, double factor, 
            double max_samples=Constants::Simulation::MAX_PAIRWISE_SOFTENING_COMPUTATIONS);
    bool should_stop();
    void post_iteration();
    virtual void on_run() = 0;
    virtual void on_pause() = 0;

    const Config::Simulation &sim_cfg;
    std::vector<Body> &bodies;
    const double epsilon_squared;
    uint64_t iteration = 0;
    std::atomic<bool> finished{false};
    std::atomic<bool> stop{false};
    BufferedMeanCalculator<float, 60> ips_calculator{};

private:
    void update_stats();

    std::mutex state_mtx;
    std::mutex stats_mtx;
    State state{State::PAUSED};
    Stats stats{};
    StopWatch sw {StopWatch::State::PAUSED};
    RLCaller stats_update_rate_limiter{Constants::Simulation::STATS_UPDATE_TIMER};
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
public:
    AllPairsSim(const Config::Simulation &sim_cfg, std::vector<Body> &bodies);
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
    BarnesHutSim(const Config::Simulation &sim_cfg, std::vector<Body> &bodies);
    virtual ~BarnesHutSim();
};
