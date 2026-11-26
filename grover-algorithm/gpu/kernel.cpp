#include <vx_spawn2.h>

#include "common.h"

// A Grover iteration is an oracle sign flip followed by inversion about the
// mean. The partial and final sums provide a global reduction without atomics.
__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto state = reinterpret_cast<complex_t*>(arg->state_addr);

    if (arg->op_type == OP_PARTIAL_SUM) {
        const uint32_t tid = threadIdx.x;
        const uint32_t first = blockIdx.x * blockDim.x + tid;
        const uint32_t stride = gridDim.x * blockDim.x;
        complex_t sum = {0.0f, 0.0f};

        for (uint32_t pair = first; pair < arg->pair_count; pair += stride) {
            const uint32_t index = pair * 2u;
            const complex_t a = state[index];
            const complex_t b = state[index + 1u];
            const float a_sign = index == arg->target_index ? -1.0f : 1.0f;
            const float b_sign = index + 1u == arg->target_index ? -1.0f : 1.0f;
            sum.real += a_sign * a.real + b_sign * b.real;
            sum.imag += a_sign * a.imag + b_sign * b.imag;
        }

        auto scratch = reinterpret_cast<complex_t*>(__local_mem());
        scratch[tid] = sum;
        for (uint32_t offset = blockDim.x / 2u; offset != 0u; offset /= 2u) {
            __syncthreads();
            if (tid < offset) {
                scratch[tid].real += scratch[tid + offset].real;
                scratch[tid].imag += scratch[tid + offset].imag;
            }
        }
        if (tid == 0u) {
            auto partial = reinterpret_cast<complex_t*>(arg->partial_addr);
            partial[blockIdx.x] = scratch[0];
        }
    } else if (arg->op_type == OP_FINAL_SUM) {
        if (blockIdx.x != 0u || threadIdx.x != 0u) {
            return;
        }
        auto partial = reinterpret_cast<complex_t*>(arg->partial_addr);
        auto mean = reinterpret_cast<complex_t*>(arg->mean_addr);
        complex_t sum = {0.0f, 0.0f};
        for (uint32_t i = 0; i < arg->partial_count; ++i) {
            sum.real += partial[i].real;
            sum.imag += partial[i].imag;
        }
        const float inverse_size = 1.0f / static_cast<float>(arg->state_size);
        mean[0] = {sum.real * inverse_size, sum.imag * inverse_size};
    } else if (arg->op_type == OP_DIFFUSE) {
        const uint32_t pair = blockIdx.x * blockDim.x + threadIdx.x;
        if (pair >= arg->pair_count) {
            return;
        }
        const uint32_t index = pair * 2u;
        auto mean = reinterpret_cast<complex_t*>(arg->mean_addr);
        const complex_t average = mean[0];
        const complex_t a = state[index];
        const complex_t b = state[index + 1u];
        const float a_sign = index == arg->target_index ? -1.0f : 1.0f;
        const float b_sign = index + 1u == arg->target_index ? -1.0f : 1.0f;
        state[index] = {2.0f * average.real - a_sign * a.real,
                        2.0f * average.imag - a_sign * a.imag};
        state[index + 1u] = {2.0f * average.real - b_sign * b.real,
                             2.0f * average.imag - b_sign * b.imag};
    }
}
