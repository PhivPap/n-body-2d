#include "Simulation/BarnesHut.hpp"

#include "Logger/Logger.hpp"

BarnesHut::BarnesHut(const Config::Simulation &sim_cfg, Bodies &bodies) : 
        Simulation(sim_cfg, bodies), n_threads(sim_cfg.threads), 
        worker_chunk(bodies.n / n_threads), master_offset(worker_chunk * (n_threads - 1)), 
        sync_point(n_threads) {
    if (sim_cfg.threads == 0)
        throw std::runtime_error("Thread count 0 is invalid");
    if (sim_cfg.threads > bodies.n)
        throw std::runtime_error("Threads must be less than the number of bodies");
    workers.reserve(sim_cfg.threads - 1);
}

BarnesHut::~BarnesHut() {
    if (master.joinable()) {
        on_pause();
    }

    StopWatch sw_total = sw_tree + sw_vel + sw_pos;
    Log::debug("Tree: [{}] ({})", sw_tree, sw_tree / sw_total);
    Log::debug("Vel:  [{}] ({})", sw_vel, sw_vel / sw_total);
    Log::debug("Pos:  [{}] ({})", sw_pos, sw_pos / sw_total);
}

void BarnesHut::on_run() {
    stop = false;
    worker_stop = false;
    master = std::thread(&BarnesHut::simulate, this);
    for (uint32_t i = 0; i < n_threads - 1; i++) {
        workers.emplace_back(&BarnesHut::worker_task, this, i);
    }
}

void BarnesHut::on_pause() {
    stop = true;
    master.join();
    worker_stop = true;
    std::ignore = sync_point.arrive();
    for (auto &worker : workers) {
        worker.join();
    }
    workers.clear();
}

void BarnesHut::simulate() {
    while (!should_stop()) {
        // computeBoundingBox(bodies.size(), bodies.data());

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

void BarnesHut::worker_task(uint32_t worker_id) {
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

void BarnesHut::update_positions(uint64_t begin_idx, uint64_t end_idx) {
    for (uint64_t idx = begin_idx; idx < end_idx; idx++) {
        bodies.pos(idx) += bodies.vel(idx) * timestep;
    }
}

void BarnesHut::update_velocities(uint64_t begin_idx, uint64_t end_idx) {
    for (uint64_t idx = begin_idx; idx < end_idx; idx++) {
        update_velocity(idx);
    }
}

// iterative DFS
void BarnesHut::update_velocity(uint64_t body_idx) {
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

sf::Vector2<double> BarnesHut::body_to_quad_force(uint64_t body_idx, const Quad &quad) {
    const double dist = distance(bodies.pos(body_idx), quad.center_of_mass);
    const double force_amplitude = Constants::Simulation::G * bodies.mass(body_idx) * 
            quad.total_mass / (dist * dist + epsilon_squared);
    return ((quad.center_of_mass - bodies.pos(body_idx)) / dist) * force_amplitude;
}
