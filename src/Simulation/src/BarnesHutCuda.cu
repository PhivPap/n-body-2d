#include "Simulation/BarnesHutCuda.hpp"

#include <cuda_runtime.h>

#include "Logger/Logger.hpp"

constexpr auto BLOCK_SIZE = 256;

#define CUDA_CHECK(call) do {                                                               \
    cudaError_t err = (call);                                                               \
    if (err != cudaSuccess) {                                                               \
        Log::error("CUDA error at {}:{}: {}", __FILE__, __LINE__, cudaGetErrorString(err)); \
        ::exit(EXIT_FAILURE);                                                               \
    }                                                                                       \
} while(0)

namespace Device {
    __device__ double shfl_down_double(uint32_t mask, double val, int32_t delta) {
        int32_t lo = __double2loint(val);
        int32_t hi = __double2hiint(val);
        lo = __shfl_down_sync(mask, lo, delta);
        hi = __shfl_down_sync(mask, hi, delta);
        return __hiloint2double(hi, lo);
    }

    __device__ double atomic_min(double* addr, double value) {
        using ull = unsigned long long;
        ull *addr_as_ull = (ull*)addr;
        ull old = *addr_as_ull;
        ull assumed;
    
        do {
            assumed = old;
            double old_val = __longlong_as_double(assumed);
            if (old_val <= value)
                break;
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
            double old_val = __longlong_as_double(assumed);
            if (old_val >= value)
                break;
            old = atomicCAS(addr_as_ull, assumed, __double_as_longlong(value));
        } while (assumed != old);
    
        return __longlong_as_double(old);
    }

    __device__ QUAD::Quadrant get_quadrant(Vector2 quad_center, 
            Vector2 pos) {
        return pos.x < quad_center.x ?
                (pos.y < quad_center.y ? QUAD::Quadrant::TopLeft : QUAD::Quadrant::BotLeft) :
                (pos.y < quad_center.y ? QUAD::Quadrant::TopRight : QUAD::Quadrant::BotRight);
    }

    __device__ Rect get_child_boundraries(const Rect &boundaries, 
            Vector2 center, QUAD::Quadrant quadrant) {
        const auto half_size_x = boundaries.size.x / 2.0;
        const auto half_size_y = boundaries.size.y / 2.0;
        switch (quadrant) {
        case QUAD::Quadrant::TopLeft:  return {boundaries.position, {half_size_x, half_size_y}};
        case QUAD::Quadrant::TopRight: return {{center.x, boundaries.position.y}, {half_size_x, half_size_y}};
        case QUAD::Quadrant::BotRight: return {center, {half_size_x, half_size_y}};
        }
        // case QUAD::Quadrant::BotLeft:
        return {{boundaries.position.x, center.y}, {half_size_x, half_size_y}}; 
    }

    __device__ void compute_com_and_count(QUAD &quad, Vector2 *pos, double *mass,
            double *sMass, Vector2 *sCOM, int32_t *count, Vector2 center,
            int32_t start, int32_t end) {
        const int32_t tx = static_cast<int32_t>(threadIdx.x);

        if (tx < 4)
            count[tx] = 0;

        double M = 0.0, Rx = 0.0, Ry = 0.0;
        __syncthreads();

        for (int32_t i = start + tx; i <= end; i += static_cast<int32_t>(blockDim.x)) {
            const double m = mass[i];
            M += m;
            const Vector2 p = pos[i];
            Rx += m * p.x;
            Ry += m * p.y;
            const auto quadrant = get_quadrant(center, p);
            atomicAdd(&count[static_cast<uint8_t>(quadrant)], 1);
        }

        sMass[tx] = M;
        sCOM[tx] = {Rx, Ry};
        __syncthreads();

        // Block-level reduction in shared memory
        for (uint32_t stride = blockDim.x / 2; stride > 32; stride >>= 1) {
            if (static_cast<uint32_t>(tx) < stride) {
                sMass[tx] += sMass[tx + stride];
                sCOM[tx].x += sCOM[tx + stride].x;
                sCOM[tx].y += sCOM[tx + stride].y;
            }
            __syncthreads();
        }

        // Warp-level reduction using shuffle
        if (tx < 32) {
            M  = sMass[tx] + sMass[tx + 32];
            Rx = sCOM[tx].x + sCOM[tx + 32].x;
            Ry = sCOM[tx].y + sCOM[tx + 32].y;

            for (int32_t offset = 16; offset >= 1; offset >>= 1) {
                M  += shfl_down_double(0xFFFFFFFF, M, offset);
                Rx += shfl_down_double(0xFFFFFFFF, Rx, offset);
                Ry += shfl_down_double(0xFFFFFFFF, Ry, offset);
            }
        }

        if (tx == 0) {
            quad.mass = M;
            quad.center_of_mass = {Rx / M, Ry / M};
        }
        __syncthreads();
    }

