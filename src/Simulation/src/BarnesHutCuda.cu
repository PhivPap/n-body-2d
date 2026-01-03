#include "Simulation/BarnesHutCuda.cuh"

#include <cuda_runtime.h>


struct BoundingBox {
    double x_min;
    double x_max;
    double y_min;
    double y_max;
};

__device__ double atomicMinDouble(double* addr, double value) {
    unsigned long long* addr_as_ull =
        (unsigned long long*)addr;

    unsigned long long old = *addr_as_ull;
    unsigned long long assumed;

    do {
        assumed = old;
        double old_val = __longlong_as_double(assumed);
        if (old_val <= value)
            break;

        old = atomicCAS(
            addr_as_ull,
            assumed,
            __double_as_longlong(value)
        );
    } while (assumed != old);

    return __longlong_as_double(old);
}

__device__ double atomicMaxDouble(double* addr, double value) {
    unsigned long long* addr_as_ull =
        (unsigned long long*)addr;

    unsigned long long old = *addr_as_ull;
    unsigned long long assumed;

    do {
        assumed = old;
        double old_val = __longlong_as_double(assumed);
        if (old_val >= value)
            break;

        old = atomicCAS(
            addr_as_ull,
            assumed,
            __double_as_longlong(value)
        );
    } while (assumed != old);

    return __longlong_as_double(old);
}


__global__ void computeBoundingBoxKernel(const double* __restrict__ pos_x, const double* __restrict__ pos_y, int N, BoundingBox* global_bbox) {
    extern __shared__ double sdata[];

    double* sxmin = sdata;
    double* sxmax = sdata + blockDim.x;
    double* symin = sdata + 2 * blockDim.x;
    double* symax = sdata + 3 * blockDim.x;

    int tid = threadIdx.x;
    int idx = blockIdx.x * blockDim.x + tid;

    // Initialize local values
    double x = (idx < N) ? pos_x[idx] :  INFINITY;
    double y = (idx < N) ? pos_y[idx] :  INFINITY;

    sxmin[tid] = x;
    sxmax[tid] = (idx < N) ? x : -INFINITY;
    symin[tid] = y;
    symax[tid] = (idx < N) ? y : -INFINITY;

    __syncthreads();

    // Parallel reduction
    for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
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
        atomicMinDouble(&global_bbox->x_min, sxmin[0]);
        atomicMaxDouble(&global_bbox->x_max, sxmax[0]);
        atomicMinDouble(&global_bbox->y_min, symin[0]);
        atomicMaxDouble(&global_bbox->y_max, symax[0]);
    }
}

void computeBoundingBox(int N, const Bodies *bodies) {
    // BoundingBox h_bbox = {
    //     +1e+50, -1e+50,
    //     +1e+50, -1e+50
    // };

    // BoundingBox* d_bbox;
    // cudaMalloc(&d_bbox, sizeof(BoundingBox));
    // cudaMemcpy(d_bbox, &h_bbox, sizeof(BoundingBox), cudaMemcpyHostToDevice);

    // double *pos_x;
    // double *pos_y;
    // cudaMalloc(&pos_x, sizeof(double) * N);
    // cudaMalloc(&pos_y, sizeof(double) * N);
    // for (int i = 0; i < N; i++) {
    //     cudaMemcpy(pos_x + i, &(bodies[i].pos.x), sizeof(double), cudaMemcpyHostToDevice);
    //     cudaMemcpy(pos_y + i, &(bodies[i].pos.y), sizeof(double), cudaMemcpyHostToDevice);
    // }

    // int blockSize = 256;
    // int gridSize  = (N + blockSize - 1) / blockSize;

    // size_t sharedMemSize = 4 * blockSize * sizeof(double);

    // StopWatch sw;
    // computeBoundingBoxKernel<<<gridSize, blockSize, sharedMemSize>>>(
    //     pos_x, pos_y, N, d_bbox
    // );
    // Log::error("{}", sw);

    // // cudaDeviceSynchronize();
    // cudaMemcpy(&h_bbox, d_bbox, sizeof(BoundingBox), cudaMemcpyDeviceToHost);
    // Log::error("min_x: {}, max_x: {}, min_y: {}, max_y: {}", h_bbox.x_min, h_bbox.x_max, h_bbox.y_min, h_bbox.y_max);

    // cudaFree(pos_x);
    // cudaFree(pos_y);
}
