#include "Simulation/BarnesHutCuda.hpp"

#include <cuda_runtime.h>

#include "Logger/Logger.hpp"

namespace Device {
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

    __device__ __host__ inline uint32_t expandBits(uint32_t v){
        v = (v * 0x00010001u) & 0xFF0000FFu;
        v = (v * 0x00000101u) & 0x0F00F00Fu;
        v = (v * 0x00000011u) & 0xC30C30C3u;
        v = (v * 0x00000005u) & 0x49249249u;
        return v;
    }

    __device__ __host__ inline uint32_t morton2D(sf::Vector2<double> pos, double x_min, double y_min, double inv_extent) {
        const double xn = (pos.x - x_min) * inv_extent;
        const double yn = (pos.y - y_min) * inv_extent;
        const uint32_t xi = static_cast<uint32_t>(xn * 65535.0);
        const uint32_t yi = static_cast<uint32_t>(yn * 65535.0);
        return (expandBits(xi) << 1) | expandBits(yi);
    }
}

namespace Kernel {
    __global__ void compute_bounding_box(const sf::Vector2<double> * __restrict__ pos, uint32_t n, BarnesHutCuda::Box *global_bbox) {
        extern __shared__ double sdata[];
    
        double *sxmin = sdata;
        double *sxmax = sdata + blockDim.x;
        double *symin = sdata + 2 * blockDim.x;
        double *symax = sdata + 3 * blockDim.x;
    
        const uint32_t tid = threadIdx.x;
        const uint32_t idx = blockIdx.x * blockDim.x + tid;
    
        // Initialize local values
        const double x = (idx < n) ? pos[idx].x :  INFINITY;
        const double y = (idx < n) ? pos[idx].y :  INFINITY;
    
        sxmin[tid] = x;
        sxmax[tid] = (idx < n) ? x : -INFINITY;
        symin[tid] = y;
        symax[tid] = (idx < n) ? y : -INFINITY;
    
        __syncthreads();
    
        // Parallel reduction
        for (uint32_t stride = blockDim.x / 2; stride > 0; stride >>= 1) {
            if (tid < stride) {
                sxmin[tid] = fmin(sxmin[tid], sxmin[tid + stride]);
                sxmax[tid] = fmax(sxmax[tid], sxmax[tid + stride]);
                symin[tid] = fmin(symin[tid], symin[tid + stride]);
                symax[tid] = fmax(symax[tid], symax[tid + stride]);
            }
            __syncthreads();
        }
    
        // One atomic update per block
        if (tid == 0) {
            Device::atomic_min(&global_bbox->x_min, sxmin[0]);
            Device::atomic_max(&global_bbox->x_max, sxmax[0]);
            Device::atomic_min(&global_bbox->y_min, symin[0]);
            Device::atomic_max(&global_bbox->y_max, symax[0]);
        }
    }
    
    __global__ void compute_morton_codes(const sf::Vector2<double> * __restrict__ pos, 
            uint32_t * __restrict__ morton, uint32_t n, double x_min, double y_min, 
            double inv_extent) {
        const uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
        if (idx < n) {
            morton[idx] = Device::morton2D(pos[idx], x_min, y_min, inv_extent);
        }
    }
}




void BarnesHutCuda::init_device_resources() {
    const auto mass_bytes = sizeof(bodies.mass(0)) * bodies.n;
    const auto pos_bytes = sizeof(bodies.pos(0)) * bodies.n;
    const auto vel_bytes = sizeof(bodies.vel(0)) * bodies.n;
    const auto morton_bytes = sizeof(*morton_d) * bodies.n;

    cudaMalloc(&mass_d, mass_bytes);
    cudaMalloc(&pos_d, pos_bytes);
    cudaMalloc(&vel_d, vel_bytes);
    cudaMalloc(&bounding_box_d, sizeof(Box));
    cudaMalloc(&morton_d, morton_bytes);

    cudaMemcpy(mass_d, bodies.mass_data(), mass_bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(pos_d, bodies.pos_data(), pos_bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(vel_d, bodies.vel_data(), vel_bytes, cudaMemcpyHostToDevice);
}

void BarnesHutCuda::release_device_resources() {
    cudaFree(mass_d);
    cudaFree(pos_d);
    cudaFree(vel_d);
    cudaFree(bounding_box_d);
}

void BarnesHutCuda::sync_from_gpu_bodies() {
    const auto pos_bytes = sizeof(bodies.pos(0)) * bodies.n;
    const auto vel_bytes = sizeof(bodies.vel(0)) * bodies.n;

    // todo: make transfer async 
    cudaMemcpy(bodies.pos_data(), pos_d, pos_bytes, cudaMemcpyDeviceToHost);
    cudaMemcpy(bodies.vel_data(), vel_d, vel_bytes, cudaMemcpyDeviceToHost);
}

void BarnesHutCuda::compute_bounding_box() {
    bounding_box = Box{
        .x_min = std::numeric_limits<double>::max(),
        .x_max = std::numeric_limits<double>::min(),
        .y_min = std::numeric_limits<double>::max(),
        .y_max = std::numeric_limits<double>::min(),
    };
    
    StopWatch sw;
    cudaMemcpy(bounding_box_d, &bounding_box, sizeof(Box), cudaMemcpyHostToDevice);

    constexpr uint32_t block_size = 256;
    const uint32_t grid_size = (bodies.n + block_size - 1) / block_size;
    constexpr uint32_t shared_mem_size = 4 * block_size * sizeof(double);

    Kernel::compute_bounding_box<<<grid_size, block_size, shared_mem_size>>>(
        pos_d, bodies.n, bounding_box_d
    );

    cudaDeviceSynchronize();
    cudaMemcpy(&bounding_box, bounding_box_d, sizeof(Box), cudaMemcpyDeviceToHost);
    inv_extent = 1.0 / std::max(bounding_box.x_max - bounding_box.x_min, 
            bounding_box.y_max - bounding_box.y_min);
    
    Log::debug("{}", sw);
}

void BarnesHutCuda::compute_morton_codes() {
    constexpr uint32_t block_size = 256;
    const uint32_t grid_size = (bodies.n + block_size - 1) / block_size;
    Kernel::compute_morton_codes<<<grid_size, block_size>>>(
        pos_d,
        morton_d,
        bodies.n,
        bounding_box.x_min, bounding_box.y_min,
        inv_extent
    );
}

void BarnesHutCuda::sort_by_morton_code() {
    
}
