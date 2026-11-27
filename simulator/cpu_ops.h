#ifndef QUANTUM_SIMULATOR_CPU_OPS_H
#define QUANTUM_SIMULATOR_CPU_OPS_H

#include "state.h"

#include <cmath>
#include <cstdint>
#include <vector>

inline void apply_hadamard_cpu(std::vector<complex_t>& state, uint32_t target) {
    const uint32_t distance = 1u << target;
    const uint32_t block_size = distance * 2u;
    constexpr float inverse_sqrt_two = 0.7071067811865475f;
    for (uint32_t base = 0; base < state.size(); base += block_size) {
        for (uint32_t offset = 0; offset < distance; ++offset) {
            const uint32_t zero_index = base + offset;
            const uint32_t one_index = zero_index + distance;
            const complex_t zero = state[zero_index];
            const complex_t one = state[one_index];
            state[zero_index] = {(zero.real + one.real) * inverse_sqrt_two,
                                 (zero.imag + one.imag) * inverse_sqrt_two};
            state[one_index] = {(zero.real - one.real) * inverse_sqrt_two,
                                (zero.imag - one.imag) * inverse_sqrt_two};
        }
    }
}

inline void apply_controlled_phase_cpu(std::vector<complex_t>& state,
                                       uint32_t control, uint32_t target,
                                       float cosine, float sine) {
    const uint32_t mask = (1u << control) | (1u << target);
    for (uint32_t index = 0; index < state.size(); ++index) {
        if ((index & mask) == mask) {
            const complex_t value = state[index];
            state[index] = {value.real * cosine - value.imag * sine,
                            value.real * sine + value.imag * cosine};
        }
    }
}

inline void apply_swap_cpu(std::vector<complex_t>& state,
                           uint32_t first, uint32_t second) {
    const uint32_t first_mask = 1u << first;
    const uint32_t second_mask = 1u << second;
    for (uint32_t index = 0; index < state.size(); ++index) {
        if ((index & first_mask) != 0u && (index & second_mask) == 0u) {
            const uint32_t partner = index ^ first_mask ^ second_mask;
            const complex_t value = state[index];
            state[index] = state[partner];
            state[partner] = value;
        }
    }
}

#endif
