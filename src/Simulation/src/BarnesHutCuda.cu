#include "Simulation/BarnesHutCuda.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cub/cub.cuh>
#include <cuda/atomic>
#include <cuda_runtime.h>
#include <stdexcept>
#include <vector>

#include "Logger/Logger.hpp"


#define CUDA_CHECK(call)                                                                           \
    do {                                                                                           \
        cudaError_t err = (call);                                                                  \
        if (err != cudaSuccess) {                                                                  \
            throw std::runtime_error(fmt::format("CUDA error at {}:{}: {}", __FILE__, __LINE__,    \
                    cudaGetErrorString(err)));                                                     \
        }                                                                                          \
    } while (0)

// An iteration runs these kernels back to back on `stream`, without waiting on the host:
//  1. start_iteration:   squares the bodies' bounding box (collected by the previous iteration)
//                        into the root cell of the quadtree
//  2. compute_keys:      Hilbert key of every body within the root cell
//  3. CUB radix sort of the keys, then reorder_bodies gathers the bodies into Hilbert order
//  4. build_lower_nodes, build_upper_nodes: the compressed quadtree of the sorted bodies, from
//                        their binary radix tree
//  5. compute_forces:    every warp walks the tree for a group of 32 consecutive bodies, then
//                        integrates them (symplectic Euler: kick, then drift). A second pass
//                        retries the bodies of the rare groups that it was faster to give up on.
//
// Precision: bodies stay in double precision while the tree and the forces use single precision,
// as consumer GPUs have a small fraction of their single precision throughput in double. Positions
// are in units of the root cell as two-floats (hi + lo, ~48 bits), so the separation of close
// bodies keeps full single precision wherever they are in the root cell. Nodes keep their mass
// moments relative to one of their bodies, and forces are summed with compensation.

namespace {
constexpr uint32_t BLOCK_SIZE = 256;
constexpr uint32_t WARP_SIZE = 32;
constexpr uint32_t FULL_MASK = 0xFFFFFFFF;

// Keys hold KEY_BITS / 2 bits per axis, so cells can be split up to KEY_BITS / 2 times (bodies
// in the same smallest cell still get separate leaves)
constexpr int32_t KEY_BITS = 56;
// Equal keys are told apart by up to 32 index bits, so radix tree nodes have < KEY_BITS + 32
// common bits, i.e. the quadtree has at most (KEY_BITS + 32) / 2 levels below the root
constexpr int32_t MAX_TREE_DEPTH = (KEY_BITS + 32) / 2 + 1;

constexpr int32_t EMPTY_CHILD = INT32_MIN;  // TreeNode::child slot without a child

// compute_forces
constexpr uint32_t FORCE_WARPS = 4;  // warps per block
// Interactions are buffered per warp and evaluated once there are at least this many
constexpr int32_t INTERACTION_BATCH = 64;
// A traversal step adds at most one interaction per lane
constexpr int32_t INTERACTION_CAPACITY = INTERACTION_BATCH + WARP_SIZE;
// The stack is sorted by depth, and every traversal step pushes <= 4 * 32 items of the same or the
// next depth after popping all deeper ones, so it never holds more than 4 * 32 items per depth.
// It rarely holds more than a couple hundred though, so it starts in shared memory and only moves
// to global memory if it might overflow.
constexpr int32_t STACK_CAPACITY = 4 * WARP_SIZE * MAX_TREE_DEPTH;
constexpr int32_t SHARED_STACK_CAPACITY = 512;
// A group with more than this many times the previous iteration's average interactions is given
// up on and its bodies are retried one by one. The rare groups spread around unrelated dense
// regions would take long enough to stall the whole pass otherwise.
constexpr uint32_t ABANDON_FACTOR = 2;
constexpr uint32_t MIN_ABANDON_INTERACTIONS = 512;
// Smallest squared softening length in units of the root cell. Keeps a body's interaction with
// itself (zero separation) finite without softening, and is far below any resolvable separation.
constexpr float MIN_SOFTENING_SQ = 1e-24f;
}  // namespace

// Root cell of the quadtree and the scales between SI and tree units, set by start_iteration
struct BarnesHutCuda::Frame {
    double origin_x, origin_y;  // lower left corner of the root cell
    double scale;               // 1 / root cell size: body tree coordinates are in [0, 1]
    double acc_factor;          // tree unit accelerations to m/s^2: G * total mass * scale^2
    float softening_sq;         // in tree units
};

// Counters of an iteration, set up by start_iteration
struct BarnesHutCuda::Counters {
    // Bounding box of the bodies (x min, x max, y min, y max) as order preserving integers (see
    // Device::ordered), which compute_forces collects for the next iteration
    unsigned long long bounds[4];
    uint32_t seed_count;   // see build_upper_nodes
    uint32_t next_group;   // next group a compute_forces warp takes
    uint32_t retry_count;  // bodies of abandoned groups
    uint32_t next_retry;
    uint32_t abandon_at;  // interactions of a group, see ABANDON_FACTOR
    unsigned long long interactions;  // of the groups compute_forces completed
    unsigned long long groups_done;
};

// Quadtree node, stored at the index of its binary radix tree node
struct BarnesHutCuda::TreeNode {
    float4 com;       // center of mass in two-float tree coordinates (hi.x, hi.y, lo.x, lo.y)
    float mass;       // in units of the total mass
    float open_sq;    // squared distance from which the node can stand in for its bodies
    int32_t first;    // bodies first..last (in Hilbert order) are in the node
    int32_t last;
    int4 child;       // >= 0: tree node, < 0: ~body index, EMPTY_CHILD: none (always the last)
};
static_assert(sizeof(BarnesHutCuda::TreeNode) == 48);

// A radix tree node (or body) for the thread that completes its parent, see build_upper_nodes
struct BarnesHutCuda::Arrival {
    int32_t node;      // >= 0: radix tree node, < 0: ~body index
    int32_t delta;     // common prefix length of the node's keys
    int32_t child[2];  // the node's radix tree children
    float mass;
    float d_x, d_y;  // first mass moment relative to the node's first body
    int32_t padding;
};
static_assert(sizeof(BarnesHutCuda::Arrival) == 32);

// A body or radix tree node whose parent isn't within its warp's bodies, see build_upper_nodes
struct BarnesHutCuda::Seed {
    Arrival arrival;
    int32_t first, last;  // its bodies
    int32_t padding[2];
};

