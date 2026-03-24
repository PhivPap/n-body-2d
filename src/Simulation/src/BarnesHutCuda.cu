#include "Simulation/BarnesHutCuda.hpp"

#include <cuda_runtime.h>
#include <cub/cub.cuh>

#include "Logger/Logger.hpp"

constexpr uint32_t BLOCK_SIZE = 256;
constexpr int32_t  LEAF_THRESHOLD = 1;  // max bodies in a leaf node before splitting

#define CUDA_CHECK(call) do {                                                               \
    cudaError_t err = (call);                                                               \
    if (err != cudaSuccess) {                                                               \
        Log::error("CUDA error at {}:{}: {}", __FILE__, __LINE__, cudaGetErrorString(err)); \
        ::exit(EXIT_FAILURE);                                                               \
    }                                                                                       \
} while(0)

// ---------------------------------------------------------------------------
// Device helpers
// ---------------------------------------------------------------------------
namespace Device {
    __device__ double atomic_min(double* addr, double value) {
        using ull = unsigned long long;
        ull *addr_as_ull = (ull*)addr;
        ull old = *addr_as_ull;
        ull assumed;
        do {
            assumed = old;
            if (__longlong_as_double(assumed) <= value) break;
            old = atomicCAS(addr_as_ull, assumed, __double_as_longlong(value));
        } while (assumed != old);
        return __longlong_as_double(old);
    }

    __device__ double atomic_max(double* addr, double value) {
        using ull = unsigned long long;
        ull *addr_as_ull = (ull*)addr;
        ull old = *addr_as_ull;
        ull assumed;
        do {
            assumed = old;
            if (__longlong_as_double(assumed) >= value) break;
            old = atomicCAS(addr_as_ull, assumed, __double_as_longlong(value));
        } while (assumed != old);
        return __longlong_as_double(old);
    }

    // Expand a 32-bit integer into 64 bits by inserting a 0 bit after each bit.
    __device__ __forceinline__ uint64_t expand_bits_2d(uint32_t v) {
        uint64_t x = v;
        x = (x | (x << 16)) & 0x0000FFFF0000FFFFull;
        x = (x | (x <<  8)) & 0x00FF00FF00FF00FFull;
        x = (x | (x <<  4)) & 0x0F0F0F0F0F0F0F0Full;
        x = (x | (x <<  2)) & 0x3333333333333333ull;
        x = (x | (x <<  1)) & 0x5555555555555555ull;
        return x;
    }

    // 64-bit Morton code for 2D point in [0,1)^2. Uses 32 bits per axis.
    __device__ __forceinline__ uint64_t morton_2d(double nx, double ny) {
        const uint32_t ix = __double2uint_rd(fmin(fmax(nx * 4294967296.0, 0.0), 4294967295.0));
        const uint32_t iy = __double2uint_rd(fmin(fmax(ny * 4294967296.0, 0.0), 4294967295.0));
        return (expand_bits_2d(ix) << 1) | expand_bits_2d(iy);
    }
}

// ---------------------------------------------------------------------------
// Kernels
// ---------------------------------------------------------------------------
namespace Kernel {

    // ---- Bounding box ----
    __global__ void compute_bounding_box(const Vector2 * __restrict__ pos, uint32_t n,
                                         BarnesHutCuda::Box *global_bbox) {
        extern __shared__ double sdata[];
        double *sxmin = sdata;
        double *sxmax = sdata +     blockDim.x;
        double *symin = sdata + 2 * blockDim.x;
        double *symax = sdata + 3 * blockDim.x;

        const uint32_t tid = threadIdx.x;
        const uint32_t idx = blockIdx.x * blockDim.x + tid;

        const double x = (idx < n) ? pos[idx].x :  INFINITY;
        const double y = (idx < n) ? pos[idx].y :  INFINITY;

        sxmin[tid] = x;
        sxmax[tid] = (idx < n) ? x : -INFINITY;
        symin[tid] = y;
        symax[tid] = (idx < n) ? y : -INFINITY;
        __syncthreads();

        for (uint32_t stride = blockDim.x / 2; stride > 0; stride >>= 1) {
            if (tid < stride) {
                sxmin[tid] = fmin(sxmin[tid], sxmin[tid + stride]);
                sxmax[tid] = fmax(sxmax[tid], sxmax[tid + stride]);
                symin[tid] = fmin(symin[tid], symin[tid + stride]);
                symax[tid] = fmax(symax[tid], symax[tid + stride]);
            }
            __syncthreads();
        }

        if (tid == 0) {
            Device::atomic_min(&global_bbox->x_min, sxmin[0]);
            Device::atomic_max(&global_bbox->x_max, sxmax[0]);
            Device::atomic_min(&global_bbox->y_min, symin[0]);
            Device::atomic_max(&global_bbox->y_max, symax[0]);
        }
    }

