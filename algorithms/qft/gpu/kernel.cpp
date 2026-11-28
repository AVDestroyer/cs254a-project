#include <vx_spawn2.h>

#include "common.h"

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto state = reinterpret_cast<complex_t*>(arg->state_addr);
    const uint32_t first = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t stride = gridDim.x * blockDim.x;

    if (arg->op_type == QFT_HADAMARD) {
        const uint32_t distance = 1u << arg->target_qubit;
        constexpr float inverse_sqrt_two = 0.7071067811865475f;
        for (uint32_t index = first; index < arg->pair_count; index += stride) {
            const uint32_t low = index & (distance - 1u);
            const uint32_t high = index >> arg->target_qubit;
            const uint32_t zero_index = (high << (arg->target_qubit + 1u)) | low;
            const uint32_t one_index = zero_index | distance;
            const complex_t zero = state[zero_index];
            const complex_t one = state[one_index];
            state[zero_index] = {(zero.real + one.real) * inverse_sqrt_two,
                                 (zero.imag + one.imag) * inverse_sqrt_two};
            state[one_index] = {(zero.real - one.real) * inverse_sqrt_two,
                                (zero.imag - one.imag) * inverse_sqrt_two};
        }
    } else if (arg->op_type == QFT_CONTROLLED_PHASE) {
        const uint32_t mask = (1u << arg->target_qubit) |
                              (1u << arg->other_qubit);
        for (uint32_t index = first; index < arg->state_size; index += stride) {
            if ((index & mask) == mask) {
                const complex_t value = state[index];
                state[index] = {
                    value.real * arg->cosine - value.imag * arg->sine,
                    value.real * arg->sine + value.imag * arg->cosine,
                };
            }
        }
    } else if (arg->op_type == QFT_SWAP) {
        const uint32_t first_mask = 1u << arg->target_qubit;
        const uint32_t second_mask = 1u << arg->other_qubit;
        for (uint32_t index = first; index < arg->state_size; index += stride) {
            if ((index & first_mask) != 0u && (index & second_mask) == 0u) {
                const uint32_t partner = index ^ first_mask ^ second_mask;
                const complex_t value = state[index];
                state[index] = state[partner];
                state[partner] = value;
            }
        }
    }
}
