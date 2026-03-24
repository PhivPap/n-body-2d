#pragma once

#include "Simulation/Simulation.hpp"

struct Vector2 {
    double x, y;
};

struct Rect {
    Vector2 position;
    Vector2 size;
};

// Compact SoA tree node layout for force computation.
// Stored as separate arrays indexed by node id.
struct TreeNodesSoA {
    double   *mass       = nullptr;   // total mass of subtree
    double   *com_x      = nullptr;   // center-of-mass x
    double   *com_y      = nullptr;   // center-of-mass y
    double   *width_sq   = nullptr;   // squared width for opening-angle test
    int32_t  *child0     = nullptr;   // first child index (-1 = no child)
    int32_t  *child1     = nullptr;
    int32_t  *child2     = nullptr;
    int32_t  *child3     = nullptr;
    int32_t  *body_start = nullptr;   // leaf: first body index, internal: -1
    int32_t  *body_end   = nullptr;   // leaf: last body index, internal: -1
};

// Packed AoS node for force traversal — 32 bytes.
// Packed AoS node for force traversal — 48 bytes.
// Children are contiguous: c0, c0+1, c0+2, c0+3.
struct ForceNode {
    double  com_x;      // 8
    double  com_y;      // 8
    double  mass;       // 8
    double  width_sq;   // 8
    int32_t c0;         // 4  (-1 = leaf; else first child idx, others are c0+1..c0+3)
    int32_t body_start; // 4  (-1 = empty node)
    int32_t body_end;   // 4
    uint8_t child_mask; // 1  bits 0-3: which children are non-empty
    uint8_t _pad[3];    // 3  pad to 48
};

class BarnesHutCuda : public Simulation {
public:
    struct Box { double x_min, x_max, y_min, y_max; };
    
    BarnesHutCuda(const Config::Simulation &sim_cfg, Bodies &bodies);
    virtual ~BarnesHutCuda();
private:
    std::thread sim_thread;
    Box bounding_box;
    const int32_t max_quads;

    // Body arrays (sorted by Morton code each iteration)
    double  *mass_d     = nullptr;
    Vector2 *pos_d      = nullptr;
    Vector2 *vel_d      = nullptr;

    // Temporary buffers for sorting/reordering
    double  *mass_alt_d = nullptr;
    Vector2 *pos_alt_d  = nullptr;
    Vector2 *vel_alt_d  = nullptr;

    // Original-order index for scatter-back
    int32_t *idx_d      = nullptr;
    int32_t *idx_alt_d  = nullptr;

    // Morton code sorting
    uint64_t *morton_d     = nullptr;
    uint64_t *morton_alt_d = nullptr;
    int32_t  *sort_idx_d   = nullptr;
    int32_t  *sort_idx_alt_d = nullptr;
    void     *cub_temp_d   = nullptr;
    size_t    cub_temp_bytes = 0;

    // Bounding box
    Box *bounding_box_d = nullptr;

    // Tree (SoA layout)
    TreeNodesSoA tree;
    int32_t *node_count_d  = nullptr;  // atomic counter for node allocation

    // Packed AoS for force traversal
    ForceNode *force_nodes_d = nullptr;

    // Work lists for tree build
    int32_t *work_list_d      = nullptr;
    int32_t *work_list_next_d = nullptr;
    int32_t *work_count_d     = nullptr;

    // Bottom-up COM computation
    int32_t *node_child_count_d = nullptr;  // atomic flag: how many children reported
    int32_t *node_parent_d      = nullptr;  // parent index for bottom-up walk

    void on_run() override;
    void on_pause() override;
    void simulate();

    void init_device_resources();
    void release_device_resources();
    void sync_from_gpu_bodies();
    void compute_bounding_box();
    void sort_bodies_by_morton();
    void build_quad_tree();
    void compute_forces();
    void update_positions();
};