using Frame = BarnesHutCuda::Frame;
using Counters = BarnesHutCuda::Counters;
using TreeNode = BarnesHutCuda::TreeNode;
using Arrival = BarnesHutCuda::Arrival;
using Seed = BarnesHutCuda::Seed;


// ---------------------------------------------------------------------------
// Device helpers
// ---------------------------------------------------------------------------
namespace Device {
template <typename T, typename Op>
__device__ __forceinline__ T warp_reduce(T value, Op op) {
    for (uint32_t offset = WARP_SIZE / 2; offset > 0; offset /= 2) {
        value = op(value, __shfl_xor_sync(FULL_MASK, value, offset));
    }
    return value;
}

// Bits of a double as an integer with the same order, for exact integer min/max reductions
__host__ __device__ __forceinline__ unsigned long long ordered(double x) {
    unsigned long long bits;
    memcpy(&bits, &x, sizeof(bits));
    return bits >> 63 ? ~bits : bits | (1ull << 63);
}

__host__ __device__ __forceinline__ double from_ordered(unsigned long long bits) {
    bits = bits >> 63 ? bits & ~(1ull << 63) : ~bits;
    double x;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

// Hilbert key of tree coordinates in [0, 1]^2. Like a Morton key, every 2 bits pick one of the 4
// quadrants of the cell given by the previous bits (so key prefixes are quadtree cells), but the
// curve never jumps between distant cells, which keeps runs of consecutive bodies compact.
__device__ __forceinline__ uint64_t hilbert_key(double x, double y) {
    constexpr double CELLS = static_cast<double>(1ull << (KEY_BITS / 2));  // per axis
    uint32_t ix = static_cast<uint32_t>(fmin(fmax(x * CELLS, 0.0), CELLS - 1.0));
    uint32_t iy = static_cast<uint32_t>(fmin(fmax(y * CELLS, 0.0), CELLS - 1.0));
    uint64_t key = 0;
    for (int32_t bit = KEY_BITS / 2 - 1; bit >= 0; bit--) {
        const uint32_t rx = (ix >> bit) & 1;
        const uint32_t ry = (iy >> bit) & 1;
        key |= static_cast<uint64_t>((3 * rx) ^ ry) << (2 * bit);
        // Rotate the quadrant so the curve continues where it left off
        const uint32_t flip = (ry == 0 && rx == 1) ? 0xFFFFFFFF : 0;
        ix ^= flip;
        iy ^= flip;
        if (ry == 0) {
            const uint32_t tmp = ix;
            ix = iy;
            iy = tmp;
        }
    }
    return key;
}

// Length of the common prefix of neighbouring sorted keys, equal keys are told apart by index
__device__ __forceinline__ uint8_t key_delta(uint64_t key, uint64_t next_key, uint32_t i) {
    return key != next_key ? __clzll(key ^ next_key) - (64 - KEY_BITS)
                           : KEY_BITS + __clz(i ^ (i + 1));
}

__device__ __forceinline__ float4 to_two_float(double x, double y) {
    const float hi_x = __double2float_rn(x);
    const float hi_y = __double2float_rn(y);
    return make_float4(hi_x, hi_y, __double2float_rn(x - hi_x), __double2float_rn(y - hi_y));
}

// Two-float coordinates p moved by (x, y), without losing precision
__device__ __forceinline__ float4 add_two_float(const float4& p, float x, float y) {
    const float hi_x = p.x + x;
    const float hi_y = p.y + y;
    const float x_part = hi_x - p.x;
    const float y_part = hi_y - p.y;
    const float err_x = (p.x - (hi_x - x_part)) + (x - x_part);
    const float err_y = (p.y - (hi_y - y_part)) + (y - y_part);
    return make_float4(hi_x, hi_y, p.z + err_x, p.w + err_y);
}

// Quadtree node of the radix tree node with bodies l..r, from its mass and first mass moment
// relative to its first body
__device__ __forceinline__ TreeNode quad_node(const float* __restrict__ open_sq_by_level,
        int32_t l, int32_t r, int32_t level, float m, float d_x, float d_y,
        const float4& first_pos, const int32_t (&child)[4]) {
    TreeNode t;
    // A massless node keeps its first body as center of mass, any point of it does
    t.com = m > 0.0f ? add_two_float(first_pos, d_x / m, d_y / m) : first_pos;
    t.mass = m;
    t.open_sq = open_sq_by_level[level];
    t.first = l;
    t.last = r;
    t.child = make_int4(child[0], child[1], child[2], child[3]);
    return t;
}

// Compensated (Kahan) float sum, as accurate as a double for the few terms it gets
struct KahanSum {
    float sum = 0.0f;
    float compensation = 0.0f;

    __device__ __forceinline__ void add(float value) {
        const float y = value - compensation;
        const float t = sum + y;
        compensation = (t - sum) - y;
        sum = t;
    }
    __device__ __forceinline__ double value() const {
        return static_cast<double>(sum) - compensation;
    }
};

// Acceleration of a body by the buffered interactions, in tree units
__device__ __forceinline__ void interact(const float4* list_pos, const float* list_mass,
        int32_t count, const float4& body, float softening_sq, KahanSum& acc_x, KahanSum& acc_y) {
    float sum_x = 0.0f, sum_y = 0.0f;
#pragma unroll 4
    for (int32_t i = 0; i < count; i++) {
        const float4 p = list_pos[i];
        const float dx = (p.x - body.x) + (p.z - body.z);
        const float dy = (p.y - body.y) + (p.w - body.w);
        const float dist_sq = fmaf(dx, dx, fmaf(dy, dy, softening_sq));
        const float inv_dist = rsqrtf(dist_sq);
        const float f = list_mass[i] * inv_dist * inv_dist * inv_dist;
        sum_x = fmaf(dx, f, sum_x);
        sum_y = fmaf(dy, f, sum_y);
    }
    acc_x.add(sum_x);
    acc_y.add(sum_y);
}
}  // namespace Device


// ---------------------------------------------------------------------------
// Kernels
// ---------------------------------------------------------------------------
namespace Kernel {
// Squares the bounding box of the bodies into the root cell and sets up the iteration's counters
__global__ void start_iteration(double softening_sq, double g_total_mass, Counters* counters,
        Frame* frame) {
    const double x_min = Device::from_ordered(counters->bounds[0]);
    const double x_max = Device::from_ordered(counters->bounds[1]);
    const double y_min = Device::from_ordered(counters->bounds[2]);
    const double y_max = Device::from_ordered(counters->bounds[3]);
    // A degenerate box gets an arbitrary size
    double size = fmax(x_max - x_min, y_max - y_min);
    if (!(size > 0.0)) {
        size = 1.0;
    }
    const double scale = 1.0 / size;
    frame->origin_x = 0.5 * (x_min + x_max - size);
    frame->origin_y = 0.5 * (y_min + y_max - size);
    frame->scale = scale;
    frame->acc_factor = g_total_mass * scale * scale;
    frame->softening_sq = fmaxf(static_cast<float>(softening_sq * scale * scale), MIN_SOFTENING_SQ);

    counters->bounds[0] = counters->bounds[2] = UINT64_MAX;
    counters->bounds[1] = counters->bounds[3] = 0;
    counters->seed_count = 0;
    counters->next_group = 0;
    counters->retry_count = 0;
    counters->next_retry = 0;
    // The previous iteration's groups set the bar for this one's
    constexpr unsigned long long NEVER = UINT32_MAX;
    unsigned long long abandon_at = NEVER;
    if (counters->groups_done > 0) {
        abandon_at = max(ABANDON_FACTOR * counters->interactions / counters->groups_done,
                static_cast<unsigned long long>(MIN_ABANDON_INTERACTIONS));
    }
    counters->abandon_at = static_cast<uint32_t>(min(abandon_at, NEVER));
    counters->interactions = 0;
    counters->groups_done = 0;
}

__global__ void compute_keys(const double2* __restrict__ pos, uint32_t n,
        const Frame* __restrict__ frame, uint64_t* keys, uint32_t* order) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) {
        return;
    }
    const double2 p = pos[i];
    const double scale = frame->scale;
    keys[i] = Device::hilbert_key((p.x - frame->origin_x) * scale, (p.y - frame->origin_y) * scale);
    order[i] = i;
}

