#pragma once

#include <mutex>

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

    Simulation(const Config::Simulation &sim_cfg, Bodies &bodies);
    virtual ~Simulation();
    State get_state();
    Stats get_stats();
    bool is_finished() const;
    void run();
    void pause();
    void set_timestep(double timestep);
protected:
    Bodies &bodies;
    const double epsilon_squared;
    const uint64_t max_iterations;
    std::atomic<double> requested_timestep;
    double timestep;
    uint64_t iteration = 0;
    std::atomic<bool> finished{false};
    std::atomic<bool> stop{false};
    BufferedMeanCalculator<float, 60> ips_calculator{};

    bool should_stop();
    void post_iteration();
    virtual void on_run() = 0;
    virtual void on_pause() = 0;
    sf::Vector2<double> force(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b, 
            double mass_a, double mass_b) const;
    static double compute_plummer_softening(const Bodies &bodies, double factor, 
            double max_samples=Constants::Simulation::MAX_PAIRWISE_SOFTENING_COMPUTATIONS);
    static double distance(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b);
    static double distance_squared(const sf::Vector2<double> &pos_a, 
            const sf::Vector2<double> &pos_b);
private:
    std::mutex state_mtx;
    std::mutex stats_mtx;
    State state{State::PAUSED};
    Stats stats{};
    StopWatch sw {StopWatch::State::PAUSED};
    RLCaller stats_update_rate_limiter{Constants::Simulation::STATS_UPDATE_TIMER};

    void update_stats();
};
