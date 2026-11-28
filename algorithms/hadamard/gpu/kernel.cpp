#include <vx_spawn2.h>

#include "common.h"

// Each thread updates disjoint pairs, with grid size controlling work per thread.
__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto state = reinterpret_cast<complex_t*>(arg->state_addr);
    const uint32_t target = arg->target_qubit;
    const uint32_t pair_distance = 1u << target;
    constexpr float inverse_sqrt_two = 0.7071067811865475f;
    const uint32_t stride = gridDim.x * blockDim.x;
    for (uint32_t pair = blockIdx.x * blockDim.x + threadIdx.x;
         pair < arg->pair_count; pair += stride) {
        const uint32_t low_bits = pair & (pair_distance - 1u);
        const uint32_t high_bits = pair >> target;
        const uint32_t zero_index = (high_bits << (target + 1u)) | low_bits;
        const uint32_t one_index = zero_index | pair_distance;
        const complex_t zero = state[zero_index];
        const complex_t one = state[one_index];
        state[zero_index] = {
            (zero.real + one.real) * inverse_sqrt_two,
            (zero.imag + one.imag) * inverse_sqrt_two,
        };
        state[one_index] = {
            (zero.real - one.real) * inverse_sqrt_two,
            (zero.imag - one.imag) * inverse_sqrt_two,
        };
    }
}