    // ---- Morton code computation ----
    __global__ void compute_morton_codes(const Vector2 * __restrict__ pos, uint32_t n,
                                         const BarnesHutCuda::Box * __restrict__ bbox,
                                         uint64_t *morton, int32_t *indices) {
        const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i >= n) return;

        const BarnesHutCuda::Box b = *bbox;
        const double inv_w = 1.0 / (b.x_max - b.x_min);
        const double inv_h = 1.0 / (b.y_max - b.y_min);

        const double nx = (pos[i].x - b.x_min) * inv_w;
        const double ny = (pos[i].y - b.y_min) * inv_h;

        morton[i]  = Device::morton_2d(nx, ny);
        indices[i] = static_cast<int32_t>(i);
    }

    // ---- Reorder body arrays by sorted index ----
    __global__ void reorder_bodies(const int32_t * __restrict__ sorted_idx,
                                   const Vector2 * __restrict__ pos_in,
                                   const Vector2 * __restrict__ vel_in,
                                   const double  * __restrict__ mass_in,
                                   const int32_t * __restrict__ orig_idx_in,
                                   Vector2 *pos_out, Vector2 *vel_out,
                                   double *mass_out, int32_t *orig_idx_out,
                                   int32_t n) {
        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i >= n) return;
        const int32_t src = sorted_idx[i];
        pos_out[i]      = pos_in[src];
        vel_out[i]      = vel_in[src];
        mass_out[i]     = mass_in[src];
        orig_idx_out[i] = orig_idx_in[src];
    }

    // ---- Tree build: topology only (no COM computation) ----
    // One thread per work-list item. Only does binary search on Morton codes
    // to find child split points and allocate child nodes.

    __device__ int32_t lower_bound_quadrant(const uint64_t * __restrict__ morton,
                                            int32_t lo, int32_t hi,
                                            uint64_t target, int32_t shift) {
        while (lo < hi) {
            int32_t mid = lo + (hi - lo) / 2;
            if (((morton[mid] >> shift) & 3ull) < target)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

    __global__ void build_tree_topology(
            const uint64_t * __restrict__ morton,
            // tree SoA
            double  *t_wsq,
            int32_t *t_c0,     int32_t *t_c1,     int32_t *t_c2,    int32_t *t_c3,
            int32_t *t_bstart, int32_t *t_bend,
            // book-keeping
            const int32_t *work_list, int32_t work_count,
            int32_t *work_list_next, int32_t *work_count_next,
            int32_t *node_alloc, int32_t max_nodes,
            int32_t level) {

        const int32_t tid = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (tid >= work_count) return;

        const int32_t nid = work_list[tid];
        const int32_t start = t_bstart[nid];
        const int32_t end   = t_bend[nid];

        // Leaf condition: too few bodies or max depth reached
        if ((end - start) < LEAF_THRESHOLD || level >= 30) return;

        // Find split points via binary search on 2 Morton bits at this level
        const int32_t shift = 62 - level * 2;

        const int32_t b0 = lower_bound_quadrant(morton, start, end + 1, 0ull, shift);
        const int32_t b1 = lower_bound_quadrant(morton, start, end + 1, 1ull, shift);
        const int32_t b2 = lower_bound_quadrant(morton, start, end + 1, 2ull, shift);
        const int32_t b3 = lower_bound_quadrant(morton, start, end + 1, 3ull, shift);
        const int32_t b4 = end + 1;

        int32_t cs[4], ce[4];
        cs[0] = b0;  ce[0] = b1 - 1;
        cs[1] = b1;  ce[1] = b2 - 1;
        cs[2] = b2;  ce[2] = b3 - 1;
        cs[3] = b3;  ce[3] = b4 - 1;

        int32_t n_children = 0;
        for (int32_t q = 0; q < 4; ++q)
            if (cs[q] <= ce[q]) ++n_children;

        if (n_children == 0) return;

        const int32_t child_base = atomicAdd(node_alloc, 4);
        if (child_base + 3 >= max_nodes) return;

        const double child_wsq = t_wsq[nid] * 0.25;

        t_c0[nid] = child_base + 0;
        t_c1[nid] = child_base + 1;
        t_c2[nid] = child_base + 2;
        t_c3[nid] = child_base + 3;

        for (int32_t q = 0; q < 4; ++q) {
            const int32_t cid = child_base + q;
            t_wsq[cid] = child_wsq;
            t_c0[cid]  = -1;
            t_c1[cid]  = -1;
            t_c2[cid]  = -1;
            t_c3[cid]  = -1;
            if (cs[q] <= ce[q]) {
                t_bstart[cid] = cs[q];
                t_bend[cid]   = ce[q];
                work_list_next[atomicAdd(work_count_next, 1)] = cid;
            } else {
                t_bstart[cid] = -1;
                t_bend[cid]   = -1;
            }
        }
    }

    // ---- COM computation: one pass over all bodies ----
    // Each thread processes one body and atomically accumulates into its
    // leaf node's COM. Then separately propagate up.
    // But for simplicity and correctness we use a single kernel that
    // processes each tree node (from leaf list) and computes its COM via reduction.

    // Compute COM for leaf nodes in a given level's work list.
    // Skips internal nodes (child0 >= 0).
    // Launched as 1 block per node.
    __global__ void compute_leaf_com(
            const Vector2 * __restrict__ pos,
            const double  * __restrict__ mass,
            double *t_mass, double *t_com_x, double *t_com_y,
            const int32_t *node_list, int32_t node_count,
            const int32_t *t_bstart, const int32_t *t_bend,
            const int32_t *t_c0) {

        if (static_cast<int32_t>(blockIdx.x) >= node_count) return;

        const int32_t nid = node_list[blockIdx.x];

        // Skip internal nodes
        if (t_c0[nid] >= 0) return;

        const int32_t start = t_bstart[nid];
        const int32_t end   = t_bend[nid];

        // Fast path for single-body leaf (overwhelmingly common case)
        if (start == end) {
            if (threadIdx.x == 0) {
                t_mass[nid]  = mass[start];
                t_com_x[nid] = pos[start].x;
                t_com_y[nid] = pos[start].y;
            }
            return;
        }

        // Multi-body leaf: reduction
        extern __shared__ char smem_raw[];
        double *sMass = reinterpret_cast<double*>(smem_raw);
        double *sComX = sMass + blockDim.x;
        double *sComY = sComX + blockDim.x;
        const int32_t tx = static_cast<int32_t>(threadIdx.x);

        double M = 0.0, Rx = 0.0, Ry = 0.0;
        for (int32_t i = start + tx; i <= end; i += static_cast<int32_t>(blockDim.x)) {
            const double m = mass[i];
            M  += m;
            Rx += m * pos[i].x;
            Ry += m * pos[i].y;
        }
        sMass[tx] = M;  sComX[tx] = Rx;  sComY[tx] = Ry;
        __syncthreads();

        for (uint32_t s = blockDim.x / 2; s > 0; s >>= 1) {
            if (static_cast<uint32_t>(tx) < s) {
                sMass[tx] += sMass[tx + s];
                sComX[tx] += sComX[tx + s];
                sComY[tx] += sComY[tx + s];
            }
            __syncthreads();
        }

        if (tx == 0) {
            t_mass[nid]  = sMass[0];
            t_com_x[nid] = sComX[0] / sMass[0];
            t_com_y[nid] = sComY[0] / sMass[0];
        }
    }

    // Propagate COM from children to parents.
    // Skips leaf nodes (child0 < 0).
    // One thread per node in the level's work list.
    __global__ void propagate_com_up(
            double *t_mass, double *t_com_x, double *t_com_y,
            const int32_t *t_c0, const int32_t *t_c1,
            const int32_t *t_c2, const int32_t *t_c3,
            const int32_t *t_bstart,
            const int32_t *node_list, int32_t count) {

        const int32_t tid = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (tid >= count) return;

        const int32_t nid = node_list[tid];

        // Skip leaf nodes
        if (t_c0[nid] < 0) return;

        const int32_t children[4] = {t_c0[nid], t_c1[nid], t_c2[nid], t_c3[nid]};

        double M = 0.0, Rx = 0.0, Ry = 0.0;
        for (int32_t q = 0; q < 4; ++q) {
            const int32_t cid = children[q];
            if (t_bstart[cid] < 0) continue;  // empty child
            const double cm = t_mass[cid];
            M  += cm;
            Rx += cm * t_com_x[cid];
            Ry += cm * t_com_y[cid];
        }
        t_mass[nid]  = M;
        t_com_x[nid] = Rx / M;
        t_com_y[nid] = Ry / M;
    }

    // ---- Pack SoA tree → AoS ForceNode array ----
    __global__ void pack_force_nodes(
            const double  * __restrict__ t_mass,
            const double  * __restrict__ t_com_x,
            const double  * __restrict__ t_com_y,
            const double  * __restrict__ t_wsq,
            const int32_t * __restrict__ t_c0,
            const int32_t * __restrict__ t_bstart,
            const int32_t * __restrict__ t_bend,
            ForceNode *out, int32_t node_count) {

        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i >= node_count) return;

        ForceNode fn;
        fn.com_x      = t_com_x[i];
        fn.com_y      = t_com_y[i];
        fn.mass       = t_mass[i];
        fn.width_sq   = t_wsq[i];
        fn.c0         = t_c0[i];
        fn.body_start = t_bstart[i];
        fn.body_end   = t_bend[i];
        // Build child mask: children are c0, c0+1, c0+2, c0+3
        uint8_t mask = 0;
        if (fn.c0 >= 0) {
            if (t_bstart[fn.c0    ] >= 0) mask |= 1u;
            if (t_bstart[fn.c0 + 1] >= 0) mask |= 2u;
            if (t_bstart[fn.c0 + 2] >= 0) mask |= 4u;
            if (t_bstart[fn.c0 + 3] >= 0) mask |= 8u;
        }
        fn.child_mask = mask;
        out[i] = fn;
    }

    // ---- Force computation (one thread per body, SoA tree) ----
    __global__ void compute_force(
            // tree SoA (read-only)
            const double  * __restrict__ t_mass,
            const double  * __restrict__ t_com_x,
            const double  * __restrict__ t_com_y,
            const double  * __restrict__ t_wsq,
            const int32_t * __restrict__ t_c0,
            const int32_t * __restrict__ t_bstart,
            const int32_t * __restrict__ t_bend,
            // body data
            const Vector2 * __restrict__ pos,
            const double  * __restrict__ mass,
            Vector2 *vel,
            int32_t n, double epsilon_sq, double G_dt) {

        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i >= n) return;

        constexpr double THETA_SQ = Constants::Simulation::THETA * Constants::Simulation::THETA;

        const double body_x = pos[i].x;
        const double body_y = pos[i].y;
        double acc_x = 0.0, acc_y = 0.0;

        int32_t stack[32];
        int32_t top = 0;
        stack[0] = 0;  // root

        while (top >= 0) {
            const int32_t nid = stack[top--];

            const int32_t bs = __ldg(&t_bstart[nid]);
            if (bs < 0) continue;  // empty node

            const double node_mass = __ldg(&t_mass[nid]);
            const double cx = __ldg(&t_com_x[nid]);
            const double cy = __ldg(&t_com_y[nid]);

            const double dx = cx - body_x;
            const double dy = cy - body_y;
            const double dist_sq = dx * dx + dy * dy;

            const int32_t c0 = __ldg(&t_c0[nid]);

            // Leaf node: c0 == -1
            if (c0 < 0) {
                const int32_t be = __ldg(&t_bend[nid]);
                if (bs == be) {
                    // Single-body leaf
                    if (bs != i) {
                        const double d2 = dist_sq + epsilon_sq;
                        const double inv_r = rsqrt(d2);
                        const double inv_r3 = inv_r * inv_r * inv_r;
                        acc_x += dx * (node_mass * inv_r3);
                        acc_y += dy * (node_mass * inv_r3);
                    }
                } else if (i < bs || i > be) {
                    // Body outside this leaf — use COM
                    const double r2 = dist_sq + epsilon_sq;
                    const double inv_r = rsqrt(r2);
                    const double inv_r3 = inv_r * inv_r * inv_r;
                    const double a = node_mass * inv_r3;
                    acc_x += dx * a;
                    acc_y += dy * a;
                } else {
                    // Body inside multi-body leaf — iterate
                    for (int32_t j = bs; j <= be; ++j) {
                        if (j == i) continue;
                        const Vector2 p = pos[j];
                        const double drx = p.x - body_x;
                        const double dry = p.y - body_y;
                        const double d2 = drx * drx + dry * dry + epsilon_sq;
                        const double inv_r = rsqrt(d2);
                        const double inv_r3 = inv_r * inv_r * inv_r;
                        acc_x += drx * (mass[j] * inv_r3);
                        acc_y += dry * (mass[j] * inv_r3);
                    }
                }
                continue;
            }

            // Internal node — opening angle test
            const double wsq = __ldg(&t_wsq[nid]);
            if (wsq < THETA_SQ * dist_sq) {
                const double r2 = dist_sq + epsilon_sq;
                const double inv_r = rsqrt(r2);
                const double inv_r3 = inv_r * inv_r * inv_r;
                const double a = node_mass * inv_r3;
                acc_x += dx * a;
                acc_y += dy * a;
                continue;
            }

            // Push non-empty children (contiguous: c0, c0+1, c0+2, c0+3)
            if (__ldg(&t_bstart[c0 + 3]) >= 0) stack[++top] = c0 + 3;
            if (__ldg(&t_bstart[c0 + 2]) >= 0) stack[++top] = c0 + 2;
            if (__ldg(&t_bstart[c0 + 1]) >= 0) stack[++top] = c0 + 1;
            if (__ldg(&t_bstart[c0    ]) >= 0) stack[++top] = c0;
        }

        vel[i].x += acc_x * G_dt;
        vel[i].y += acc_y * G_dt;
    }

    // ---- Update positions ----
    __global__ void update_positions(Vector2 *pos, const Vector2 *vel, int32_t n, double dt) {
        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i < n) {
            pos[i].x += vel[i].x * dt;
            pos[i].y += vel[i].y * dt;
        }
    }

    // ---- Scatter sorted data back to original body order ----
    __global__ void scatter_to_original_order(const Vector2 *sorted_pos, const Vector2 *sorted_vel,
                                              const int32_t *idx, Vector2 *out_pos, Vector2 *out_vel,
                                              int32_t n) {
        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i < n) {
            const int32_t orig = idx[i];
            out_pos[orig] = sorted_pos[i];
            out_vel[orig] = sorted_vel[i];
        }
    }

    // ---- Initialize index array to identity ----
    __global__ void init_indices(int32_t *idx, int32_t n) {
        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i < n) idx[i] = i;
    }

} // namespace Kernel


