#include "Simulation/Simulation.hpp"

#include <stack>

#include "Logger/Logger.hpp"
#include "Constants/Constants.hpp"

static inline constexpr double distance(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
}

static constexpr double distance_squared(const sf::Vector2<double> &pos_a, const sf::Vector2<double> &pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return diff.x * diff.x + diff.y * diff.y;
}

static constexpr double softening_squared(const sf::Vector2<double> &vel_a, const sf::Vector2<double> &vel_b) {
    return Constants::SOFTENING_FACTOR * (vel_a - vel_b).lengthSquared();
    // return 1e36;
}

Simulation::Simulation(const Config &cfg, std::vector<Body> &bodies) : 
        cfg(cfg), bodies(bodies), sim_thread(&Simulation::simulate, this) {}

Simulation::~Simulation() {
    terminate();
}

void Simulation::simulate() {
    std::unique_lock<std::mutex> state_lock(state_mtx, std::defer_lock);
    uint64_t i;
    for (i = 0; i < cfg.iterations; i++) {
        state_lock.lock();
        state_cv.wait(state_lock, [&]() {return state != State::PAUSED;});
        state_lock.unlock();
        if (state == State::TERMINATED) {
            Log::info("Sim terminated");
            break;
        }
        iteration();
    }
    state_lock.lock();
    sw.pause();
    if (state != State::TERMINATED) {
        Log::info("Sim completed");
        state = State::COMPLETED;
    }
    state_lock.unlock();
    Log::debug("Iterations: {}/{}", i, cfg.iterations);
    Log::debug("Iterations per second: {}", i / sw.elapsed());
}

void Simulation::run() {
    {
        std::lock_guard state_lock(state_mtx);
        if (state != State::PAUSED) {
            Log::warning("Cannot start run, it's not paused");
            return;
        }
        state = State::RUNNING;
        sw.resume();
    }
    state_cv.notify_one();
    Log::info("Sim running");
}

void Simulation::pause() {
    {
        std::lock_guard state_lock(state_mtx);
        if (state != State::RUNNING) {
            Log::warning("Cannot pause simulation, it's not running");
            return;
        }
        state = State::PAUSED; 
        sw.pause();
    }
    Log::info("Sim paused");
}

void Simulation::terminate() {
    {
        std::lock_guard state_lock(state_mtx);
        if (state == State::TERMINATED) {
            return;
        }
        state = State::TERMINATED;
    }
    state_cv.notify_one();
    sim_thread.join();
}

bool Simulation::completed() {
    return state == State::COMPLETED;
}

NaiveSim::NaiveSim(const Config &cfg, std::vector<Body> &bodies) : 
        Simulation(cfg, bodies) {}

NaiveSim::~NaiveSim() {
    terminate();
}

void NaiveSim::iteration() {
    update_positions();
    update_velocities();
}

void NaiveSim::update_positions() {
    for (Body &body : bodies) {
        body.pos += body.vel * cfg.timestep;
    }
}

// This algorithm only iterates each pair once calculating the forces both ways
void NaiveSim::update_velocities() {
    Body* _bodies = bodies.data();
    const uint64_t total_bodies = bodies.size();
    for (uint64_t i = 0; i < total_bodies - 1; i++) {
        Body &body_a = _bodies[i];
        sf::Vector2<double> force_sum = {0.0, 0.0};
        for (uint64_t j = i + 1; j < total_bodies; j++) {
            Body &body_b = _bodies[j];
            const sf::Vector2<double> force = gravitational_force(body_a, body_b);
            body_b.vel -= force / body_b.mass * cfg.timestep;
            force_sum += force;
        }
        body_a.vel += force_sum / body_a.mass * cfg.timestep;
    }
}

sf::Vector2<double> NaiveSim::gravitational_force(const Body &body_a, const Body &body_b) {
    const double dist = distance(body_a.pos, body_b.pos);
    const double epsilon_squared = 
            softening_squared(body_a.vel, body_b.vel);
    const double force_amplitude = Constants::G * body_a.mass * body_b.mass / 
            (dist * dist + epsilon_squared);
    return {
        force_amplitude * (body_b.pos.x - body_a.pos.x) / dist,
        force_amplitude * (body_b.pos.y - body_a.pos.y) / dist
    };
}

