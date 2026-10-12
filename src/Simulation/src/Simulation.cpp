#include "Simulation/Simulation.hpp"

#include <cassert>
#include <random>
#include <stack>

#include "Constants/Constants.hpp"
#include "Logger/Distance.hpp"
#include "Logger/Logger.hpp"


Simulation::Simulation(const Config::Simulation& sim_cfg, Bodies& bodies)
        : sim_cfg(sim_cfg), bodies(bodies),
          epsilon_squared(std::max(
                  std::pow(compute_plummer_softening(bodies, sim_cfg.softening_factor), 2),
                  Constants::Simulation::MIN_EPSILON_SQUARED)),
          requested_timestep(sim_cfg.timestep), timestep(sim_cfg.timestep) {}

Simulation::~Simulation() {
    assert(!sim_thread.joinable());  // derived destructors pause a running simulation
}

Simulation::State Simulation::get_state() {
    std::lock_guard state_lock(state_mtx);
    return state;
}

Simulation::Stats Simulation::get_stats() {
    std::lock_guard stats_lock(stats_mtx);
    return stats;
}

bool Simulation::is_finished() const {
    return finished;
}

void Simulation::run() {
    std::lock_guard state_lock(state_mtx);
    if (state != State::PAUSED) {
        Log::warning("Cannot run simulation, it is not paused");
        return;
    }
    state = State::RUNNING;
    stop = false;
    on_run();
    sim_thread = std::thread(&Simulation::simulate, this);
    sw.resume();
}

void Simulation::pause() {
    std::lock_guard state_lock(state_mtx);
    if (state != State::RUNNING) {
        Log::warning("Cannot pause simulation, it is not running");
        return;
    }
    state = State::PAUSED;
    stop = true;
    sim_thread.join();
    on_pause();
    sw.pause();
}

void Simulation::set_timestep(double timestep) {
    requested_timestep.store(timestep, std::memory_order::relaxed);
}

double Simulation::get_timestep() const {
    return requested_timestep.load(std::memory_order::relaxed);
}

void Simulation::rethrow_error() const {
    if (error) {
        std::rethrow_exception(error);
    }
}

void Simulation::simulate() {
    try {
        while (!stop && iteration < sim_cfg.iterations) {
            step();
            post_iteration();
        }
        // `bodies` must hold the final state before the simulation is reported finished
        sync_bodies_to_host();
    }
    catch (...) {
        // The main thread finds the simulation finished, pauses it and rethrows the error
        error = std::current_exception();
    }
    if (error || iteration >= sim_cfg.iterations) {
        finished = true;
        Log::info("Simulation {}", error ? "stopped by an error" : "finished");
    }
}

void Simulation::post_iteration() {
    stats_update_rate_limiter.try_call(std::bind(&Simulation::update_stats, this));
    timestep = requested_timestep.load(std::memory_order::relaxed);
    iteration++;
}

sf::Vector2<double> Simulation::pull(const sf::Vector2<double>& pos_a,
        const sf::Vector2<double>& pos_b) const {
    const double softened_dist_sq = distance_squared(pos_a, pos_b) + epsilon_squared;
    return Constants::Simulation::G / (softened_dist_sq * std::sqrt(softened_dist_sq))
           * (pos_b - pos_a);
}

double Simulation::compute_plummer_softening(const Bodies& bodies, double factor,
        double max_samples) {
    if (factor == 0.0) {
        Log::info("Plummer softening disabled (softening_factor=0)");
        return 0.0;
    }
    if (bodies.n <= 1) {
        return 0.0;
    }

    const auto compute_avg_pairwise_distance = [&bodies]() -> double {
        double dist_sum = 0.0;
        for (uint64_t i = 0; i < bodies.n; i++) {
            for (uint64_t j = i + 1; j < bodies.n; j++) {
                dist_sum += distance(bodies.pos(i), bodies.pos(j));
            }
        }
        const auto avg_pairwise_distance = dist_sum / (bodies.n * (bodies.n - 1) / 2);
        Log::info("Average inter-body distance: {}", Log::Distance::from(avg_pairwise_distance));
        return avg_pairwise_distance;
    };

    const auto estimate_avg_pairwise_distance = [&bodies, samples = max_samples]() -> double {
        std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<size_t> uniform(0, bodies.n - 1);
        double dist_sum = 0.0;
        for (uint64_t s = 0; s < samples; s++) {
            const uint64_t i = uniform(rng);
            uint64_t j = uniform(rng);
            while (i == j)
                j = uniform(rng);
            dist_sum += distance(bodies.pos(i), bodies.pos(j));
        }
        const auto avg_pairwise_distance = dist_sum / samples;
        Log::info("Average inter-body distance: {} (estimated with samples={})",
                Log::Distance::from(avg_pairwise_distance), samples);
        return avg_pairwise_distance;
    };

    const auto avg_distance = bodies.n * bodies.n <= max_samples ? compute_avg_pairwise_distance()
                                                                 : estimate_avg_pairwise_distance();
    const auto epsilon = avg_distance * factor;
    Log::info("Plummer softening (epsilon): {:.5g} (softening_factor={})", epsilon, factor);
    return epsilon;
}

double Simulation::distance(const sf::Vector2<double>& pos_a, const sf::Vector2<double>& pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
}

double Simulation::distance_squared(const sf::Vector2<double>& pos_a,
        const sf::Vector2<double>& pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return diff.x * diff.x + diff.y * diff.y;
}

void Simulation::update_stats() {
    const auto elapsed_s = sw.elapsed<std::chrono::seconds, 6>();
    std::lock_guard stats_lock(stats_mtx);
    const auto iter_delta = iteration - stats.iteration;
    const auto dt = elapsed_s - stats.real_elapsed_s;
    ips_calculator.register_value(iter_delta / dt);

    stats.iteration = iteration;
    stats.ips = ips_calculator.get_mean<float>();
    stats.real_elapsed_s = elapsed_s;
    stats.simulated_elapsed_s += iter_delta * timestep;
}
