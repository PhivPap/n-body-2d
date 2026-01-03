#include "Simulation/Simulation.hpp"

#include <stack>
#include <random>

#include "Logger/Logger.hpp"
#include "Logger/Distance.hpp"
#include "Constants/Constants.hpp"


static inline constexpr double distance(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
}

static constexpr double distance_squared(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return diff.x * diff.x + diff.y * diff.y;
}

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

double Simulation::compute_plummer_softening(const Bodies &bodies, double factor, 
        double max_samples) {
    const auto n = bodies.n;
    if (factor == 0.0) {
        Log::info("Plummer softening disabled (softening_factor=0)");
        return 0.0;
    }
    if (n <= 1) {
        return 0.0;
    }

    const auto compute_avg_pairwise_distance = [&bodies, n]() -> double {
        double dist_sum = 0.0;
        for (uint64_t i = 0; i < n; i++) {
            for (uint64_t j = i + 1; j < n; j++) {
                dist_sum += distance(bodies.pos(i), bodies.pos(j));
            }
        }
        const auto avg_pairwise_distance = dist_sum / n;
        Log::info("Average inter-body distance: {}", Log::Distance::from(avg_pairwise_distance));
        return dist_sum / n;
    };

    const auto estimate_avg_pairwise_distance = [&bodies, n, samples=max_samples]() -> double {
        std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<size_t> uniform(0, n - 1);
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

    const auto avg_distance = n * n <= max_samples ? 
            compute_avg_pairwise_distance() : estimate_avg_pairwise_distance();
    const auto epsilon = avg_distance * factor;
    Log::info("Plummer softening (epsilon): {:.5g} (softening_factor={})", epsilon, factor);
    return epsilon;
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

AllPairsSim::AllPairsSim(const Config::Simulation &sim_cfg, Bodies &bodies) : 
        Simulation(sim_cfg, bodies) {}

AllPairsSim::~AllPairsSim() {
    if (sim_thread.joinable()) {
        on_pause();
    }
}

void AllPairsSim::on_run() {
    stop = false;
    sim_thread = std::thread(&AllPairsSim::simulate, this);
}

void AllPairsSim::on_pause() {
    stop = true;
    sim_thread.join();
}

void AllPairsSim::simulate() {
    while (!should_stop()) {
        update_positions();
        update_velocities();
        post_iteration();
    }
}

void AllPairsSim::update_positions() {
    for (uint64_t i = 0; i < bodies.n; i++) {
        bodies.pos(i) += bodies.vel(i) * timestep;
    }
}

// This algorithm only iterates each pair once calculating the forces both ways
void AllPairsSim::update_velocities() {
    for (uint64_t i = 0; i < bodies.n; i++) {
        sf::Vector2<double> force_sum = {0.0, 0.0};
        for (uint64_t j = i + 1; j < bodies.n; j++) {
            const sf::Vector2<double> force = gravitational_force(bodies.pos(i), bodies.pos(j), 
                    bodies.mass(i), bodies.mass(j));
            bodies.vel(j) -= force / bodies.mass(j) * timestep;
            force_sum += force;
        }
        bodies.vel(i) += force_sum / bodies.mass(i) * timestep;
    }
}

sf::Vector2<double> AllPairsSim::gravitational_force(const sf::Vector2<double> &pos_a, 
            const sf::Vector2<double> &pos_b, double mass_a, double mass_b) {
    const double dist = distance(pos_a, pos_b);
    const double force_amplitude = Constants::Simulation::G * mass_a * mass_b / 
            (dist * dist + epsilon_squared);
    return force_amplitude * (pos_b - pos_a) / dist;
}

BarnesHutSim::BarnesHutSim(const Config::Simulation &sim_cfg, Bodies &bodies) : 
        Simulation(sim_cfg, bodies), n_threads(sim_cfg.threads), 
        worker_chunk(bodies.n / n_threads), master_offset(worker_chunk * (n_threads - 1)), 
        sync_point(n_threads) {
    if (sim_cfg.threads == 0)
        throw std::runtime_error("Thread count 0 is invalid");
    if (sim_cfg.threads > bodies.n)
        throw std::runtime_error("Threads must be less than the number of bodies");
    workers.reserve(sim_cfg.threads - 1);
}

BarnesHutSim::~BarnesHutSim() {
    if (master.joinable()) {
        on_pause();
    }

    StopWatch sw_total = sw_tree + sw_vel + sw_pos;
    Log::debug("Tree: [{}] ({})", sw_tree, sw_tree / sw_total);
    Log::debug("Vel:  [{}] ({})", sw_vel, sw_vel / sw_total);
    Log::debug("Pos:  [{}] ({})", sw_pos, sw_pos / sw_total);
}

void BarnesHutSim::on_run() {
    stop = false;
    worker_stop = false;
    master = std::thread(&BarnesHutSim::simulate, this);
    for (uint32_t i = 0; i < n_threads - 1; i++) {
        workers.emplace_back(&BarnesHutSim::worker_task, this, i);
    }
}

void BarnesHutSim::on_pause() {
    stop = true;
    master.join();
    worker_stop = true;
    std::ignore = sync_point.arrive();
    for (auto &worker : workers) {
        worker.join();
    }
    workers.clear();
}

void BarnesHutSim::simulate() {
    while (!should_stop()) {
        sw_tree.resume();
        qtree.build_tree(bodies);
        sw_tree.pause();

        sync_point.arrive_and_wait();

        sw_vel.resume();
        update_velocities(master_offset, bodies.n);
        sw_vel.pause();

        sw_pos.resume();
        update_positions(master_offset, bodies.n);
        sw_pos.pause();

        sync_point.arrive_and_wait();
        post_iteration();
    }
}

void BarnesHutSim::worker_task(uint32_t worker_id) {
    const uint64_t begin_idx = worker_id * worker_chunk;
    const uint64_t end_idx =  begin_idx + worker_chunk;
    while (true) {
        sync_point.arrive_and_wait();
        if (worker_stop)
            return;
        update_velocities(begin_idx, end_idx);
        update_positions(begin_idx, end_idx);
        sync_point.arrive_and_wait();
    }
}

void BarnesHutSim::update_positions(uint64_t begin_idx, uint64_t end_idx) {
    for (uint64_t idx = begin_idx; idx < end_idx; idx++) {
        bodies.pos(idx) += bodies.vel(idx) * timestep;
    }
}

void BarnesHutSim::update_velocities(uint64_t begin_idx, uint64_t end_idx) {
    for (uint64_t idx = begin_idx; idx < end_idx; idx++) {
        update_velocity(idx);
    }
}

// iterative DFS
void BarnesHutSim::update_velocity(uint64_t body_idx) {
    std::vector<uint32_t> quad_idx_stack;
    quad_idx_stack.reserve(2000);
    quad_idx_stack.push_back(0);

    sf::Vector2<double> F = {0.0, 0.0};
    
    while (!quad_idx_stack.empty()) {
        const Quad &quad = qtree.quads[quad_idx_stack.back()];
        quad_idx_stack.pop_back();
        if (quad.is_leaf()) {
            if (quad.total_mass != 0 && quad.center_of_mass != bodies.pos(body_idx)) {
                F += body_to_quad_force(body_idx, quad);
            }
        }
        else {
            const double dist_squared = (bodies.pos(body_idx) - quad.center_of_mass)
                    .lengthSquared();
            if (quad.boundaries.size.lengthSquared() / 
                    dist_squared < Constants::Simulation::THETA) {
                F += body_to_quad_force(body_idx, quad);
            }
            else {
                quad_idx_stack.push_back(quad.top_left_idx + 3);
                quad_idx_stack.push_back(quad.top_left_idx + 2);
                quad_idx_stack.push_back(quad.top_left_idx + 1);
                quad_idx_stack.push_back(quad.top_left_idx);
            }
        }
    }

    bodies.vel(body_idx) += F / bodies.mass(body_idx) * timestep;
}

sf::Vector2<double> BarnesHutSim::body_to_quad_force(uint64_t body_idx, const Quad &quad) {
    const double dist = distance(bodies.pos(body_idx), quad.center_of_mass);
    const double force_amplitude = Constants::Simulation::G * bodies.mass(body_idx) * 
            quad.total_mass / (dist * dist + epsilon_squared);
    return ((quad.center_of_mass - bodies.pos(body_idx)) / dist) * force_amplitude;
}
