#ifndef QUANTUM_SIMULATOR_GPU_TUNING_H
#define QUANTUM_SIMULATOR_GPU_TUNING_H

#include "vortex_runner.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

struct gpu_tuning_t {
    uint32_t block = 0;
    uint32_t work_per_thread = 1;
    uint32_t reduction_ctas = 0;
    uint32_t reduction_work_per_thread = 0;
    submission_mode_t submission = submission_mode_t::individual;
};

inline bool parse_gpu_tuning_option(const std::string& option, gpu_tuning_t& tuning) {
    if (option.rfind("-b", 0) == 0) {
        tuning.block = parse_uint32(option.substr(2));
    } else if (option.rfind("-w", 0) == 0) {
        tuning.work_per_thread = parse_uint32(option.substr(2));
    } else if (option.rfind("-r", 0) == 0) {
        tuning.reduction_ctas = parse_uint32(option.substr(2));
    } else if (option.rfind("-p", 0) == 0) {
        tuning.reduction_work_per_thread = parse_uint32(option.substr(2));
    } else if (option.rfind("-s", 0) == 0) {
        tuning.submission = parse_submission_mode(option.substr(2));
    } else {
        return false;
    }
    return true;
}

inline uint32_t select_block(const gpu_tuning_t& tuning,
                             const vortex_hardware_t& hardware, uint32_t tasks) {
    const uint64_t capacity = hardware.warps_per_core * hardware.threads_per_warp;
    const uint32_t block = tuning.block == 0u
        ? static_cast<uint32_t>(std::min<uint64_t>(capacity, tasks))
        : tuning.block;
    if (block == 0u || block > capacity || (block & (block - 1u)) != 0u) {
        throw std::invalid_argument("block must be a power of two within one core's thread capacity");
    }
    if (tuning.work_per_thread == 0u || tuning.work_per_thread > 64u) {
        throw std::invalid_argument("work per thread must be between 1 and 64");
    }
    return block;
}

inline uint32_t work_grid(uint32_t tasks, uint32_t block, uint32_t work) {
    return static_cast<uint32_t>((static_cast<uint64_t>(tasks) +
                                  static_cast<uint64_t>(block) * work - 1u) /
                                 (static_cast<uint64_t>(block) * work));
}

#endif