// Gathers the bodies into Hilbert order and prepares the tree build
__global__ void reorder_bodies(const uint32_t* __restrict__ order,
        const uint64_t* __restrict__ keys, const Frame* __restrict__ frame, uint32_t n,
        const double2* __restrict__ pos, const double2* __restrict__ vel,
        const float* __restrict__ mass, const uint32_t* __restrict__ id, double2* pos_out,
        double2* vel_out, float* mass_out, uint32_t* id_out, float4* body_pos,
        uint8_t* key_delta, int32_t* merge_bound) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) {
        return;
    }
    const uint32_t src = order[i];
    const double2 p = pos[src];
    pos_out[i] = p;
    vel_out[i] = vel[src];
    mass_out[i] = mass[src];
    id_out[i] = id[src];

    const double scale = frame->scale;
    body_pos[i] =
            Device::to_two_float((p.x - frame->origin_x) * scale, (p.y - frame->origin_y) * scale);
    if (i + 1 < n) {
        key_delta[i] = Device::key_delta(keys[i], keys[i + 1], i);
        merge_bound[i] = -1;
    }
}

// The quadtree comes from the binary radix tree of the sorted keys. Radix node p splits its bodies
// l..r between p and p + 1, so its common prefix length is key_delta[p], and l - 1 and r are the
// nearest bodies with smaller common prefix lengths with their next. A radix node is a quadtree
// node if its enclosing quadtree cell (level key_delta / 2) is smaller than its parent's, all
// others are half cells whose children their parent adopts. This gives the same compressed
// quadtree as a top-down build (without empty cells and single child chains).
// Nodes get their mass and first mass moment relative to their first body,
// d = sum(m * (u - u_first)), which stays as accurate as the node is small in single precision.
//
// Most nodes are within the 32 bodies of a warp, which builds them with shuffles here. The bodies
// and nodes whose parent isn't are the seeds of build_upper_nodes.
__global__ void build_lower_nodes(uint32_t n, const uint8_t* __restrict__ key_delta,
        const float4* __restrict__ body_pos, const float* __restrict__ body_mass,
        const float* __restrict__ open_sq_by_level, Counters* counters, Seed* seeds,
        TreeNode* nodes, int32_t* root) {
    __shared__ TreeNode block_nodes[BLOCK_SIZE];
    const int32_t lane = threadIdx.x % WARP_SIZE;
    const uint32_t lanes_before = (1u << lane) - 1;
    const int32_t first_body = blockIdx.x * blockDim.x + threadIdx.x - lane;
    if (first_body >= static_cast<int32_t>(n)) {
        return;
    }
    const int32_t last_body = n - 1;
    const auto delta_of = [&](int32_t i) -> int32_t {
        return i >= 0 && i < last_body ? key_delta[i] : -1;
    };
    const auto shfl_pos = [](const float4& p, int32_t src) {
        return make_float4(__shfl_sync(FULL_MASK, p.x, src), __shfl_sync(FULL_MASK, p.y, src),
                __shfl_sync(FULL_MASK, p.z, src), __shfl_sync(FULL_MASK, p.w, src));
    };
    // Every lane takes part in every shuffle below, branches only select from their results

    // Lane k has body first_body + k and the radix node that splits after it
    const int32_t body = first_body + lane;
    const int32_t delta = delta_of(body);
    const int32_t delta_before = delta_of(first_body - 1);
    const float4 pos = body <= last_body ? body_pos[body] : make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    const float mass = body <= last_body ? body_mass[body] : 0.0f;

    // The node is local if it is within the warp's bodies: lanes l..r (l = -1, r = WARP_SIZE if
    // it reaches past them)
    int32_t l = delta_before < delta ? 0 : -1;
    int32_t r = WARP_SIZE;
    for (int32_t k = 0; k < WARP_SIZE; k++) {
        const int32_t delta_k = __shfl_sync(FULL_MASK, delta, k);
        if (delta_k < delta) {
            if (k < lane) {
                l = k + 1;
            }
            else if (k > lane && r == WARP_SIZE) {
                r = k;
            }
        }
    }
    const bool local = body < last_body && l >= 0 && r < WARP_SIZE;

    // Local nodes' moments and radix children: their halves' radix nodes split where the common
    // prefix is shortest
    int32_t left_split = 0, right_split = 0;
    int32_t left_delta = INT32_MAX, right_delta = INT32_MAX;
    const float4 first_pos = shfl_pos(pos, max(l, 0));
    float m = 0.0f, d_x = 0.0f, d_y = 0.0f;
    for (int32_t k = 0; k < WARP_SIZE; k++) {
        const int32_t delta_k = __shfl_sync(FULL_MASK, delta, k);
        const float mass_k = __shfl_sync(FULL_MASK, mass, k);
        const float4 pos_k = shfl_pos(pos, k);
        if (k >= l && k <= r) {
            m += mass_k;
            d_x += mass_k * ((pos_k.x - first_pos.x) + (pos_k.z - first_pos.z));
            d_y += mass_k * ((pos_k.y - first_pos.y) + (pos_k.w - first_pos.w));
            if (k < lane && delta_k < left_delta) {
                left_delta = delta_k;
                left_split = k;
            }
            else if (k > lane && k < r && delta_k < right_delta) {
                right_delta = delta_k;
                right_split = k;
            }
        }
    }
    const int32_t left_child = l == lane ? ~body : first_body + left_split;
    const int32_t right_child = r == lane + 1 ? ~(body + 1) : first_body + right_split;
    const int32_t left_grandchildren[2] = {__shfl_sync(FULL_MASK, left_child, left_split),
            __shfl_sync(FULL_MASK, right_child, left_split)};
    const int32_t right_grandchildren[2] = {__shfl_sync(FULL_MASK, left_child, right_split),
            __shfl_sync(FULL_MASK, right_child, right_split)};

    // The parent splits at the end with the longer common prefix
    const int32_t delta_end = __shfl_sync(FULL_MASK, delta, min(r, WARP_SIZE - 1));
    const int32_t delta_before_l = __shfl_sync(FULL_MASK, delta, max(l - 1, 0));
    const int32_t delta_start = l == 0 ? delta_before : delta_before_l;
    const int32_t parent_split = delta_end > delta_start ? r : l - 1;
    const bool parent_local = __shfl_sync(FULL_MASK, local, max(parent_split, 0))
                              && parent_split >= 0 && parent_split < WARP_SIZE;
    // Same for the lane's body, a leaf
    const int32_t delta_previous = __shfl_sync(FULL_MASK, delta, max(lane - 1, 0));
    const int32_t delta_left = lane == 0 ? delta_before : delta_previous;
    const int32_t leaf_parent_split = delta > delta_left ? lane : lane - 1;
    const bool leaf_parent_local = __shfl_sync(FULL_MASK, local, max(leaf_parent_split, 0))
                                   && leaf_parent_split >= 0;

    const bool is_root = first_body + l == 0 && first_body + r == last_body;
    const int32_t level = delta / 2;
    if (local && (is_root || level > max(delta_start, delta_end) / 2)) {
        int32_t child[4] = {EMPTY_CHILD, EMPTY_CHILD, EMPTY_CHILD, EMPTY_CHILD};
        int32_t k = 0;
        if (left_child < 0 || left_delta / 2 > level) {
            child[k++] = left_child;
        }
        else {
            child[k++] = left_grandchildren[0];
            child[k++] = left_grandchildren[1];
        }
        if (right_child < 0 || right_delta / 2 > level) {
            child[k++] = right_child;
        }
        else {
            child[k++] = right_grandchildren[0];
            child[k++] = right_grandchildren[1];
        }
        block_nodes[threadIdx.x] = Device::quad_node(open_sq_by_level, first_body + l,
                first_body + r, level, m, d_x, d_y, first_pos, child);
        if (is_root) {
            *root = body;
        }
    }
    // The warp's nodes are written together, which is much faster than each lane writing its own.
    // The slots of nodes that aren't local quadtree nodes get junk, build_upper_nodes overwrites
    // those it builds.
    __syncwarp();
    const auto warp_nodes = reinterpret_cast<const float4*>(&block_nodes[threadIdx.x - lane]);
    const auto nodes_out = reinterpret_cast<float4*>(&nodes[first_body]);
    constexpr int32_t NODE_FLOAT4S = sizeof(TreeNode) / sizeof(float4);
    // The last lane's node always reaches past the warp's bodies
    const int32_t node_count = min(static_cast<int32_t>(WARP_SIZE) - 1, last_body - first_body);
    for (int32_t k = lane; k < node_count * NODE_FLOAT4S; k += WARP_SIZE) {
        nodes_out[k] = warp_nodes[k];
    }

    const bool leaf_seed = body <= last_body && !leaf_parent_local;
    const bool node_seed = local && !is_root && !parent_local;
    const uint32_t leaf_seeds = __ballot_sync(FULL_MASK, leaf_seed);
    const uint32_t node_seeds = __ballot_sync(FULL_MASK, node_seed);
    uint32_t seed_at = 0;
    if (lane == 0) {
        seed_at = atomicAdd(&counters->seed_count, __popc(leaf_seeds) + __popc(node_seeds));
    }
    seed_at = __shfl_sync(FULL_MASK, seed_at, 0);
    if (leaf_seed) {
        seeds[seed_at + __popc(leaf_seeds & lanes_before)] =
                Seed{{~body, INT32_MAX, {EMPTY_CHILD, EMPTY_CHILD}, mass, 0.0f, 0.0f}, body, body};
    }
    if (node_seed) {
        seeds[seed_at + __popc(leaf_seeds) + __popc(node_seeds & lanes_before)] =
                Seed{{body, delta, {left_child, right_child}, m, d_x, d_y}, first_body + l,
                        first_body + r};
    }
}

