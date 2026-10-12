#include "Simulation/BarnesHut.hpp"

#include <algorithm>

#include "Logger/Logger.hpp"


BarnesHut::BarnesHut(const Config::Simulation& sim_cfg, Bodies& bodies)
        : Simulation(sim_cfg, bodies), n_threads(sim_cfg.threads),
          theta_sq(sim_cfg.theta * sim_cfg.theta), worker_chunk(bodies.n / n_threads),
          master_offset(worker_chunk * (n_threads - 1)), sync_point(n_threads) {
    if (sim_cfg.threads == 0)
        throw std::runtime_error("Thread count 0 is invalid");
    if (sim_cfg.threads > bodies.n)
        throw std::runtime_error("Threads must be less than the number of bodies");
    workers.reserve(sim_cfg.threads - 1);
}

BarnesHut::~BarnesHut() {
    if (get_state() == State::RUNNING) {
        pause();
    }

    StopWatch sw_total = sw_tree + sw_vel + sw_pos;
    Log::debug("Tree: [{}] ({})", sw_tree, sw_tree / sw_total);
    Log::debug("Vel:  [{}] ({})", sw_vel, sw_vel / sw_total);
    Log::debug("Pos:  [{}] ({})", sw_pos, sw_pos / sw_total);
}

// The simulation thread is the master, the workers wait for its steps
void BarnesHut::on_run() {
    worker_stop = false;
    for (uint32_t i = 0; i + 1 < n_threads; i++) {
        workers.emplace_back(&BarnesHut::worker_task, this, i);
    }
}

void BarnesHut::on_pause() {
    worker_stop = true;
    std::ignore = sync_point.arrive();
    for (auto& worker : workers) {
        worker.join();
    }
    workers.clear();
}

// An exception mustn't skip a barrier, the other threads would wait for this one forever. The
// master rethrows the first exception of a step after the last barrier.
void BarnesHut::step() {
    try {
        sw_tree.resume();
        qtree.build_tree(bodies);
        sw_tree.pause();
        tree_built = true;
    }
    catch (...) {
        tree_built = false;
        record_step_error();
    }

    sync_point.arrive_and_wait();

    if (tree_built) {
        try {
            sw_vel.resume();
            update_velocities(master_offset, bodies.n);
            sw_vel.pause();

            sw_pos.resume();
            update_positions(master_offset, bodies.n);
            sw_pos.pause();
        }
        catch (...) {
            record_step_error();
        }
    }

    sync_point.arrive_and_wait();
    if (step_error) {
        std::rethrow_exception(std::exchange(step_error, nullptr));
    }
}

void BarnesHut::worker_task(uint32_t worker_id) {
    const uint64_t begin_idx = worker_id * worker_chunk;
    const uint64_t end_idx = begin_idx + worker_chunk;
    while (true) {
        sync_point.arrive_and_wait();
        if (worker_stop)
            return;
        if (tree_built) {
            try {
                update_velocities(begin_idx, end_idx);
                update_positions(begin_idx, end_idx);
            }
            catch (...) {
                record_step_error();
            }
        }
        sync_point.arrive_and_wait();
    }
}

void BarnesHut::record_step_error() {
    std::lock_guard lock(step_error_mtx);
    if (!step_error) {
        step_error = std::current_exception();
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

    sf::Vector2<double> acc = {0.0, 0.0};
    // Includes the cell's max edges: bodies on the root's max edges lie on their cells' max edges
    const sf::Vector2<double> pos = bodies.pos(body_idx);
    const auto in_cell = [&pos](const sf::Rect<double>& cell) {
        return pos.x >= cell.position.x && pos.x <= cell.position.x + cell.size.x
               && pos.y >= cell.position.y && pos.y <= cell.position.y + cell.size.y;
    };

    while (!quad_idx_stack.empty()) {
        const Quad& quad = qtree.quads[quad_idx_stack.back()];
        quad_idx_stack.pop_back();
        if (quad.is_leaf()) {
            // The other bodies of a leaf of nearly coincident bodies (see Quadtree) pull one by one
            if (quad.body_count > 1
                    && std::ranges::find(quad.body_idxs, body_idx) != quad.body_idxs.end()) {
                for (const auto other_idx : quad.body_idxs) {
                    if (other_idx != body_idx) {
                        acc += pull(pos, bodies.pos(other_idx)) * bodies.mass(other_idx);
                    }
                }
            }
            else if (quad.total_mass != 0 && quad.center_of_mass != pos) {
                acc += body_to_quad_acceleration(body_idx, quad);
            }
        }
        else {
            const double dist_squared = (pos - quad.center_of_mass).lengthSquared();
            if (!in_cell(quad.boundaries) && dist_squared > 0.0
                    && quad.boundaries.size.lengthSquared() / dist_squared < theta_sq) {
                acc += body_to_quad_acceleration(body_idx, quad);
            }
            else {
                quad_idx_stack.push_back(quad.top_left_idx + 3);
                quad_idx_stack.push_back(quad.top_left_idx + 2);
                quad_idx_stack.push_back(quad.top_left_idx + 1);
                quad_idx_stack.push_back(quad.top_left_idx);
            }
        }
    }

    bodies.vel(body_idx) += acc * timestep;
}

sf::Vector2<double> BarnesHut::body_to_quad_acceleration(uint64_t body_idx, const Quad& quad) {
    const double softened_dist_sq =
            distance_squared(bodies.pos(body_idx), quad.center_of_mass) + epsilon_squared;
    const double amplitude = Constants::Simulation::G * quad.total_mass
                             / (softened_dist_sq * std::sqrt(softened_dist_sq));
    return (quad.center_of_mass - bodies.pos(body_idx)) * amplitude;
}
