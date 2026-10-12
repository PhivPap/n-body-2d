#pragma once

#include <exception>
#include <mutex>
#include <thread>

#include "Body/Body.hpp"
#include "BufferedMeanCalculator/BufferedMeanCalculator.hpp"
#include "Config/Config.hpp"
#include "Constants/Constants.hpp"
#include "Quadtree/Quadtree.hpp"
#include "RLCaller/RLCaller.hpp"
#include "StopWatch/StopWatch.hpp"

class Simulation {
public:
    enum class State : bool { PAUSED, RUNNING };

    struct Stats {
        uint64_t iteration = 0;
        float ips = 0.0;
        double real_elapsed_s = 0;
        double simulated_elapsed_s = 0;
    };

    Simulation(const Config::Simulation& sim_cfg, Bodies& bodies);
    virtual ~Simulation();
    State get_state();
    Stats get_stats();
    // True once the simulation ran all its iterations or failed
    bool is_finished() const;
    void run();
    void pause();
    void set_timestep(double timestep);
    // The timestep set last, which the simulation uses from its next iteration
    double get_timestep() const;
    // Rethrows the exception that stopped the simulation thread, if any (after pause())
    void rethrow_error() const;

protected:
    const Config::Simulation& sim_cfg;
    Bodies& bodies;
    const double epsilon_squared;
    std::atomic<double> requested_timestep;
    double timestep;  // of the current iteration
    uint64_t iteration = 0;
    std::atomic<bool> finished{false};
    BufferedMeanCalculator<float, 60> ips_calculator{};

    static double compute_plummer_softening(const Bodies& bodies, const Config::Simulation& sim_cfg,
            double max_samples = Constants::Simulation::MAX_PAIRWISE_SOFTENING_COMPUTATIONS);
    static double distance(const sf::Vector2<double>& pos_a, const sf::Vector2<double>& pos_b);
    static double distance_squared(const sf::Vector2<double>& pos_a,
            const sf::Vector2<double>& pos_b);

    // One iteration, on the simulation thread that run() starts and pause() joins. Derived
    // destructors must pause a running simulation, as the thread uses them.
    virtual void step() = 0;
    // Brings `bodies` up to date, on the simulation thread before it stops
    virtual void sync_bodies_to_host() {}
    // On the thread calling run() or pause(), before the simulation thread starts or after it
    // stopped
    virtual void on_run() {}
    virtual void on_pause() {}
    // Acceleration of a body at pos_a towards a unit mass at pos_b
    sf::Vector2<double> pull(const sf::Vector2<double>& pos_a, const sf::Vector2<double>& pos_b) const;


private:
    std::mutex state_mtx;
    std::mutex stats_mtx;
    State state{State::PAUSED};
    Stats stats{};
    StopWatch sw{StopWatch::State::PAUSED};
    RLCaller stats_update_rate_limiter{Constants::Simulation::STATS_UPDATE_TIMER};
    std::thread sim_thread;
    std::atomic<bool> stop{false};
    std::exception_ptr error;

    void simulate();
    void post_iteration();
    void update_stats();
};
