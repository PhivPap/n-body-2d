#include "Simulation/Simulation.hpp"

#include <stack>
#include <random>

#include "Logger/Logger.hpp"
#include "Logger/Distance.hpp"
#include "Constants/Constants.hpp"


Simulation::Simulation(const Config::Simulation &sim_cfg, Bodies &bodies) : 
        bodies(bodies), max_iterations(sim_cfg.iterations), requested_timestep(sim_cfg.timestep),
        timestep(sim_cfg.timestep), 
        epsilon_squared(std::pow(compute_plummer_softening(bodies, sim_cfg.softening_factor), 2)) {}

Simulation::~Simulation() {}

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
    on_run();
    sw.resume();
}

void Simulation::pause() {
    std::lock_guard state_lock(state_mtx);
    if (state != State::RUNNING) {
        Log::warning("Cannot pause simulation, it is not running");
        return;
    }
    state = State::PAUSED;
    on_pause();
    sw.pause();
}

void Simulation::set_timestep(double timestep) {
    requested_timestep.store(timestep, std::memory_order::relaxed);
}

bool Simulation::should_stop() {
    if (iteration >= max_iterations) {
        finished = true;
        Log::info("Simulation finshed");
        return true;
    }
    return stop;
}

void Simulation::post_iteration() {
    stats_update_rate_limiter.try_call(std::bind(&Simulation::update_stats, this));
    timestep = requested_timestep.load(std::memory_order::relaxed);
    iteration++;
}

sf::Vector2<double> Simulation::force(const sf::Vector2<double> &pos_a, 
            const sf::Vector2<double> &pos_b, double mass_a, double mass_b) const {
    const double dist = distance(pos_a, pos_b);
    const double force_amplitude = Constants::Simulation::G * mass_a * mass_b / 
            (dist * dist + epsilon_squared);
    return force_amplitude * (pos_b - pos_a) / dist;
}

double Simulation::compute_plummer_softening(const Bodies &bodies, double factor, 
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
        const auto avg_pairwise_distance = dist_sum / bodies.n;
        Log::info("Average inter-body distance: {}", Log::Distance::from(avg_pairwise_distance));
        return avg_pairwise_distance;
    };

    const auto estimate_avg_pairwise_distance = [&bodies, samples=max_samples]() -> double {
        std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<size_t> uniform(0, bodies.n - 1);
        double dist_sum = 0.0;
        for (uint64_t s = 0; s < samples; s++) {
            const uint64_t i = uniform(rng);
            uint64_t j = uniform(rng);
            while (i == j) j = uniform(rng);
            dist_sum += distance(bodies.pos(i), bodies.pos(j));
        }
        const auto avg_pairwise_distance = dist_sum / samples;
        Log::info("Average inter-body distance: {} (estimated with samples={})", 
                Log::Distance::from(avg_pairwise_distance), samples);
        return avg_pairwise_distance;
    };

    const auto avg_distance = bodies.n * bodies.n <= max_samples ? 
            compute_avg_pairwise_distance() : estimate_avg_pairwise_distance();
    const auto epsilon = avg_distance * factor;
    Log::info("Plummer softening (epsilon): {:.5g} (softening_factor={})", epsilon, factor);
    return epsilon;
}

double Simulation::distance(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
}

double Simulation::distance_squared(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b) {
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
