#pragma once

#include "Simulation/Simulation.hpp"

// CUDA types, defined by the CUDA headers which host only translation units including this header
// can't see
struct double2;
struct float4;
struct CUstream_st;
struct CUevent_st;


class BarnesHutCuda : public Simulation {
public:
    // Defined in BarnesHutCuda.cu
    struct Frame;
    struct Counters;
    struct TreeNode;
    struct Arrival;
    struct Seed;

    BarnesHutCuda(Config::Simulation& sim_cfg, Bodies& bodies);
    ~BarnesHutCuda() override;

private:
    const uint32_t n;
    const double inv_theta_sq;
    double total_mass = 0.0;
    bool host_bodies_pinned = false;
    uint32_t force_blocks = 0;
    CUstream_st* stream = nullptr;
    CUstream_st* copy_stream = nullptr;
    CUevent_st* copy_done = nullptr;
    CUevent_st* step_done[2] = {};
    uint64_t queued_steps = 0;

    // Bodies in Hilbert order, re-sorted every iteration into the `_alt` buffers
    double2* pos_d = nullptr;
    double2* vel_d = nullptr;
    float* mass_d = nullptr;  // in units of the total mass
    uint32_t* id_d = nullptr;  // index in `bodies`
    double2* pos_alt_d = nullptr;
    double2* vel_alt_d = nullptr;
    float* mass_alt_d = nullptr;
    uint32_t* id_alt_d = nullptr;

    // Hilbert sort
    uint64_t* keys_d = nullptr;
    uint64_t* keys_alt_d = nullptr;
    uint32_t* order_d = nullptr;
    uint32_t* order_alt_d = nullptr;
    void* sort_temp_d = nullptr;
    size_t sort_temp_bytes = 0;

    // Single precision coordinates of the sorted bodies in units of the root cell, two-floats
    // (hi.x, hi.y, lo.x, lo.y)
    float4* body_pos_d = nullptr;
    uint8_t* key_delta_d = nullptr;  // common prefix length of neighbouring keys

    // Tree: one node per binary radix tree split, quadtree nodes get a TreeNode
    Seed* seeds_d = nullptr;
    Arrival* arrivals_d = nullptr;  // two per radix tree node, see build_upper_nodes
    int32_t* merge_bound_d = nullptr;
    TreeNode* nodes_d = nullptr;
    int32_t* root_d = nullptr;
    float* open_sq_by_level_d = nullptr;

    Frame* frame_d = nullptr;
    Counters* counters_d = nullptr;
    uint32_t* retry_bodies_d = nullptr;  // bodies of abandoned groups
    int32_t* stacks_d = nullptr;         // traversal stacks that outgrow shared memory

    // Bodies in their original order, staged for the copy back to `bodies`
    double2* pos_out_d = nullptr;
    double2* vel_out_d = nullptr;

    void init_device_resources();
    void release_device_resources();
    void step() override;
    void copy_bodies_to_host_async();
    void sync_bodies_to_host() override;
};