    __device__ void compute_offset(int32_t *count, int32_t start) {
        const int32_t tx = static_cast<int32_t>(threadIdx.x);
        if (tx < 4) {
            int32_t offset = start;
            for (int32_t i = 0; i < tx; ++i) {
                offset += count[i];
            }
            count[tx + 4] = offset;
        }
        __syncthreads();
    }

    __device__ void group_bodies(const Vector2 *pos, Vector2 *pos_alt, 
            const double *mass, double *mass_alt, const Vector2 *vel, Vector2 *vel_alt,
            const int32_t *idx, int32_t *idx_alt,
            Vector2 center, int32_t *count, int32_t start, int32_t end) {
        int32_t *count2 = &count[4];
        for (int32_t i = start + static_cast<int32_t>(threadIdx.x); i <= end; i += static_cast<int32_t>(blockDim.x)) {
            const auto quadrant = Device::get_quadrant(center, pos[i]);
            const int32_t dest = atomicAdd(&count2[static_cast<uint8_t>(quadrant)], 1);
            pos_alt[dest] = pos[i];
            mass_alt[dest] = mass[i];
            vel_alt[dest] = vel[i];
            idx_alt[dest] = idx[i];
        }
        __syncthreads();
    }

    __device__ void compute_force(const QUAD * __restrict__ quads, const Vector2 *pos, const double *mass,
            int32_t body_idx, int32_t quads_count, double epsilon_sq, double G_dt, double *vel_out) {

        constexpr double THETA_SQ = Constants::Simulation::THETA * Constants::Simulation::THETA;

        int32_t stack[128];
        int32_t top = 0;
        stack[0] = 0;

        const Vector2 body_pos = pos[body_idx];
        double acc_x = 0.0, acc_y = 0.0;

        while (top >= 0) {
            const int32_t qi = stack[top--];

            if (qi >= quads_count) continue;

            const QUAD &quad = quads[qi];
            if (quad.start == -1 && quad.end == -1) continue;

            const Vector2 rij = {quad.center_of_mass.x - body_pos.x,
                                 quad.center_of_mass.y - body_pos.y};
            const double dist_sq = rij.x * rij.x + rij.y * rij.y;

            if (quad.is_leaf) {
                if (dist_sq > 0.0 && quad.mass > 0.0) {
                    const double r_sq_soft = dist_sq + epsilon_sq;
                    const double inv_r = rsqrt(r_sq_soft);
                    const double inv_r3 = inv_r * inv_r * inv_r;
                    const double a = quad.mass * inv_r3;
                    acc_x += rij.x * a;
                    acc_y += rij.y * a;
                }
                continue;
            }

            // Opening angle criterion: (s/d)^2 < theta^2
            if (dist_sq > 0.0 && quad.width_sq < THETA_SQ * dist_sq) {
                const double r_sq_soft = dist_sq + epsilon_sq;
                const double inv_r = rsqrt(r_sq_soft);
                const double inv_r3 = inv_r * inv_r * inv_r;
                const double a = quad.mass * inv_r3;
                acc_x += rij.x * a;
                acc_y += rij.y * a;
                continue;
            }

            // Push 4 children onto stack
            const int32_t base = qi * 4;
            stack[++top] = base + 4;
            stack[++top] = base + 3;
            stack[++top] = base + 2;
            stack[++top] = base + 1;
        }

        vel_out[0] = acc_x * G_dt;
        vel_out[1] = acc_y * G_dt;
    }
}

