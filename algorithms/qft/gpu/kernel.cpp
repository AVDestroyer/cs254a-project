#include <vx_spawn2.h>

#include "common.h"

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto state = reinterpret_cast<complex_t*>(arg->state_addr);
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;

    if (arg->op_type == QFT_HADAMARD) {
        if (index >= arg->pair_count) return;
        const uint32_t distance = 1u << arg->target_qubit;
        const uint32_t low = index & (distance - 1u);
        const uint32_t high = index >> arg->target_qubit;
        const uint32_t zero_index = (high << (arg->target_qubit + 1u)) | low;
        const uint32_t one_index = zero_index | distance;
        const complex_t zero = state[zero_index];
        const complex_t one = state[one_index];
        constexpr float inverse_sqrt_two = 0.7071067811865475f;
        state[zero_index] = {(zero.real + one.real) * inverse_sqrt_two,
                             (zero.imag + one.imag) * inverse_sqrt_two};
        state[one_index] = {(zero.real - one.real) * inverse_sqrt_two,
                            (zero.imag - one.imag) * inverse_sqrt_two};
    } else if (arg->op_type == QFT_CONTROLLED_PHASE) {
        if (index >= arg->state_size) return;
        const uint32_t mask = (1u << arg->target_qubit) |
                              (1u << arg->other_qubit);
        if ((index & mask) == mask) {
            const complex_t value = state[index];
            state[index] = {
                value.real * arg->cosine - value.imag * arg->sine,
                value.real * arg->sine + value.imag * arg->cosine,
            };
        }
    } else if (arg->op_type == QFT_SWAP) {
        if (index >= arg->state_size) return;
        const uint32_t first_mask = 1u << arg->target_qubit;
        const uint32_t second_mask = 1u << arg->other_qubit;
        if ((index & first_mask) != 0u && (index & second_mask) == 0u) {
            const uint32_t partner = index ^ first_mask ^ second_mask;
            const complex_t value = state[index];
            state[index] = state[partner];
            state[partner] = value;
        }
    }
}