BarnesHutSim::BarnesHutSim(const Config &cfg, std::vector<Body> &bodies) : 
        Simulation(cfg, bodies), worker_chunk(bodies.size() / cfg.threads), 
        master_offset(worker_chunk * (cfg.threads - 1)), sync_point(cfg.threads) {
    assert(cfg.threads > 1);
    if (cfg.threads > bodies.size()) {
        Log::warning("Handle this..."); // ...
        throw std::runtime_error("...");
    }
    workers.reserve(cfg.threads - 1);
    for (uint32_t i = 0; i < cfg.threads - 1; i++) {
        workers.emplace_back(&BarnesHutSim::worker_task, this, i);
    }
}

BarnesHutSim::~BarnesHutSim() {
    terminate();
    worker_quit = true;
    sync_point.arrive();

    StopWatch sw_total = sw_tree + sw_vel + sw_pos;
    Log::debug("Tree: [{}] ({})", sw_tree, sw_tree / sw_total);
    Log::debug("Vel:  [{}] ({})", sw_vel, sw_vel / sw_total);
    Log::debug("Pos:  [{}] ({})", sw_pos, sw_pos / sw_total);
}

void BarnesHutSim::worker_task(uint32_t worker_id) {
    const uint64_t begin_idx = worker_id * worker_chunk;
    const uint64_t end_idx =  begin_idx + worker_chunk;
    while (true) {
        sync_point.arrive_and_wait();
        if (worker_quit) {
            return;
        }
        update_velocities(begin_idx, end_idx);
        update_positions(begin_idx, end_idx);
        sync_point.arrive_and_wait();
    }
}

void BarnesHutSim::iteration() {
    sw_tree.resume();
    qtree.build_tree(bodies);
    sw_tree.pause();

    sync_point.arrive_and_wait();

    sw_vel.resume();
    update_velocities(master_offset, bodies.size());
    sw_vel.pause();

    sw_pos.resume();
    update_positions(master_offset, bodies.size());
    sw_pos.pause();

    sync_point.arrive_and_wait();
}

void BarnesHutSim::update_positions(uint64_t begin_idx, uint64_t end_idx) {
    Body* _bodies = bodies.data();
    const uint32_t total_bodies = bodies.size();

    for (uint64_t idx = begin_idx; idx < end_idx; idx++) {
        Body &body = _bodies[idx];
        body.pos += body.vel * cfg.timestep;
    }
}

void BarnesHutSim::update_velocities(uint64_t begin_idx, uint64_t end_idx) {
    for (uint64_t idx = begin_idx; idx < end_idx; idx++) {
        update_velocity(bodies[idx]);
    }
}

// iterative DFS
void BarnesHutSim::update_velocity(Body &body) {
    std::vector<uint32_t> quad_idx_stack;
    quad_idx_stack.reserve(2000);
    quad_idx_stack.push_back(0);

    sf::Vector2<double> F = {0.0, 0.0};
    
    while (!quad_idx_stack.empty()) {
        const Quad &quad = qtree.quads[quad_idx_stack.back()];
        quad_idx_stack.pop_back();
        if (quad.is_leaf()) {
            if (quad.total_mass != 0 && quad.center_of_mass != body.pos) {
                F += body_to_quad_force(body, quad);
            }
        }
        else {
            const double dist_squared = distance_squared(body.pos, quad.center_of_mass);
            if (quad.boundaries.size.lengthSquared() / dist_squared < Constants::THETA) {
                F += body_to_quad_force(body, quad);
            }
            else {
                quad_idx_stack.push_back(quad.top_left_idx + 3);
                quad_idx_stack.push_back(quad.top_left_idx + 2);
                quad_idx_stack.push_back(quad.top_left_idx + 1);
                quad_idx_stack.push_back(quad.top_left_idx);
            }
        }
    }

    body.vel += F / body.mass * cfg.timestep;
}

sf::Vector2<double> BarnesHutSim::body_to_quad_force(const Body &body, const Quad &quad) {
    const double dist = distance(body.pos, quad.center_of_mass);
    const double epsilon_squared = 
            softening_squared(body.vel, quad.momentum / quad.total_mass);
    const double force_amplitude = Constants::G * body.mass * quad.total_mass / 
        (dist * dist + epsilon_squared);

    return {
        force_amplitude * (quad.center_of_mass.x - body.pos.x) / dist,
        force_amplitude * (quad.center_of_mass.y - body.pos.y) / dist
    };
}