namespace Kernel {
    __global__ void compute_bounding_box(const Vector2 * __restrict__ pos, uint32_t n, BarnesHutCuda::Box *global_bbox) {
        extern __shared__ double sdata[];
    
        double *sxmin = sdata;
        double *sxmax = sdata + blockDim.x;
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

    __global__ void build_quad_tree(QUAD *quads, Vector2 *pos, Vector2 *pos_alt, 
            double *mass, double *mass_alt, Vector2 *vel, Vector2 *vel_alt,
            int32_t *idx, int32_t *idx_alt,
            const int32_t *work_list, int32_t *work_list_next, int32_t *work_count_next,
            int32_t quads_count, int32_t leaf_limit) {
        __shared__ int32_t count[8];
        __shared__ double totalMass[BLOCK_SIZE];
        __shared__ Vector2 centerMass[BLOCK_SIZE];

        const int32_t tx = static_cast<int32_t>(threadIdx.x);
        const int32_t quad_idx = work_list[blockIdx.x];

        QUAD &quad = quads[quad_idx];
        const int32_t start = quad.start, end = quad.end;

        Vector2 center;
        center.x = quad.boundaries.position.x + quad.boundaries.size.x / 2.0;
        center.y = quad.boundaries.position.y + quad.boundaries.size.y / 2.0;

        Device::compute_com_and_count(quad, pos, mass, totalMass, centerMass, count, center, start, end);

        if (quad_idx >= leaf_limit || start == end) {
            for (int32_t i = start + tx; i <= end; i += static_cast<int32_t>(blockDim.x)) {
                pos_alt[i] = pos[i];
                mass_alt[i] = mass[i];
                vel_alt[i] = vel[i];
                idx_alt[i] = idx[i];
            }
            return;
        }

        Device::compute_offset(count, start);
        Device::group_bodies(pos, pos_alt, mass, mass_alt, vel, vel_alt, idx, idx_alt, center, count, start, end);

        if (tx == 0) {
            const int32_t base = quad_idx * 4;
            quad.is_leaf = false;
            const double child_width_sq = quad.width_sq * 0.25;

            if (count[0] > 0) {
                QUAD &child = quads[base + 1];
                child.boundaries = Device::get_child_boundraries(quad.boundaries, center, QUAD::Quadrant::TopLeft);
                child.start = start;
                child.end = start + count[0] - 1;
                child.width_sq = child_width_sq;
                work_list_next[atomicAdd(work_count_next, 1)] = base + 1;
            }

            if (count[1] > 0) {
                QUAD &child = quads[base + 2];
                child.boundaries = Device::get_child_boundraries(quad.boundaries, center, QUAD::Quadrant::TopRight);
                child.start = start + count[0];
                child.end = start + count[0] + count[1] - 1;
                child.width_sq = child_width_sq;
                work_list_next[atomicAdd(work_count_next, 1)] = base + 2;
            }

            if (count[2] > 0) {
                QUAD &child = quads[base + 3];
                child.boundaries = Device::get_child_boundraries(quad.boundaries, center, QUAD::Quadrant::BotRight);
                child.start = start + count[0] + count[1];
                child.end = start + count[0] + count[1] + count[2] - 1;
                child.width_sq = child_width_sq;
                work_list_next[atomicAdd(work_count_next, 1)] = base + 3;
            }

            if (count[3] > 0) {
                QUAD &child = quads[base + 4];
                child.boundaries = Device::get_child_boundraries(quad.boundaries, center, QUAD::Quadrant::BotLeft);
                child.start = start + count[0] + count[1] + count[2];
                child.end = end;
                child.width_sq = child_width_sq;
                work_list_next[atomicAdd(work_count_next, 1)] = base + 4;
            }
        }
    }

    __global__ void compute_force(const QUAD * __restrict__ quads, const Vector2 * __restrict__ pos, 
            const double * __restrict__ mass, Vector2 *vel, 
            int32_t quads_count, int32_t n, double epsilon_sq, double G_dt) {
        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);

        if (i < n) {
            double dv[2];
            Device::compute_force(quads, pos, mass, i, quads_count, epsilon_sq, G_dt, dv);
            vel[i].x += dv[0];
            vel[i].y += dv[1];
        }
    }

