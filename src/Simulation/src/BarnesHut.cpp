#include "Simulation/BarnesHut.hpp"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <limits>

#include "Logger/Logger.hpp"


namespace {
// Keys hold one 2-bit digit per quadtree level: bodies closer than 2^-KEY_LEVELS of the root cell
// share a leaf
constexpr uint32_t KEY_LEVELS = 24;
constexpr uint32_t KEY_BITS = 2 * KEY_LEVELS;
// Least significant digit radix sort
constexpr uint32_t RADIX_BITS = 8;
constexpr uint32_t RADIX = 1u << RADIX_BITS;
static_assert(KEY_BITS % (2 * RADIX_BITS) == 0, "an even number of passes ends in the input");
// Nodes of at most this many bodies are leaves
constexpr uint32_t LEAF_SIZE = 8;
// Bodies that walk the tree together
constexpr uint32_t GROUP_SIZE = 64;
// At most one thread per this many bodies: in smaller systems the threads' synchronization costs
// more than it saves (300 bodies run 11x faster on 1 thread than on 32)
constexpr uint64_t MIN_BODIES_PER_THREAD = 400;
// The threads build subtrees of at most max(bodies / (this * threads), MIN_SUBTREE_SIZE) bodies
constexpr uint32_t SUBTREES_PER_THREAD = 8;
constexpr uint32_t MIN_SUBTREE_SIZE = 1024;
// Every node popped from the walk's stack pushes at most 4 children, a level deeper
constexpr uint32_t STACK_CAPACITY = 4 * (KEY_LEVELS + 2);

// Hilbert key of coordinates in [0, 1]^2, as in BarnesHutCuda.cu: each digit picks one of the
// quadrants of the cell given by the previous digits, along a curve without jumps
uint64_t hilbert_key(double x, double y) {
    constexpr double CELLS = static_cast<double>(1u << KEY_LEVELS);  // per axis
    uint32_t ix = static_cast<uint32_t>(std::clamp(x * CELLS, 0.0, CELLS - 1.0));
    uint32_t iy = static_cast<uint32_t>(std::clamp(y * CELLS, 0.0, CELLS - 1.0));
    uint64_t key = 0;
    for (int32_t bit = KEY_LEVELS - 1; bit >= 0; bit--) {
        const uint32_t rx = (ix >> bit) & 1;
        const uint32_t ry = (iy >> bit) & 1;
        key |= static_cast<uint64_t>((3 * rx) ^ ry) << (2 * bit);
        // Rotate the quadrant so the curve continues where it left off
        const uint32_t flip = (ry == 0 && rx == 1) ? 0xFFFFFFFF : 0;
        ix ^= flip;
        iy ^= flip;
        if (ry == 0) {
            std::swap(ix, iy);
        }
    }
    return key;
}

// Number of leading digits two keys share
uint32_t common_digits(uint64_t a, uint64_t b) {
    return a == b ? KEY_LEVELS : (std::countl_zero(a ^ b) - (64 - KEY_BITS)) / 2;
}

// Splits the sorted keys first..last - 1, which share `level` digits, by their next digit d into
// bounds[d]..bounds[d + 1] - 1
void split(const uint64_t* keys, uint32_t first, uint32_t last, uint32_t level,
        uint32_t (&bounds)[5]) {
    const uint32_t shift = KEY_BITS - 2 * (level + 1);
    const uint64_t prefix = keys[first] >> (shift + 2) << (shift + 2);
    bounds[0] = first;
    for (uint64_t digit = 1; digit < 4; digit++) {
        bounds[digit] = std::lower_bound(keys + bounds[digit - 1], keys + last,
                                prefix | digit << shift)
                        - keys;
    }
    bounds[4] = last;
}
}  // namespace