// ===========================================================================
// Host-side implementations (called from BarnesHutCuda methods)
// ===========================================================================

static void alloc_tree_soa(TreeNodesSoA &t, int32_t count) {
    const size_t d_bytes = sizeof(double)  * count;
    const size_t i_bytes = sizeof(int32_t) * count;
    CUDA_CHECK(cudaMalloc(&t.mass,       d_bytes));
    CUDA_CHECK(cudaMalloc(&t.com_x,      d_bytes));
    CUDA_CHECK(cudaMalloc(&t.com_y,      d_bytes));
    CUDA_CHECK(cudaMalloc(&t.width_sq,   d_bytes));
    CUDA_CHECK(cudaMalloc(&t.child0,     i_bytes));
    CUDA_CHECK(cudaMalloc(&t.child1,     i_bytes));
    CUDA_CHECK(cudaMalloc(&t.child2,     i_bytes));
    CUDA_CHECK(cudaMalloc(&t.child3,     i_bytes));
    CUDA_CHECK(cudaMalloc(&t.body_start, i_bytes));
    CUDA_CHECK(cudaMalloc(&t.body_end,   i_bytes));
}

static void free_tree_soa(TreeNodesSoA &t) {
    CUDA_CHECK(cudaFree(t.mass));
    CUDA_CHECK(cudaFree(t.com_x));
    CUDA_CHECK(cudaFree(t.com_y));
    CUDA_CHECK(cudaFree(t.width_sq));
    CUDA_CHECK(cudaFree(t.child0));
    CUDA_CHECK(cudaFree(t.child1));
    CUDA_CHECK(cudaFree(t.child2));
    CUDA_CHECK(cudaFree(t.child3));
    CUDA_CHECK(cudaFree(t.body_start));
    CUDA_CHECK(cudaFree(t.body_end));
}

