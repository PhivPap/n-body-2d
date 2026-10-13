#pragma once

#include <barrier>
#include <functional>
#include <mutex>

#include "Simulation/Simulation.hpp"


// Barnes-Hut on the CPU, along the lines of BarnesHutCuda: every step sorts the bodies along a
// Hilbert curve, builds the quadtree from their sorted keys, and lets groups of consecutive bodies
// walk it together. The simulation thread and `threads` - 1 workers share each part of a step.
class BarnesHut : public Simulation {
public:
    BarnesHut(const Config::Simulation& sim_cfg, Bodies& bodies);
    ~BarnesHut() override;

private:
    // Quadtree node of the sorted bodies first..last - 1
    struct Node {
        double com_x, com_y;  // center of mass
        double mass;
        double open_sq;  // squared distance (in root cells) from which it stands in for its bodies
        uint32_t first, last;
        int32_t child[4];  // -1: none, a leaf has none
    };
    // Bodies first..last - 1, whose subtree a thread builds from nodes[node]
    struct Subtree {
        uint32_t first, last;
        int32_t node;
    };
    struct alignas(64) Bounds {
        double min_x, max_x, min_y, max_y;
    };
    struct alignas(64) Interactions {  // per thread, so on separate cache lines
        std::vector<double> x, y, mass;
    };
    struct SortItem {
        uint64_t key;
        uint32_t body;
    };

    const uint32_t n_threads;
    std::vector<double> open_sq_by_level;  // in root cells, see Node::open_sq
    std::vector<std::thread> workers;
    std::barrier<> sync_point;
    std::atomic<bool> worker_stop;
    const std::function<void(uint32_t)>* task = nullptr;  // run by every thread in parallel()
    std::mutex task_error_mtx;
    std::exception_ptr task_error;
    std::atomic<uint32_t> next_item;  // work items handed out to the threads one by one

    // Root cell: the square around the bodies' bounding box, which the previous step collected
    Bounds bounds{};
    bool bounds_known = false;
    std::vector<Bounds> thread_bounds;
    double root_x = 0.0, root_y = 0.0, root_size = 1.0;

    // Bodies in Hilbert order: their index in `bodies`, key, position and mass
    std::vector<uint32_t> order;
    std::vector<uint64_t> keys;
    std::vector<sf::Vector2<double>> sorted_pos;
    std::vector<double> sorted_mass;
    // Sort: the bodies go to one bucket per thread, of the keys between two splitters
    std::vector<uint64_t> splitters;  // per thread, the same
    std::vector<uint32_t> body_buckets;  // in the previous order
    std::vector<uint32_t> bucket_sizes, bucket_offsets;  // per thread and bucket
    std::vector<SortItem> sort_items, sort_scratch;  // by bucket

    // Tree: the top nodes that the master builds, then the subtrees that the threads build
    std::vector<Node> nodes;
    std::vector<Node> top_nodes;
    std::vector<Subtree> subtrees;
    std::vector<Interactions> interactions;  // per thread

    StopWatch sw_sort{StopWatch::State::PAUSED};
    StopWatch sw_build{StopWatch::State::PAUSED};
    StopWatch sw_walk{StopWatch::State::PAUSED};

    void on_run() override;
    void on_pause() override;
    void step(double kick, double drift) override;
    void worker_task(uint32_t thread);
    void parallel(const std::function<void(uint32_t)>& thread_task);
    void run_task(uint32_t thread);
    void sort_bodies(uint32_t thread);
    void build_tree();
    int32_t build_top(uint32_t first, uint32_t last, uint32_t subtree_size);
    int32_t build_subtree(uint32_t first, uint32_t last, int32_t at);
    void sum_children(Node& node) const;
    void walk_groups(uint32_t thread, double kick, double drift);
};