BarnesHut::BarnesHut(const Config::Simulation& sim_cfg, Bodies& bodies)
        : Simulation(sim_cfg, bodies),
          n_threads(std::max<uint64_t>(
                  std::min<uint64_t>(bodies.n / MIN_BODIES_PER_THREAD, sim_cfg.threads), 1)),
          sync_point(n_threads),
          thread_bounds(n_threads), order(bodies.n), order_alt(bodies.n), keys(bodies.n),
          keys_alt(bodies.n), histograms(n_threads * RADIX), sorted_pos(bodies.n),
          sorted_mass(bodies.n), interactions(n_threads) {
    if (sim_cfg.threads == 0)
        throw std::runtime_error("Thread count 0 is invalid");
    if (sim_cfg.threads > bodies.n)
        throw std::runtime_error("Threads must be less than the number of bodies");
    // The tree refers to bodies and to its nodes (at most two per body) by 32-bit signed indices
    if (bodies.n > static_cast<uint64_t>(std::numeric_limits<int32_t>::max() / 2))
        throw std::runtime_error("Too many bodies for Barnes-Hut");

    // A node of level l has a cell of side 2^-l root cells: it stands in for its bodies from
    // side / theta, as in BarnesHutCuda
    const double inv_theta_sq =
            sim_cfg.theta > 1e-150 ? 1.0 / (sim_cfg.theta * sim_cfg.theta) : DBL_MAX;
    for (int32_t level = 0; level <= static_cast<int32_t>(KEY_LEVELS); level++) {
        open_sq_by_level.push_back(std::ldexp(inv_theta_sq, -2 * level));
    }
    for (uint32_t i = 0; i < bodies.n; i++) {
        order[i] = i;
    }
    workers.reserve(n_threads - 1);
    Log::debug("Barnes-Hut on {} threads", n_threads);
}

BarnesHut::~BarnesHut() {
    if (get_state() == State::RUNNING) {
        pause();
    }

    StopWatch sw_total = sw_sort + sw_build + sw_walk;
    Log::debug("Sort:  [{}] ({})", sw_sort, sw_sort / sw_total);
    Log::debug("Build: [{}] ({})", sw_build, sw_build / sw_total);
    Log::debug("Walk:  [{}] ({})", sw_walk, sw_walk / sw_total);
}

// The simulation thread is thread 0, the workers wait for its parallel() calls
void BarnesHut::on_run() {
    worker_stop = false;
    try {
        for (uint32_t thread = 1; thread < n_threads; thread++) {
            workers.emplace_back(&BarnesHut::worker_task, this, thread);
        }
    }
    catch (...) {
        on_pause();
        throw;
    }
}

void BarnesHut::on_pause() {
    worker_stop = true;
    // The workers wait at the barrier: arrive for the master and for any worker that didn't start
    std::ignore = sync_point.arrive(n_threads - workers.size());
    for (auto& worker : workers) {
        worker.join();
    }
    workers.clear();
}

void BarnesHut::worker_task(uint32_t thread) {
    while (true) {
        sync_point.arrive_and_wait();
        if (worker_stop)
            return;
        run_task(thread);
        sync_point.arrive_and_wait();
    }
}

// Runs thread_task(thread) on every thread and rethrows the first exception once all are done. A
// task that waits at sync_point itself mustn't throw: the other threads would wait for it forever.
void BarnesHut::parallel(const std::function<void(uint32_t)>& thread_task) {
    task = &thread_task;
    sync_point.arrive_and_wait();
    run_task(0);
    sync_point.arrive_and_wait();
    if (task_error) {
        std::rethrow_exception(std::exchange(task_error, nullptr));
    }
}

void BarnesHut::run_task(uint32_t thread) {
    try {
        (*task)(thread);
    }
    catch (...) {
        std::lock_guard lock(task_error_mtx);
        if (!task_error) {
            task_error = std::current_exception();
        }
    }
}