void BarnesHutCuda::init_device_resources() {
    const int32_t n = static_cast<int32_t>(bodies.n);
    const auto mass_bytes = sizeof(double)  * n;
    const auto pos_bytes  = sizeof(Vector2) * n;
    const auto vel_bytes  = sizeof(Vector2) * n;
    const auto idx_bytes  = sizeof(int32_t) * n;

    // Body arrays (primary + alt for sorting)
    CUDA_CHECK(cudaMalloc(&mass_d,     mass_bytes));
    CUDA_CHECK(cudaMalloc(&mass_alt_d, mass_bytes));
    CUDA_CHECK(cudaMalloc(&pos_d,      pos_bytes));
    CUDA_CHECK(cudaMalloc(&pos_alt_d,  pos_bytes));
    CUDA_CHECK(cudaMalloc(&vel_d,      vel_bytes));
    CUDA_CHECK(cudaMalloc(&vel_alt_d,  vel_bytes));
    CUDA_CHECK(cudaMalloc(&idx_d,      idx_bytes));
    CUDA_CHECK(cudaMalloc(&idx_alt_d,  idx_bytes));

    // Morton codes + sort indices
    CUDA_CHECK(cudaMalloc(&morton_d,       sizeof(uint64_t) * n));
    CUDA_CHECK(cudaMalloc(&morton_alt_d,   sizeof(uint64_t) * n));
    CUDA_CHECK(cudaMalloc(&sort_idx_d,     idx_bytes));
    CUDA_CHECK(cudaMalloc(&sort_idx_alt_d, idx_bytes));

    // Bounding box
    CUDA_CHECK(cudaMalloc(&bounding_box_d, sizeof(Box)));

    // Tree nodes (SoA)
    alloc_tree_soa(tree, max_quads);
    CUDA_CHECK(cudaMalloc(&node_count_d, sizeof(int32_t)));
    CUDA_CHECK(cudaMalloc(&node_parent_d, sizeof(int32_t) * max_quads));

    // Packed AoS for force traversal
    CUDA_CHECK(cudaMalloc(&force_nodes_d, sizeof(ForceNode) * max_quads));

    // Work lists
    const int32_t wl_size = n + 1;
    CUDA_CHECK(cudaMalloc(&work_list_d,      sizeof(int32_t) * wl_size));
    CUDA_CHECK(cudaMalloc(&work_list_next_d, sizeof(int32_t) * wl_size));
    CUDA_CHECK(cudaMalloc(&work_count_d,     sizeof(int32_t)));

    // Query CUB temp storage size for radix sort
    cub_temp_bytes = 0;
    cub::DeviceRadixSort::SortPairs(nullptr, cub_temp_bytes,
                                    morton_d, morton_alt_d,
                                    sort_idx_d, sort_idx_alt_d,
                                    n);
    CUDA_CHECK(cudaMalloc(&cub_temp_d, cub_temp_bytes));

    // Upload initial body data
    CUDA_CHECK(cudaMemcpy(mass_d, bodies.mass_data(), mass_bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(pos_d,  bodies.pos_data(),  pos_bytes,  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(vel_d,  bodies.vel_data(),  vel_bytes,  cudaMemcpyHostToDevice));

    // Initialize index to identity
    const uint32_t grid = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    Kernel::init_indices<<<grid, BLOCK_SIZE>>>(idx_d, n);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
}

void BarnesHutCuda::release_device_resources() {
    CUDA_CHECK(cudaFree(mass_d));
    CUDA_CHECK(cudaFree(mass_alt_d));
    CUDA_CHECK(cudaFree(pos_d));
    CUDA_CHECK(cudaFree(pos_alt_d));
    CUDA_CHECK(cudaFree(vel_d));
    CUDA_CHECK(cudaFree(vel_alt_d));
    CUDA_CHECK(cudaFree(idx_d));
    CUDA_CHECK(cudaFree(idx_alt_d));
    CUDA_CHECK(cudaFree(morton_d));
    CUDA_CHECK(cudaFree(morton_alt_d));
    CUDA_CHECK(cudaFree(sort_idx_d));
    CUDA_CHECK(cudaFree(sort_idx_alt_d));
    CUDA_CHECK(cudaFree(cub_temp_d));
    CUDA_CHECK(cudaFree(bounding_box_d));
    free_tree_soa(tree);
    CUDA_CHECK(cudaFree(node_count_d));
    CUDA_CHECK(cudaFree(node_parent_d));
    CUDA_CHECK(cudaFree(force_nodes_d));
    CUDA_CHECK(cudaFree(work_list_d));
    CUDA_CHECK(cudaFree(work_list_next_d));
    CUDA_CHECK(cudaFree(work_count_d));
}

void BarnesHutCuda::sync_from_gpu_bodies() {
    const int32_t n = static_cast<int32_t>(bodies.n);
    const uint32_t grid = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Scatter sorted GPU data back to original body order
    Kernel::scatter_to_original_order<<<grid, BLOCK_SIZE>>>(
        pos_d, vel_d, idx_d, pos_alt_d, vel_alt_d, n);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(bodies.pos_data(), pos_alt_d,
                          sizeof(Vector2) * n, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(bodies.vel_data(), vel_alt_d,
                          sizeof(Vector2) * n, cudaMemcpyDeviceToHost));
}

void BarnesHutCuda::compute_bounding_box() {
    bounding_box = Box{
        .x_min =  std::numeric_limits<double>::max(),
        .x_max = -std::numeric_limits<double>::max(),
        .y_min =  std::numeric_limits<double>::max(),
        .y_max = -std::numeric_limits<double>::max(),
    };
    CUDA_CHECK(cudaMemcpy(bounding_box_d, &bounding_box, sizeof(Box), cudaMemcpyHostToDevice));

    const uint32_t grid = (bodies.n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    constexpr uint32_t shared_mem = 4 * BLOCK_SIZE * sizeof(double);
    Kernel::compute_bounding_box<<<grid, BLOCK_SIZE, shared_mem>>>(
        pos_d, static_cast<uint32_t>(bodies.n), bounding_box_d);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(&bounding_box, bounding_box_d, sizeof(Box), cudaMemcpyDeviceToHost));

    // Square the bounding box
    const double x_range = bounding_box.x_max - bounding_box.x_min;
    const double y_range = bounding_box.y_max - bounding_box.y_min;
    if (x_range > y_range) {
        const double mid = (bounding_box.y_min + bounding_box.y_max) * 0.5;
        bounding_box.y_min = mid - x_range * 0.5;
        bounding_box.y_max = mid + x_range * 0.5;
    } else {
        const double mid = (bounding_box.x_min + bounding_box.x_max) * 0.5;
        bounding_box.x_min = mid - y_range * 0.5;
        bounding_box.x_max = mid + y_range * 0.5;
    }
    // Upload squared bbox for Morton code computation
    CUDA_CHECK(cudaMemcpy(bounding_box_d, &bounding_box, sizeof(Box), cudaMemcpyHostToDevice));
}

void BarnesHutCuda::sort_bodies_by_morton() {
    const int32_t n = static_cast<int32_t>(bodies.n);
    const uint32_t grid = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // 1. Compute Morton codes
    Kernel::compute_morton_codes<<<grid, BLOCK_SIZE>>>(
        pos_d, static_cast<uint32_t>(n), bounding_box_d, morton_d, sort_idx_d);
    CUDA_CHECK(cudaGetLastError());

    // 2. Radix sort by Morton code
    cub::DeviceRadixSort::SortPairs(cub_temp_d, cub_temp_bytes,
                                    morton_d, morton_alt_d,
                                    sort_idx_d, sort_idx_alt_d,
                                    n);
    CUDA_CHECK(cudaGetLastError());

    // 3. Reorder body arrays
    Kernel::reorder_bodies<<<grid, BLOCK_SIZE>>>(
        sort_idx_alt_d,
        pos_d, vel_d, mass_d, idx_d,
        pos_alt_d, vel_alt_d, mass_alt_d, idx_alt_d,
        n);
    CUDA_CHECK(cudaGetLastError());

    // Swap primary ↔ alt so primary holds sorted data
    std::swap(pos_d,  pos_alt_d);
    std::swap(vel_d,  vel_alt_d);
    std::swap(mass_d, mass_alt_d);
    std::swap(idx_d,  idx_alt_d);

    // morton_alt_d now holds sorted Morton codes — swap so morton_d is sorted
    std::swap(morton_d, morton_alt_d);

    CUDA_CHECK(cudaDeviceSynchronize());
}

void BarnesHutCuda::build_quad_tree() {
    const int32_t n = static_cast<int32_t>(bodies.n);

    // Initialize root node (index 0)
    const double root_width_sq = std::pow(bounding_box.x_max - bounding_box.x_min, 2.0);

    // Reset node allocator: root = 0, next alloc starts at 1
    int32_t alloc_start = 1;
    CUDA_CHECK(cudaMemcpy(node_count_d, &alloc_start, sizeof(int32_t), cudaMemcpyHostToDevice));

    // Set root node fields
    int32_t zero_i = 0;
    int32_t minus_one = -1;
    int32_t n_minus_1 = n - 1;

    CUDA_CHECK(cudaMemcpy(tree.width_sq   + 0, &root_width_sq,sizeof(double),  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(tree.child0     + 0, &minus_one,    sizeof(int32_t), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(tree.child1     + 0, &minus_one,    sizeof(int32_t), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(tree.child2     + 0, &minus_one,    sizeof(int32_t), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(tree.child3     + 0, &minus_one,    sizeof(int32_t), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(tree.body_start + 0, &zero_i,       sizeof(int32_t), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(tree.body_end   + 0, &n_minus_1,    sizeof(int32_t), cudaMemcpyHostToDevice));

    // ---------------------------------------------------------------
    // Phase 1: Build topology level-by-level.
    // All work lists stored contiguously in work_list_d.
    // Only copy back work_count (one int32_t) per level.
    // ---------------------------------------------------------------

    // Level offsets: level_offsets[i] = start index within work_list_d for level i
    // level_counts[i] = number of work items at level i
    std::vector<int32_t> level_offsets;
    std::vector<int32_t> level_counts;

    // Seed root at offset 0 in work_list_d
    int32_t root_idx = 0;
    CUDA_CHECK(cudaMemcpy(work_list_d, &root_idx, sizeof(int32_t), cudaMemcpyHostToDevice));

    int32_t cur_offset = 0;
    int32_t work_count = 1;

    for (int32_t level = 0; level < 31; ++level) {
        if (work_count == 0) break;

        level_offsets.push_back(cur_offset);
        level_counts.push_back(work_count);

        // Next level's work items will be written starting after all existing items
        const int32_t next_offset = cur_offset + work_count;

        CUDA_CHECK(cudaMemset(work_count_d, 0, sizeof(int32_t)));

        const uint32_t topo_grid = (work_count + BLOCK_SIZE - 1) / BLOCK_SIZE;
        Kernel::build_tree_topology<<<topo_grid, BLOCK_SIZE>>>(
            morton_d,
            tree.width_sq,
            tree.child0, tree.child1, tree.child2, tree.child3,
            tree.body_start, tree.body_end,
            work_list_d + cur_offset, work_count,
            work_list_d + next_offset, work_count_d,
            node_count_d, max_quads,
            level);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());

        CUDA_CHECK(cudaMemcpy(&work_count, work_count_d, sizeof(int32_t), cudaMemcpyDeviceToHost));
        cur_offset = next_offset;
    }

    // ---------------------------------------------------------------
    // Phase 2: Compute COM bottom-up.
    // Process levels from deepest to shallowest.
    // At each level, launch compute_leaf_com (skips internals) and
    // propagate_com_up (skips leaves). Both kernels self-filter
    // using child0 — no host-side node classification needed.
    // ---------------------------------------------------------------

    const int32_t num_levels = static_cast<int32_t>(level_offsets.size());
    for (int32_t lvl = num_levels - 1; lvl >= 0; --lvl) {
        const int32_t off = level_offsets[lvl];
        const int32_t cnt = level_counts[lvl];
        const int32_t *level_nodes = work_list_d + off;

        // Leaf COM: one block per node, kernel skips internal nodes
        constexpr uint32_t leaf_block = 128;
        constexpr uint32_t leaf_smem = 3 * leaf_block * sizeof(double);
        Kernel::compute_leaf_com<<<cnt, leaf_block, leaf_smem>>>(
            pos_d, mass_d,
            tree.mass, tree.com_x, tree.com_y,
            level_nodes, cnt,
            tree.body_start, tree.body_end,
            tree.child0);
        CUDA_CHECK(cudaGetLastError());

        // Propagate COM: one thread per node, kernel skips leaves
        const uint32_t prop_grid = (cnt + BLOCK_SIZE - 1) / BLOCK_SIZE;
        Kernel::propagate_com_up<<<prop_grid, BLOCK_SIZE>>>(
            tree.mass, tree.com_x, tree.com_y,
            tree.child0, tree.child1, tree.child2, tree.child3,
            tree.body_start,
            level_nodes, cnt);
        CUDA_CHECK(cudaGetLastError());
    }

    CUDA_CHECK(cudaDeviceSynchronize());
}

void BarnesHutCuda::compute_forces() {
    const int32_t n = static_cast<int32_t>(bodies.n);
    const double G_dt = Constants::Simulation::G * timestep;

    const uint32_t grid = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    Kernel::compute_force<<<grid, BLOCK_SIZE>>>(
        tree.mass, tree.com_x, tree.com_y, tree.width_sq,
        tree.child0,
        tree.body_start, tree.body_end,
        pos_d, mass_d, vel_d,
        n, epsilon_squared, G_dt);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
}

void BarnesHutCuda::update_positions() {
    const int32_t n = static_cast<int32_t>(bodies.n);
    const uint32_t grid = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    Kernel::update_positions<<<grid, BLOCK_SIZE>>>(pos_d, vel_d, n, timestep);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
}