// The rest of the tree, built bottom-up from the seeds: each climbs to its parent, and the second
// child to reach a parent completes it from what the first one left there (Apetrei, "Fast and
// Simple Agglomerative LBVH Construction", 2014)
__global__ void build_upper_nodes(uint32_t n, const uint8_t* __restrict__ key_delta,
        const float4* __restrict__ body_pos, const float* __restrict__ open_sq_by_level,
        const Counters* __restrict__ counters, const Seed* __restrict__ seeds, Arrival* arrivals,
        int32_t* merge_bound, TreeNode* nodes, int32_t* root) {
    const uint32_t s = blockIdx.x * blockDim.x + threadIdx.x;
    if (s >= counters->seed_count) {
        return;
    }
    const int32_t last_body = n - 1;
    const auto parent_is_right = [&](int32_t l, int32_t r) {
        return l == 0 || (r != last_body && key_delta[r] > key_delta[l - 1]);
    };

    Arrival own = seeds[s].arrival;
    int32_t l = seeds[s].first;
    int32_t r = seeds[s].last;
    while (true) {
        const bool is_left_child = parent_is_right(l, r);
        const int32_t parent = is_left_child ? r : l - 1;
        const int32_t parent_delta = key_delta[parent];
        Arrival* const slots = &arrivals[2 * parent];
        slots[is_left_child ? 0 : 1] = own;
        // Releases this node to the sibling's thread, or acquires the sibling from it
        const int32_t other_bound =
                cuda::atomic_ref<int32_t, cuda::thread_scope_device>(merge_bound[parent])
                        .exchange(is_left_child ? l : r, cuda::memory_order_acq_rel);
        if (other_bound < 0) {
            return;  // the sibling's thread completes the parent
        }
        (is_left_child ? r : l) = other_bound;  // the parent's bodies, split after `parent`
        const Arrival sibling = slots[is_left_child ? 1 : 0];
        const Arrival& left = is_left_child ? own : sibling;
        const Arrival& right = is_left_child ? sibling : own;

        // The right child's moment is relative to its own first body
        const float4 first_pos = body_pos[l];
        const float4 split_pos = body_pos[parent + 1];
        Arrival completed;
        completed.node = parent;
        completed.delta = parent_delta;
        completed.child[0] = left.node;
        completed.child[1] = right.node;
        completed.mass = left.mass + right.mass;
        completed.d_x = left.d_x + right.d_x
                        + right.mass * ((split_pos.x - first_pos.x) + (split_pos.z - first_pos.z));
        completed.d_y = left.d_y + right.d_y
                        + right.mass * ((split_pos.y - first_pos.y) + (split_pos.w - first_pos.w));

        const bool is_root = l == 0 && r == last_body;
        const int32_t level = parent_delta / 2;
        if (is_root || level > key_delta[parent_is_right(l, r) ? r : l - 1] / 2) {
            int32_t child[4] = {EMPTY_CHILD, EMPTY_CHILD, EMPTY_CHILD, EMPTY_CHILD};
            int32_t k = 0;
            for (const Arrival* c : {&left, &right}) {
                if (c->node < 0 || c->delta / 2 > level) {
                    child[k++] = c->node;
                }
                else {
                    child[k++] = c->child[0];
                    child[k++] = c->child[1];
                }
            }
            nodes[parent] = Device::quad_node(open_sq_by_level, l, r, level, completed.mass,
                    completed.d_x, completed.d_y, first_pos, child);
        }
        if (is_root) {
            *root = parent;
            return;
        }
        own = completed;
    }
}