void BarnesHut::step(double kick, double drift) {
    if (!bounds_known) {
        bounds = {DBL_MAX, -DBL_MAX, DBL_MAX, -DBL_MAX};
        for (uint64_t i = 0; i < bodies.n; i++) {
            const auto& p = bodies.pos(i);
            bounds = {std::min(bounds.min_x, p.x), std::max(bounds.max_x, p.x),
                    std::min(bounds.min_y, p.y), std::max(bounds.max_y, p.y)};
        }
        bounds_known = true;
    }
    // The root cell is the square around the bounding box, of any size if the box is a point
    root_size = std::max(bounds.max_x - bounds.min_x, bounds.max_y - bounds.min_y);
    if (!(root_size > 0.0)) {
        root_size = 1.0;
    }
    root_x = 0.5 * (bounds.min_x + bounds.max_x - root_size);
    root_y = 0.5 * (bounds.min_y + bounds.max_y - root_size);

    sw_sort.resume();
    parallel([this](uint32_t thread) { sort_bodies(thread); });
    sw_sort.pause();

    sw_build.resume();
    build_tree();
    sw_build.pause();

    sw_walk.resume();
    next_item = 0;
    parallel([this, kick, drift](uint32_t thread) { walk_groups(thread, kick, drift); });
    sw_walk.pause();

    // The threads collected the bounding box of the drifted bodies
    bounds = {DBL_MAX, -DBL_MAX, DBL_MAX, -DBL_MAX};
    for (const Bounds& b : thread_bounds) {
        bounds = {std::min(bounds.min_x, b.min_x), std::max(bounds.max_x, b.max_x),
                std::min(bounds.min_y, b.min_y), std::max(bounds.max_y, b.max_y)};
    }
}

// Sorts the bodies by Hilbert key from the previous step's order, with a least significant digit
// radix sort over the threads' slices, then gathers their positions and masses
void BarnesHut::sort_bodies(uint32_t thread) {
    const uint32_t n = bodies.n;
    const uint32_t begin = static_cast<uint64_t>(n) * thread / n_threads;
    const uint32_t end = static_cast<uint64_t>(n) * (thread + 1) / n_threads;
    const double scale = 1.0 / root_size;
    for (uint32_t i = begin; i < end; i++) {
        const auto& p = bodies.pos(order[i]);
        keys[i] = hilbert_key((p.x - root_x) * scale, (p.y - root_y) * scale);
    }

    uint64_t* src_keys = keys.data();
    uint64_t* dst_keys = keys_alt.data();
    uint32_t* src_order = order.data();
    uint32_t* dst_order = order_alt.data();
    uint32_t* const histogram = &histograms[thread * RADIX];
    for (uint32_t shift = 0; shift < KEY_BITS; shift += RADIX_BITS) {
        std::fill_n(histogram, RADIX, 0);
        for (uint32_t i = begin; i < end; i++) {
            histogram[(src_keys[i] >> shift) & (RADIX - 1)]++;
        }
        sync_point.arrive_and_wait();

        // A body goes after those with smaller digits, then those with its digit in earlier slices
        uint32_t offsets[RADIX];
        uint32_t total = 0;
        for (uint32_t digit = 0; digit < RADIX; digit++) {
            offsets[digit] = total;
            for (uint32_t t = 0; t < n_threads; t++) {
                const uint32_t count = histograms[t * RADIX + digit];
                offsets[digit] += t < thread ? count : 0;
                total += count;
            }
        }
        for (uint32_t i = begin; i < end; i++) {
            const uint32_t at = offsets[(src_keys[i] >> shift) & (RADIX - 1)]++;
            dst_keys[at] = src_keys[i];
            dst_order[at] = src_order[i];
        }
        sync_point.arrive_and_wait();
        std::swap(src_keys, dst_keys);
        std::swap(src_order, dst_order);
    }

    for (uint32_t i = begin; i < end; i++) {
        sorted_pos[i] = bodies.pos(order[i]);
        sorted_mass[i] = bodies.mass(order[i]);
    }
}

void BarnesHut::build_tree() {
    const uint32_t n = bodies.n;
    top_nodes.clear();
    subtrees.clear();
    build_top(0, n, std::max(n / (SUBTREES_PER_THREAD * n_threads), MIN_SUBTREE_SIZE));

    // A subtree of k bodies has at most 2k - 1 nodes: its leaves hold bodies, its other nodes at
    // least two children
    int32_t at = static_cast<int32_t>(top_nodes.size());
    for (Subtree& subtree : subtrees) {
        subtree.node = at;
        at += 2 * static_cast<int32_t>(subtree.last - subtree.first);
    }
    nodes.resize(at);
    for (uint32_t i = 0; i < top_nodes.size(); i++) {
        nodes[i] = top_nodes[i];
        for (int32_t& child : nodes[i].child) {
            if (child < -1) {
                child = subtrees[-2 - child].node;
            }
        }
    }

    next_item = 0;
    parallel([this](uint32_t) {
        for (uint32_t s = next_item++; s < subtrees.size(); s = next_item++) {
            build_subtree(subtrees[s].first, subtrees[s].last, subtrees[s].node);
        }
    });

    // The top nodes' children come after them
    for (int32_t i = static_cast<int32_t>(top_nodes.size()) - 1; i >= 0; i--) {
        sum_children(nodes[i]);
    }
}