    __global__ void update_positions(Vector2 *pos, Vector2 *vel, int32_t n, double dt) {
        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i < n) {
            pos[i].x += vel[i].x * dt;
            pos[i].y += vel[i].y * dt;
        }
    }

    __global__ void init_quads(QUAD *quads, int32_t count) {
        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i < count) {
            quads[i].start = -1;
            quads[i].end = -1;
            quads[i].is_leaf = true;
            quads[i].mass = 0;
            quads[i].center_of_mass = {0.0, 0.0};
            quads[i].width_sq = 0.0;
        }
    }

    __global__ void init_indices(int32_t *idx, int32_t n) {
        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i < n) {
            idx[i] = i;
        }
    }

    __global__ void scatter_to_original_order(const Vector2 *sorted_pos, const Vector2 *sorted_vel,
            const int32_t *idx, Vector2 *out_pos, Vector2 *out_vel, int32_t n) {
        const int32_t i = static_cast<int32_t>(blockIdx.x * blockDim.x + threadIdx.x);
        if (i < n) {
            const int32_t orig = idx[i];
            out_pos[orig] = sorted_pos[i];
            out_vel[orig] = sorted_vel[i];
        }
    }
    
}


void BarnesHutCuda::init_device_resources() {
    const auto mass_bytes = sizeof(bodies.mass(0)) * bodies.n;
    const auto pos_bytes = sizeof(bodies.pos(0)) * bodies.n;
    const auto vel_bytes = sizeof(bodies.vel(0)) * bodies.n;
    const auto idx_bytes = sizeof(int32_t) * bodies.n;
    const auto quad_bytes = sizeof(QUAD) * max_quads;
    const int32_t work_list_size = static_cast<int32_t>(bodies.n) + 1;

    CUDA_CHECK(cudaMalloc(&mass_d, mass_bytes));
    CUDA_CHECK(cudaMalloc(&mass_alt_d, mass_bytes));
    CUDA_CHECK(cudaMalloc(&pos_d, pos_bytes));
    CUDA_CHECK(cudaMalloc(&pos_alt_d, pos_bytes));
    CUDA_CHECK(cudaMalloc(&vel_d, vel_bytes));
    CUDA_CHECK(cudaMalloc(&vel_alt_d, vel_bytes));
    CUDA_CHECK(cudaMalloc(&idx_d, idx_bytes));
    CUDA_CHECK(cudaMalloc(&idx_alt_d, idx_bytes));
    CUDA_CHECK(cudaMalloc(&bounding_box_d, sizeof(Box)));
    CUDA_CHECK(cudaMalloc(&quads_d, quad_bytes));
    CUDA_CHECK(cudaMalloc(&work_list_d, sizeof(int32_t) * work_list_size));
    CUDA_CHECK(cudaMalloc(&work_list_next_d, sizeof(int32_t) * work_list_size));
    CUDA_CHECK(cudaMalloc(&work_count_d, sizeof(int32_t)));

    CUDA_CHECK(cudaMemcpy(mass_d, bodies.mass_data(), mass_bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(pos_d, bodies.pos_data(), pos_bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(vel_d, bodies.vel_data(), vel_bytes, cudaMemcpyHostToDevice));

    // Initialize index array to identity mapping once; it will be
    // carried through every tree build to track original body order.
    constexpr uint32_t block_size = BLOCK_SIZE;
    const uint32_t grid_size = (bodies.n + block_size - 1) / block_size;
    Kernel::init_indices<<<grid_size, block_size>>>(idx_d, static_cast<int32_t>(bodies.n));
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
    CUDA_CHECK(cudaFree(bounding_box_d));
    CUDA_CHECK(cudaFree(quads_d));
    CUDA_CHECK(cudaFree(work_list_d));
    CUDA_CHECK(cudaFree(work_list_next_d));
    CUDA_CHECK(cudaFree(work_count_d));
}

void BarnesHutCuda::sync_from_gpu_bodies() {
    // Scatter sorted GPU data back to original body order using pos_alt_d/vel_alt_d as temp
    constexpr uint32_t block_size = 256;
    const uint32_t grid_size = (bodies.n + block_size - 1) / block_size;
    Kernel::scatter_to_original_order<<<grid_size, block_size>>>(
        pos_d, vel_d, idx_d, pos_alt_d, vel_alt_d, static_cast<int32_t>(bodies.n)
    );
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    const auto pos_bytes = sizeof(bodies.pos(0)) * bodies.n;
    const auto vel_bytes = sizeof(bodies.vel(0)) * bodies.n;

    CUDA_CHECK(cudaMemcpy(bodies.pos_data(), pos_alt_d, pos_bytes, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(bodies.vel_data(), vel_alt_d, vel_bytes, cudaMemcpyDeviceToHost));
}

void BarnesHutCuda::compute_bounding_box() {
    bounding_box = Box{
        .x_min = std::numeric_limits<double>::max(),
        .x_max = -std::numeric_limits<double>::max(),
        .y_min = std::numeric_limits<double>::max(),
        .y_max = -std::numeric_limits<double>::max(),
    };

    CUDA_CHECK(cudaMemcpy(bounding_box_d, &bounding_box, sizeof(Box), cudaMemcpyHostToDevice));

    constexpr uint32_t block_size = BLOCK_SIZE;
    const uint32_t grid_size = (bodies.n + block_size - 1) / block_size;
    constexpr uint32_t shared_mem_size = 4 * block_size * sizeof(double);

    Kernel::compute_bounding_box<<<grid_size, block_size, shared_mem_size>>>(
        pos_d, static_cast<uint32_t>(bodies.n), bounding_box_d
    );
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(&bounding_box, bounding_box_d, sizeof(Box), cudaMemcpyDeviceToHost));

    // Square the bounding box — required because the force computation
    // tracks cell width by halving at each tree level.
    const double x_range = bounding_box.x_max - bounding_box.x_min;
    const double y_range = bounding_box.y_max - bounding_box.y_min;
    if (x_range > y_range) {
        const double mid_y = (bounding_box.y_min + bounding_box.y_max) * 0.5;
        bounding_box.y_min = mid_y - x_range * 0.5;
        bounding_box.y_max = mid_y + x_range * 0.5;
    } else {
        const double mid_x = (bounding_box.x_min + bounding_box.x_max) * 0.5;
        bounding_box.x_min = mid_x - y_range * 0.5;
        bounding_box.x_max = mid_x + y_range * 0.5;
    }
}

void BarnesHutCuda::build_quad_tree() {
    constexpr uint32_t block_size = BLOCK_SIZE;
    const int32_t leaf_limit = max_quads / 4 - 1;

    // Initialize all quads to default (sentinel) state
    const uint32_t init_grid = (max_quads + block_size - 1) / block_size;
    Kernel::init_quads<<<init_grid, block_size>>>(quads_d, max_quads);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    const QUAD root {
        .boundaries = {
            .position = {bounding_box.x_min, bounding_box.y_min},
            .size = {bounding_box.x_max - bounding_box.x_min, bounding_box.y_max - bounding_box.y_min}
        },
        .mass = 0.0,
        .center_of_mass = {0.0, 0.0},
        .start = 0,
        .end = static_cast<int32_t>(bodies.n) - 1,
        .is_leaf = true,
        .width_sq = root.boundaries.size.x * root.boundaries.size.x
    };
    CUDA_CHECK(cudaMemcpy(quads_d, &root, sizeof(QUAD), cudaMemcpyHostToDevice));

    // Initialize work list with root quad
    int32_t root_idx = 0;
    CUDA_CHECK(cudaMemcpy(work_list_d, &root_idx, sizeof(int32_t), cudaMemcpyHostToDevice));
    int32_t work_count = 1;

    // Build tree level-by-level using work lists of occupied quads.
    // Each level reads from cur_* and writes sorted data to cur_*_alt,
    // then we swap the buffer pointers for the next level.
    Vector2 *cur_pos = pos_d, *cur_pos_alt = pos_alt_d;
    double  *cur_mass = mass_d, *cur_mass_alt = mass_alt_d;
    Vector2 *cur_vel = vel_d, *cur_vel_alt = vel_alt_d;
    int32_t *cur_idx = idx_d, *cur_idx_alt = idx_alt_d;
    int32_t *cur_work = work_list_d, *cur_work_next = work_list_next_d;

    // TODO: need to define a max level based on max_quads
    for (int32_t level = 0; level < 20; level++) {
        if (work_count == 0)
            break;

        // Reset next work count
        CUDA_CHECK(cudaMemset(work_count_d, 0, sizeof(int32_t)));

        Kernel::build_quad_tree<<<work_count, block_size>>>(
            quads_d, cur_pos, cur_pos_alt, cur_mass, cur_mass_alt,
            cur_vel, cur_vel_alt, cur_idx, cur_idx_alt,
            cur_work, cur_work_next, work_count_d,
            max_quads, leaf_limit
        );
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());

        // Read next level's work count
        CUDA_CHECK(cudaMemcpy(&work_count, work_count_d, sizeof(int32_t), cudaMemcpyDeviceToHost));

        // Swap buffers for next level
        std::swap(cur_pos, cur_pos_alt);
        std::swap(cur_mass, cur_mass_alt);
        std::swap(cur_vel, cur_vel_alt);
        std::swap(cur_idx, cur_idx_alt);
        std::swap(cur_work, cur_work_next);
    }

    // Ensure the primary buffers (pos_d, mass_d, vel_d, idx_d) hold the final data.
    if (cur_pos != pos_d) {
        const auto pos_bytes  = sizeof(Vector2) * bodies.n;
        const auto mass_bytes = sizeof(double) * bodies.n;
        const auto vel_bytes  = sizeof(Vector2) * bodies.n;
        const auto idx_bytes  = sizeof(int32_t) * bodies.n;
        CUDA_CHECK(cudaMemcpy(pos_d, cur_pos, pos_bytes, cudaMemcpyDeviceToDevice));
        CUDA_CHECK(cudaMemcpy(mass_d, cur_mass, mass_bytes, cudaMemcpyDeviceToDevice));
        CUDA_CHECK(cudaMemcpy(vel_d, cur_vel, vel_bytes, cudaMemcpyDeviceToDevice));
        CUDA_CHECK(cudaMemcpy(idx_d, cur_idx, idx_bytes, cudaMemcpyDeviceToDevice));
    }
}

void BarnesHutCuda::compute_forces() {
    constexpr uint32_t block_size = 256;
    const uint32_t grid_size = (bodies.n + block_size - 1) / block_size;
    const double G_dt = Constants::Simulation::G * timestep;
    Kernel::compute_force<<<grid_size, block_size>>>(quads_d, pos_d, mass_d, vel_d, max_quads, static_cast<int32_t>(bodies.n), epsilon_squared, G_dt);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
}

void BarnesHutCuda::update_positions() {
    constexpr uint32_t block_size = 256;
    const uint32_t grid_size = (bodies.n + block_size - 1) / block_size;
    Kernel::update_positions<<<grid_size, block_size>>>(pos_d, vel_d, static_cast<int32_t>(bodies.n), timestep);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
}
