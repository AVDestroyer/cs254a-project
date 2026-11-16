#ifndef GROVER_COMMON_H
#define GROVER_COMMON_H

#include <stdint.h>

enum gate_op_t : uint32_t {
    OP_HADAMARD = 0,
    OP_PAULI_X  = 1,
    OP_PHASE_FLIP = 2, // Flips sign of state[target_index]
};

struct complex_t {
    float real;
    float imag;
};

struct kernel_arg_t {
    uint64_t state_addr;
    uint32_t pair_count;
    uint32_t target_qubit;
    uint32_t op_type;
    uint32_t target_index;
};

#endif