// Builds the top of the tree, leaving subtrees of at most `subtree_size` bodies to the threads.
// Returns the node of the bodies first..last - 1: an index in top_nodes, or -2 - k for subtree k.
int32_t BarnesHut::build_top(uint32_t first, uint32_t last, uint32_t subtree_size) {
    const uint32_t level = common_digits(keys[first], keys[last - 1]);
    if (last - first <= subtree_size || level == KEY_LEVELS) {
        subtrees.push_back({first, last, -1});
        return -1 - static_cast<int32_t>(subtrees.size());
    }
    const int32_t index = static_cast<int32_t>(top_nodes.size());
    top_nodes.push_back({.com_x = 0.0, .com_y = 0.0, .mass = 0.0,
            .open_sq = open_sq_by_level[level], .first = first, .last = last,
            .child = {-1, -1, -1, -1}});
    uint32_t bounds[5];
    split(keys.data(), first, last, level, bounds);
    int32_t k = 0;
    for (uint32_t digit = 0; digit < 4; digit++) {
        if (bounds[digit + 1] > bounds[digit]) {
            const int32_t child = build_top(bounds[digit], bounds[digit + 1], subtree_size);
            top_nodes[index].child[k++] = child;  // build_top() may move top_nodes
        }
    }
    return index;
}

// Builds the node of the bodies first..last - 1 at nodes[at] and its subtree after it. Returns the
// index after the subtree.
int32_t BarnesHut::build_subtree(uint32_t first, uint32_t last, int32_t at) {
    const uint32_t level = common_digits(keys[first], keys[last - 1]);
    Node& node = nodes[at];
    node.open_sq = open_sq_by_level[level];
    node.first = first;
    node.last = last;
    std::fill_n(node.child, 4, -1);
    int32_t next = at + 1;
    if (last - first <= LEAF_SIZE || level == KEY_LEVELS) {
        double moment_x = 0.0, moment_y = 0.0;
        node.mass = 0.0;
        for (uint32_t i = first; i < last; i++) {
            node.mass += sorted_mass[i];
            moment_x += sorted_mass[i] * sorted_pos[i].x;
            moment_y += sorted_mass[i] * sorted_pos[i].y;
        }
        // A massless node keeps its first body as center of mass, any point of it does
        node.com_x = node.mass > 0.0 ? moment_x / node.mass : sorted_pos[first].x;
        node.com_y = node.mass > 0.0 ? moment_y / node.mass : sorted_pos[first].y;
        return next;
    }
    uint32_t bounds[5];
    split(keys.data(), first, last, level, bounds);
    int32_t k = 0;
    for (uint32_t digit = 0; digit < 4; digit++) {
        if (bounds[digit + 1] > bounds[digit]) {
            node.child[k++] = next;
            next = build_subtree(bounds[digit], bounds[digit + 1], next);
        }
    }
    sum_children(node);
    return next;
}

void BarnesHut::sum_children(Node& node) const {
    double moment_x = 0.0, moment_y = 0.0;
    node.mass = 0.0;
    for (const int32_t child : node.child) {
        if (child >= 0) {
            node.mass += nodes[child].mass;
            moment_x += nodes[child].mass * nodes[child].com_x;
            moment_y += nodes[child].mass * nodes[child].com_y;
        }
    }
    node.com_x = node.mass > 0.0 ? moment_x / node.mass : sorted_pos[node.first].x;
    node.com_y = node.mass > 0.0 ? moment_y / node.mass : sorted_pos[node.first].y;
}