// Each warp takes groups of 32 consecutive bodies (in Hilbert order) and walks the tree for all of
// them together. A node can stand in for its bodies if it is far enough from every body of the
// group, i.e. from the group's bounding box, and contains none of them, else it is opened. The
// lanes test 32 items of the traversal stack at once, the accepted nodes and bodies are buffered
// in shared memory, and the whole warp evaluates the buffer once it fills up. Finally every lane
// integrates its body, and stages it for the copy to the host if pos_out is given.
__global__ void __launch_bounds__(FORCE_WARPS * WARP_SIZE)
        compute_forces(const TreeNode* __restrict__ nodes, const float4* __restrict__ body_pos,
                const float* __restrict__ body_mass, const Frame* __restrict__ frame,
                const int32_t* __restrict__ root, const uint32_t* __restrict__ id, uint32_t n,
                double dt, bool retry_pass, Counters* counters, uint32_t* retry_bodies,
                int32_t* stacks, double2* pos, double2* vel, double2* pos_out, double2* vel_out) {
    __shared__ float4 interaction_pos[FORCE_WARPS][INTERACTION_CAPACITY];
    __shared__ float interaction_mass[FORCE_WARPS][INTERACTION_CAPACITY];
    __shared__ int32_t shared_stacks[FORCE_WARPS][SHARED_STACK_CAPACITY];
    const uint32_t lane = threadIdx.x % WARP_SIZE;
    const uint32_t warp = threadIdx.x / WARP_SIZE;
    const uint32_t lanes_before = (1u << lane) - 1;
    float4* const list_pos = interaction_pos[warp];
    float* const list_mass = interaction_mass[warp];
    int32_t* const global_stack = stacks + (blockIdx.x * FORCE_WARPS + warp) * STACK_CAPACITY;

    const float softening_sq = frame->softening_sq;
    const double acc_dt = frame->acc_factor * dt;
    const int32_t root_node = n > 1 ? *root : -1;
    // The retry pass takes the bodies of abandoned groups one by one, and never abandons them
    const uint32_t groups = retry_pass ? counters->retry_count : (n + WARP_SIZE - 1) / WARP_SIZE;
    uint32_t* const next_group = retry_pass ? &counters->next_retry : &counters->next_group;
    const uint32_t abandon_at = retry_pass ? UINT32_MAX : counters->abandon_at;
    unsigned long long warp_interactions = 0;
    uint32_t warp_groups = 0;

    while (true) {
        uint32_t group = 0;
        if (lane == 0) {
            group = atomicAdd(next_group, 1);
        }
        group = __shfl_sync(FULL_MASK, group, 0);
        if (group >= groups) {
            break;
        }
        const int32_t first = retry_pass ? retry_bodies[group] : group * WARP_SIZE;
        const int32_t last = retry_pass ? first : min(first + WARP_SIZE, n) - 1;
        // Lanes past the last body repeat it
        const int32_t body = min(first + static_cast<int32_t>(lane), last);
        const float4 b = body_pos[body];

        // Group bounding box, relative to the first body to keep it exact
        const float ref_x_hi = __shfl_sync(FULL_MASK, b.x, 0);
        const float ref_y_hi = __shfl_sync(FULL_MASK, b.y, 0);
        const float ref_x_lo = __shfl_sync(FULL_MASK, b.z, 0);
        const float ref_y_lo = __shfl_sync(FULL_MASK, b.w, 0);
        const float rel_x = (b.x - ref_x_hi) + (b.z - ref_x_lo);
        const float rel_y = (b.y - ref_y_hi) + (b.w - ref_y_lo);
        const auto min_op = [](float a, float b) { return fminf(a, b); };
        const auto max_op = [](float a, float b) { return fmaxf(a, b); };
        const float box_min_x = Device::warp_reduce(rel_x, min_op);
        const float box_max_x = Device::warp_reduce(rel_x, max_op);
        const float box_min_y = Device::warp_reduce(rel_y, min_op);
        const float box_max_y = Device::warp_reduce(rel_y, max_op);
        const float box_center_x = 0.5f * (box_min_x + box_max_x);
        const float box_center_y = 0.5f * (box_min_y + box_max_y);
        const float box_half_x = 0.5f * (box_max_x - box_min_x);
        const float box_half_y = 0.5f * (box_max_y - box_min_y);

        Device::KahanSum acc_x, acc_y;
        int32_t* stack = shared_stacks[warp];
        int32_t stack_capacity = SHARED_STACK_CAPACITY;
        int32_t stack_size = 0;
        int32_t list_size = 0;
        uint32_t interactions = 0;
        if (root_node >= 0) {
            if (lane == 0) {
                stack[0] = root_node;
            }
            stack_size = 1;
        }
        __syncwarp();

        while (stack_size > 0 && interactions <= abandon_at) {
            const int32_t popped = min(stack_size, static_cast<int32_t>(WARP_SIZE));
            // Every popped node pushes at most 4 children
            if (stack_size + 3 * popped > stack_capacity) {
                for (int32_t k = lane; k < stack_size; k += WARP_SIZE) {
                    global_stack[k] = stack[k];
                }
                __syncwarp();
                stack = global_stack;
                stack_capacity = STACK_CAPACITY;
            }
            stack_size -= popped;

            // Stack items are tree nodes (>= 0) and bodies (~index), the latter always accepted
            const int32_t item =
                    static_cast<int32_t>(lane) < popped ? stack[stack_size + lane] : EMPTY_CHILD;
            __syncwarp();  // the pushes below overwrite the popped items
            bool accept = item < 0 && item != EMPTY_CHILD;
            bool open = item >= 0;
            TreeNode t;
            t.first = 0;
            t.last = -1;
            t.child = make_int4(EMPTY_CHILD, EMPTY_CHILD, EMPTY_CHILD, EMPTY_CHILD);
            if (open) {
                t = nodes[item];
            }
            else if (accept) {
                t.com = body_pos[~item];
                t.mass = body_mass[~item];
            }
            const float dx = fmaxf(
                    fabsf((t.com.x - ref_x_hi) + (t.com.z - ref_x_lo) - box_center_x) - box_half_x,
                    0.0f);
            const float dy = fmaxf(
                    fabsf((t.com.y - ref_y_hi) + (t.com.w - ref_y_lo) - box_center_y) - box_half_y,
                    0.0f);
            const bool disjoint = t.last < first || t.first > last;
            if (open && disjoint && dx * dx + dy * dy > t.open_sq) {
                accept = true;
                open = false;
            }

            // Accepted nodes and bodies go to the interaction list, the children of opened nodes
            // to the stack
            const uint32_t accepted = __ballot_sync(FULL_MASK, accept);
            if (accept) {
                const int32_t list_at = list_size + __popc(accepted & lanes_before);
                list_pos[list_at] = t.com;
                list_mass[list_at] = t.mass;
            }
            list_size += __popc(accepted);
            interactions += __popc(accepted);

            const int4 c = t.child;
            const uint32_t push_count = open ? (c.x != EMPTY_CHILD) + (c.y != EMPTY_CHILD)
                                                       + (c.z != EMPTY_CHILD) + (c.w != EMPTY_CHILD)
                                             : 0;
            // push_count is 0, 2, 3 or 4, sum its bits across the warp
            const uint32_t pushes_1 = __ballot_sync(FULL_MASK, push_count & 1);
            const uint32_t pushes_2 = __ballot_sync(FULL_MASK, push_count & 2);
            const uint32_t pushes_4 = __ballot_sync(FULL_MASK, push_count & 4);
            if (open) {
                int32_t* const push_at = stack + stack_size + __popc(pushes_1 & lanes_before)
                                         + 2 * __popc(pushes_2 & lanes_before)
                                         + 4 * __popc(pushes_4 & lanes_before);
                push_at[0] = c.x;
                push_at[1] = c.y;
                if (push_count > 2) {
                    push_at[2] = c.z;
                }
                if (push_count > 3) {
                    push_at[3] = c.w;
                }
            }
            stack_size += __popc(pushes_1) + 2 * __popc(pushes_2) + 4 * __popc(pushes_4);
            __syncwarp();

            if (list_size >= INTERACTION_BATCH) {
                Device::interact(list_pos, list_mass, list_size, b, softening_sq, acc_x, acc_y);
                list_size = 0;
                __syncwarp();
            }
        }

        if (interactions > abandon_at) {
            uint32_t retry_at = 0;
            if (lane == 0) {
                retry_at = atomicAdd(&counters->retry_count, last - first + 1);
            }
            retry_at = __shfl_sync(FULL_MASK, retry_at, 0);
            if (first + static_cast<int32_t>(lane) <= last) {
                retry_bodies[retry_at + lane] = first + lane;
            }
            continue;
        }
        warp_interactions += interactions;
        warp_groups++;
        Device::interact(list_pos, list_mass, list_size, b, softening_sq, acc_x, acc_y);
        __syncwarp();

        using Bits = unsigned long long;
        Bits x_min = UINT64_MAX, x_max = 0, y_min = UINT64_MAX, y_max = 0;
        if (first + static_cast<int32_t>(lane) <= last) {
            double2 v = vel[body];
            double2 x = pos[body];
            v.x += acc_x.value() * acc_dt;
            v.y += acc_y.value() * acc_dt;
            x.x += v.x * dt;
            x.y += v.y * dt;
            vel[body] = v;
            pos[body] = x;
            if (pos_out) {
                pos_out[id[body]] = x;
                vel_out[id[body]] = v;
            }
            x_min = x_max = Device::ordered(x.x);
            y_min = y_max = Device::ordered(x.y);
        }
        // Bounding box for the next iteration
        const auto bits_min = [](Bits a, Bits b) { return min(a, b); };
        const auto bits_max = [](Bits a, Bits b) { return max(a, b); };
        x_min = Device::warp_reduce(x_min, bits_min);
        x_max = Device::warp_reduce(x_max, bits_max);
        y_min = Device::warp_reduce(y_min, bits_min);
        y_max = Device::warp_reduce(y_max, bits_max);
        if (lane == 0) {
            atomicMin(&counters->bounds[0], x_min);
            atomicMax(&counters->bounds[1], x_max);
            atomicMin(&counters->bounds[2], y_min);
            atomicMax(&counters->bounds[3], y_max);
        }
    }
    // Retried bodies are cheaper than groups, they would lower the bar
    if (lane == 0 && !retry_pass) {
        atomicAdd(&counters->interactions, warp_interactions);
        atomicAdd(&counters->groups_done, static_cast<unsigned long long>(warp_groups));
    }
}

