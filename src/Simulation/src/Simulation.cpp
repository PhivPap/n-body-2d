#include "Simulation/Simulation.hpp"

#include "Logger/Logger.hpp"
#include "Constants/Constants.hpp"


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
        double Fx_sum = 0, Fy_sum = 0;
        for (uint64_t j = i + 1; j < total_bodies; j++) {
            Body &body_b = _bodies[j];
            const sf::Vector2<double> force = gravitational_force(body_a, body_b);
            body_b.vel -= force / body_b.mass * cfg.timestep;
            force_sum += force;
        }
        body_a.vel += force_sum / body_a.mass * cfg.timestep;
    }
}

double NaiveSim::euclidean_distance(const sf::Vector2<double> &pos_a, 
        const sf::Vector2<double> &pos_b) {
    const sf::Vector2<double> diff = pos_a - pos_b;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
}

sf::Vector2<double> NaiveSim::gravitational_force(const Body &body_a, const Body &body_b) {
    const double distance = euclidean_distance(body_a.pos, body_b.pos);
    const double force_amplitude = Constants::G * body_a.mass * body_b.mass / 
            (distance * distance + Constants::e);
    return {
        force_amplitude * (body_b.pos.x - body_a.pos.x) / distance,
        force_amplitude * (body_b.pos.y - body_a.pos.y) / distance
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
    quadtree = std::make_unique<Quadtree>(bodies);
    sync_point.arrive_and_wait();
    update_velocities(master_offset, bodies.size());
    update_positions(master_offset, bodies.size());
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
    const auto _bodies = bodies.data();
    for (uint64_t idx = begin_idx; idx < end_idx; idx++) {
        update_velocity(_bodies[idx], quadtree.get());
    }
}

void BarnesHutSim::update_velocity(Body &body, const Quadtree *node) {
    if (node->bodies.empty() || (node->bodies.size() == 1 && node->center_of_mass == body.pos)) {
        return;
    }

    const double distance = NaiveSim::euclidean_distance(body.pos, node->center_of_mass);
    if (node->bodies.size() == 1 || node->boundaries.size.x / distance < Constants::THETA) {
        const sf::Vector2<double> F = body_to_quad_force(body, node);
        body.vel += F / body.mass * cfg.timestep;
    }
    else {
        update_velocity(body, node->top_left);
        update_velocity(body, node->top_right);
        update_velocity(body, node->bottom_left);
        update_velocity(body, node->bottom_right);
    }
}

sf::Vector2<double> BarnesHutSim::body_to_quad_force(const Body &body, const Quadtree *node) {
    const double distance = NaiveSim::euclidean_distance(body.pos, node->center_of_mass);
    const double force_amplitude = Constants::G * body.mass * node->total_mass / 
        (distance * distance + Constants::e);

    return {
        force_amplitude * (node->center_of_mass.x - body.pos.x) / distance,
        force_amplitude * (node->center_of_mass.y - body.pos.y) / distance
    };
}