// Each group of GROUP_SIZE consecutive bodies walks the tree once. A node stands in for its bodies
// if it holds none of the group's and is far enough from all of them, i.e. from the group's
// bounding box, else it is opened, and leaves add their bodies one by one (a body's pull on itself
// is zero). Then every body of the group sums the accepted nodes and bodies, and is kicked and
// drifted.
void BarnesHut::walk_groups(uint32_t thread, double kick, double drift) {
    const uint32_t n = bodies.n;
    const uint32_t groups = (n + GROUP_SIZE - 1) / GROUP_SIZE;
    const double inv_root_sq = 1.0 / (root_size * root_size);
    const double acc_kick = Constants::Simulation::G * kick;
    Interactions& list = interactions[thread];
    Bounds drifted{DBL_MAX, -DBL_MAX, DBL_MAX, -DBL_MAX};
    int32_t stack[STACK_CAPACITY];
    for (uint32_t group = next_item++; group < groups; group = next_item++) {
        const uint32_t first = group * GROUP_SIZE;
        const uint32_t last = std::min(first + GROUP_SIZE, n);
        double min_x = DBL_MAX, max_x = -DBL_MAX, min_y = DBL_MAX, max_y = -DBL_MAX;
        for (uint32_t i = first; i < last; i++) {
            min_x = std::min(min_x, sorted_pos[i].x);
            max_x = std::max(max_x, sorted_pos[i].x);
            min_y = std::min(min_y, sorted_pos[i].y);
            max_y = std::max(max_y, sorted_pos[i].y);
        }
        const double center_x = 0.5 * (min_x + max_x), half_x = 0.5 * (max_x - min_x);
        const double center_y = 0.5 * (min_y + max_y), half_y = 0.5 * (max_y - min_y);

        list.x.clear();
        list.y.clear();
        list.mass.clear();
        uint32_t stack_size = 0;
        stack[stack_size++] = 0;
        while (stack_size > 0) {
            const Node& node = nodes[stack[--stack_size]];
            if (node.last <= first || node.first >= last) {
                const double dx = std::max(std::abs(node.com_x - center_x) - half_x, 0.0);
                const double dy = std::max(std::abs(node.com_y - center_y) - half_y, 0.0);
                if ((dx * dx + dy * dy) * inv_root_sq > node.open_sq) {
                    list.x.push_back(node.com_x);
                    list.y.push_back(node.com_y);
                    list.mass.push_back(node.mass);
                    continue;
                }
            }
            if (node.child[0] < 0) {
                for (uint32_t i = node.first; i < node.last; i++) {
                    list.x.push_back(sorted_pos[i].x);
                    list.y.push_back(sorted_pos[i].y);
                    list.mass.push_back(sorted_mass[i]);
                }
                continue;
            }
            for (int32_t k = 3; k >= 0; k--) {
                if (node.child[k] >= 0) {
                    stack[stack_size++] = node.child[k];
                }
            }
        }

        const size_t count = list.mass.size();
        const double* const list_x = list.x.data();
        const double* const list_y = list.y.data();
        const double* const list_mass = list.mass.data();
        for (uint32_t i = first; i < last; i++) {
            const double x = sorted_pos[i].x, y = sorted_pos[i].y;
            double acc_x = 0.0, acc_y = 0.0;
            for (size_t j = 0; j < count; j++) {
                const double dx = list_x[j] - x;
                const double dy = list_y[j] - y;
                const double inv_dist = 1.0 / std::sqrt(dx * dx + dy * dy + epsilon_squared);
                const double f = list_mass[j] * inv_dist * inv_dist * inv_dist;
                acc_x += dx * f;
                acc_y += dy * f;
            }
            sf::Vector2<double>& vel = bodies.vel(order[i]);
            sf::Vector2<double>& pos = bodies.pos(order[i]);
            vel += sf::Vector2<double>(acc_x, acc_y) * acc_kick;
            pos += vel * drift;
            drifted = {std::min(drifted.min_x, pos.x), std::max(drifted.max_x, pos.x),
                    std::min(drifted.min_y, pos.y), std::max(drifted.max_y, pos.y)};
        }
    }
    thread_bounds[thread] = drifted;
}
