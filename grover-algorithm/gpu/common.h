#ifndef GROVER_COMMON_H
#define GROVER_COMMON_H

#include <stdint.h>

enum grover_op_t : uint32_t {
    OP_PARTIAL_SUM = 0,
    OP_FINAL_SUM = 1,
    OP_DIFFUSE = 2,
};

struct complex_t {
    float real;
    float imag;
};

struct kernel_arg_t {
    uint64_t state_addr;
    uint64_t partial_addr;
    uint64_t mean_addr;
    uint32_t state_size;
    uint32_t pair_count;
    uint32_t partial_count;
    uint32_t target_index;
    uint32_t op_type;
};

#endif
