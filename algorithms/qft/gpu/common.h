#ifndef QFT_GPU_COMMON_H
#define QFT_GPU_COMMON_H

#include "../../../simulator/state.h"

#include <stdint.h>

enum qft_op_t : uint32_t {
    QFT_HADAMARD = 0,
    QFT_CONTROLLED_PHASE = 1,
    QFT_SWAP = 2,
};

struct kernel_arg_t {
    uint64_t state_addr;
    uint32_t state_size;
    uint32_t pair_count;
    uint32_t target_qubit;
    uint32_t other_qubit;
    float cosine;
    float sine;
    uint32_t op_type;
};

#endif
