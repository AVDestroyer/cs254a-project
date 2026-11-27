#ifndef QUANTUM_GATE_COMMON_H
#define QUANTUM_GATE_COMMON_H

#include <stdint.h>
#include "../../../simulator/state.h"

struct kernel_arg_t {
    uint64_t state_addr;
    uint32_t pair_count;
    uint32_t target_qubit;
};

#endif