// Scatters the sorted bodies back to their original order for the copy to the host
__global__ void unsort_bodies(const double2* __restrict__ pos, const double2* __restrict__ vel,
        const uint32_t* __restrict__ id, uint32_t n, double2* pos_out, double2* vel_out) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        const uint32_t dst = id[i];
        pos_out[dst] = pos[i];
        vel_out[dst] = vel[i];
    }
}
}  // namespace Kernel


// ===========================================================================
// Host side
// ===========================================================================

void BarnesHutCuda::init_device_resources() {
    const auto alloc = [](auto*& ptr, size_t count) {
        CUDA_CHECK(cudaMalloc(&ptr, sizeof(*ptr) * std::max<size_t>(count, 1)));
    };

    int device;
    CUDA_CHECK(cudaGetDevice(&device));
    int sm_count;
    CUDA_CHECK(cudaDeviceGetAttribute(&sm_count, cudaDevAttrMultiProcessorCount, device));
    int force_blocks_per_sm;
    CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&force_blocks_per_sm,
            Kernel::compute_forces, FORCE_WARPS * WARP_SIZE, 0));
    force_blocks = force_blocks_per_sm * sm_count;

    CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    CUDA_CHECK(cudaStreamCreateWithFlags(&copy_stream, cudaStreamNonBlocking));
    CUDA_CHECK(cudaEventCreateWithFlags(&copy_done, cudaEventDisableTiming));
    for (CUevent_st*& event : step_done) {
        CUDA_CHECK(cudaEventCreateWithFlags(&event, cudaEventDisableTiming));
    }

    alloc(pos_d, n);
    alloc(vel_d, n);
    alloc(mass_d, n);
    alloc(id_d, n);
    alloc(pos_alt_d, n);
    alloc(vel_alt_d, n);
    alloc(mass_alt_d, n);
    alloc(id_alt_d, n);
    alloc(keys_d, n);
    alloc(keys_alt_d, n);
    alloc(order_d, n);
    alloc(order_alt_d, n);
    alloc(body_pos_d, n);
    alloc(key_delta_d, n);
    alloc(seeds_d, n);
    alloc(arrivals_d, 2 * static_cast<size_t>(n));
    alloc(merge_bound_d, n);
    alloc(nodes_d, n);
    alloc(root_d, 1);
    alloc(open_sq_by_level_d, MAX_TREE_DEPTH);
    alloc(frame_d, 1);
    alloc(counters_d, 1);
    alloc(retry_bodies_d, n);
    alloc(stacks_d, static_cast<size_t>(force_blocks) * FORCE_WARPS * STACK_CAPACITY);
    alloc(pos_out_d, n);
    alloc(vel_out_d, n);
    CUDA_CHECK(cub::DeviceRadixSort::SortPairs(nullptr, sort_temp_bytes, keys_d, keys_alt_d,
            order_d, order_alt_d, n, 0, KEY_BITS));
    CUDA_CHECK(cudaMalloc(&sort_temp_d, sort_temp_bytes));

    // Nodes of level l have cells of side 2^-l, levels past KEY_BITS / 2 only split equal keys
    float open_sq_by_level[MAX_TREE_DEPTH];
    for (int32_t level = 0; level < MAX_TREE_DEPTH; level++) {
        const double open_sq = std::ldexp(inv_theta_sq, -2 * std::min(level, KEY_BITS / 2));
        open_sq_by_level[level] = static_cast<float>(std::min<double>(open_sq, FLT_MAX));
    }
    CUDA_CHECK(cudaMemcpy(open_sq_by_level_d, open_sq_by_level, sizeof(open_sq_by_level),
            cudaMemcpyHostToDevice));

    // compute_forces collects the bounding box for the next iteration, the first one's is set here
    Counters counters{.bounds = {UINT64_MAX, 0, UINT64_MAX, 0}};
    for (uint64_t i = 0; i < bodies.n; i++) {
        const auto& p = bodies.pos(i);
        counters.bounds[0] = std::min(counters.bounds[0], Device::ordered(p.x));
        counters.bounds[1] = std::max(counters.bounds[1], Device::ordered(p.x));
        counters.bounds[2] = std::min(counters.bounds[2], Device::ordered(p.y));
        counters.bounds[3] = std::max(counters.bounds[3], Device::ordered(p.y));
    }
    CUDA_CHECK(cudaMemcpy(counters_d, &counters, sizeof(Counters), cudaMemcpyHostToDevice));

    for (uint64_t i = 0; i < bodies.n; i++) {
        total_mass += bodies.mass(i);
    }
    if (!(total_mass > 0.0)) {
        total_mass = 1.0;  // no gravity at all, any scale works
    }
    std::vector<float> mass(n);
    std::vector<uint32_t> id(n);
    for (uint32_t i = 0; i < n; i++) {
        mass[i] = static_cast<float>(bodies.mass(i) / total_mass);
        id[i] = i;
    }
    CUDA_CHECK(cudaMemcpy(pos_d, bodies.pos_data(), sizeof(double2) * n, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(vel_d, bodies.vel_data(), sizeof(double2) * n, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(mass_d, mass.data(), sizeof(float) * n, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(id_d, id.data(), sizeof(uint32_t) * n, cudaMemcpyHostToDevice));

    if (n == 0) {
        return;  // nothing to copy back
    }
    // Page-locked `bodies` lets the copies back run alongside the simulation
    host_bodies_pinned =
            cudaHostRegister(bodies.pos_data(), sizeof(double2) * n, cudaHostRegisterDefault)
                    == cudaSuccess
            && cudaHostRegister(bodies.vel_data(), sizeof(double2) * n, cudaHostRegisterDefault)
                       == cudaSuccess;
    if (!host_bodies_pinned) {
        Log::warning("Could not page-lock the bodies, copying them back will stall the GPU");
        cudaHostUnregister(bodies.pos_data());
        cudaGetLastError();
    }
}

// Doesn't throw: it runs in the destructor, possibly after a CUDA error or a partial init
void BarnesHutCuda::release_device_resources() {
    cudaError_t first_error = cudaSuccess;
    const auto check = [&first_error](cudaError_t err) {
        if (first_error == cudaSuccess) {
            first_error = err;
        }
    };
    if (host_bodies_pinned) {
        check(cudaHostUnregister(bodies.pos_data()));
        check(cudaHostUnregister(bodies.vel_data()));
    }
    for (void* ptr : std::initializer_list<void*>{pos_d, vel_d, mass_d, id_d, pos_alt_d, vel_alt_d,
                 mass_alt_d, id_alt_d, keys_d, keys_alt_d, order_d, order_alt_d, sort_temp_d,
                 body_pos_d, key_delta_d, seeds_d, arrivals_d, merge_bound_d, nodes_d, root_d,
                 open_sq_by_level_d, frame_d, counters_d, retry_bodies_d, stacks_d, pos_out_d,
                 vel_out_d}) {
        if (ptr) {
            check(cudaFree(ptr));
        }
    }
    for (CUevent_st* event : {step_done[0], step_done[1], copy_done}) {
        if (event) {
            check(cudaEventDestroy(event));
        }
    }
    for (CUstream_st* s : {copy_stream, stream}) {
        if (s) {
            check(cudaStreamDestroy(s));
        }
    }
    if (first_error != cudaSuccess) {
        Log::warning("Releasing GPU resources failed: {}", cudaGetErrorString(first_error));
    }
}

void BarnesHutCuda::step() {
    if (n == 0) {
        return;
    }
    const uint32_t grid = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;

    Kernel::start_iteration<<<1, 1, 0, stream>>>(epsilon_squared,
            Constants::Simulation::G * total_mass, counters_d, frame_d);
    Kernel::compute_keys<<<grid, BLOCK_SIZE, 0, stream>>>(pos_d, n, frame_d, keys_d, order_d);
    CUDA_CHECK(cub::DeviceRadixSort::SortPairs(sort_temp_d, sort_temp_bytes, keys_d, keys_alt_d,
            order_d, order_alt_d, n, 0, KEY_BITS, stream));
    Kernel::reorder_bodies<<<grid, BLOCK_SIZE, 0, stream>>>(order_alt_d, keys_alt_d, frame_d, n,
            pos_d, vel_d, mass_d, id_d, pos_alt_d, vel_alt_d, mass_alt_d, id_alt_d, body_pos_d,
            key_delta_d, merge_bound_d);
    std::swap(pos_d, pos_alt_d);
    std::swap(vel_d, vel_alt_d);
    std::swap(mass_d, mass_alt_d);
    std::swap(id_d, id_alt_d);

    if (n > 1) {
        Kernel::build_lower_nodes<<<grid, BLOCK_SIZE, 0, stream>>>(n, key_delta_d, body_pos_d,
                mass_d, open_sq_by_level_d, counters_d, seeds_d, nodes_d, root_d);
        Kernel::build_upper_nodes<<<grid, BLOCK_SIZE, 0, stream>>>(n, key_delta_d, body_pos_d,
                open_sq_by_level_d, counters_d, seeds_d, arrivals_d, merge_bound_d, nodes_d,
                root_d);
    }
    // Stage the bodies for the copy to the host, unless the previous copy is still in flight
    const bool copy = cudaEventQuery(copy_done) == cudaSuccess;
    for (const bool retry_pass : {false, true}) {
        Kernel::compute_forces<<<force_blocks, FORCE_WARPS * WARP_SIZE, 0, stream>>>(nodes_d,
                body_pos_d, mass_d, frame_d, root_d, id_d, n, sim_cfg.timestep, retry_pass,
                counters_d, retry_bodies_d, stacks_d, pos_d, vel_d, copy ? pos_out_d : nullptr,
                copy ? vel_out_d : nullptr);
    }
    CUDA_CHECK(cudaGetLastError());
    if (copy) {
        copy_bodies_to_host_async();
    }

    // Keep one iteration queued behind the running one, so the GPU never waits for the host
    CUDA_CHECK(cudaEventRecord(step_done[queued_steps % 2], stream));
    if (queued_steps > 0) {
        CUDA_CHECK(cudaEventSynchronize(step_done[(queued_steps - 1) % 2]));
    }
    queued_steps++;
}

// Copies the bodies staged in their original order back to `bodies`, alongside the simulation
void BarnesHutCuda::copy_bodies_to_host_async() {
    CUDA_CHECK(cudaEventRecord(copy_done, stream));
    CUDA_CHECK(cudaStreamWaitEvent(copy_stream, copy_done));
    CUDA_CHECK(cudaMemcpyAsync(bodies.pos_data(), pos_out_d, sizeof(double2) * n,
            cudaMemcpyDeviceToHost, copy_stream));
    CUDA_CHECK(cudaMemcpyAsync(bodies.vel_data(), vel_out_d, sizeof(double2) * n,
            cudaMemcpyDeviceToHost, copy_stream));
    CUDA_CHECK(cudaEventRecord(copy_done, copy_stream));
}

void BarnesHutCuda::sync_bodies_to_host() {
    if (n == 0) {
        return;
    }
    CUDA_CHECK(cudaStreamSynchronize(copy_stream));
    Kernel::unsort_bodies<<<(n + BLOCK_SIZE - 1) / BLOCK_SIZE, BLOCK_SIZE, 0, stream>>>(pos_d,
            vel_d, id_d, n, pos_out_d, vel_out_d);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaMemcpyAsync(bodies.pos_data(), pos_out_d, sizeof(double2) * n,
            cudaMemcpyDeviceToHost, stream));
    CUDA_CHECK(cudaMemcpyAsync(bodies.vel_data(), vel_out_d, sizeof(double2) * n,
            cudaMemcpyDeviceToHost, stream));
    CUDA_CHECK(cudaStreamSynchronize(stream));
}
