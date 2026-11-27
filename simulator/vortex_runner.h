#ifndef QUANTUM_SIMULATOR_VORTEX_RUNNER_H
#define QUANTUM_SIMULATOR_VORTEX_RUNNER_H

#include "benchmark.h"

#include <VX_types.h>
#include <vortex2.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

inline void vortex_check(vx_result_t result, const char* operation) {
    if (result != VX_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 vx_result_string(result));
    }
}

struct vortex_hardware_t {
    uint64_t cores = 0;
    uint64_t warps_per_core = 0;
    uint64_t threads_per_warp = 0;
    uint64_t clock_mhz = 0;
};

struct gpu_result_t {
    state_t state;
    double sim_wall_ms = 0.0;
    uint64_t device_cycles = 0;
    uint32_t launches = 0;
    uint64_t clock_mhz = 0;
};

class vortex_runner_t {
public:
    explicit vortex_runner_t(const state_t& initial) : state_size_(initial.size()) {
        if (initial.empty()) {
            throw std::invalid_argument("initial state must be nonempty");
        }
        try {
            vortex_check(vx_device_open(0, &device_), "vx_device_open");
            vortex_check(vx_device_query(device_, VX_CAPS_NUM_CORES,
                                         &hardware_.cores), "query cores");
            vortex_check(vx_device_query(device_, VX_CAPS_NUM_WARPS,
                                         &hardware_.warps_per_core), "query warps");
            vortex_check(vx_device_query(device_, VX_CAPS_NUM_THREADS,
                                         &hardware_.threads_per_warp), "query threads");
            vortex_check(vx_device_query(device_, VX_CAPS_CLOCK_RATE,
                                         &hardware_.clock_mhz), "query clock");
            if (hardware_.cores == 0u || hardware_.warps_per_core == 0u ||
                hardware_.threads_per_warp == 0u || hardware_.clock_mhz == 0u) {
                throw std::runtime_error("Vortex reported invalid hardware configuration");
            }

            vx_queue_info_t queue_info{};
            queue_info.struct_size = sizeof(queue_info);
            queue_info.priority = VX_QUEUE_PRIORITY_NORMAL;
            vortex_check(vx_queue_create(device_, &queue_info, &queue_),
                         "vx_queue_create");

            state_buffer_ = create_buffer(initial.size() * sizeof(complex_t));
            state_address_ = buffer_address(state_buffer_);
            vortex_check(vx_module_load_file(device_, "kernel.vxbin", &module_),
                         "vx_module_load_file");
            vortex_check(vx_module_get_kernel(module_, "main", &kernel_),
                         "vx_module_get_kernel");
            vortex_check(vx_enqueue_write(queue_, state_buffer_, 0, initial.data(),
                                          initial.size() * sizeof(complex_t),
                                          0, nullptr, nullptr), "vx_enqueue_write");
            vortex_check(vx_queue_finish(queue_, VX_TIMEOUT_INFINITE),
                         "vx_queue_finish after upload");
            cycles_before_ = max_core_cycles();
        } catch (...) {
            cleanup();
            throw;
        }
    }

    vortex_runner_t(const vortex_runner_t&) = delete;
    vortex_runner_t& operator=(const vortex_runner_t&) = delete;

    ~vortex_runner_t() { cleanup(); }

    const vortex_hardware_t& hardware() const { return hardware_; }
    uint64_t state_address() const { return state_address_; }

    vx_buffer_h create_buffer(uint64_t bytes) {
        vx_buffer_h buffer = nullptr;
        vortex_check(vx_buffer_create(device_, bytes, VX_MEM_READ_WRITE, &buffer),
                     "vx_buffer_create");
        buffers_.push_back(buffer);
        return buffer;
    }

    uint64_t buffer_address(vx_buffer_h buffer) const {
        uint64_t address = 0;
        vortex_check(vx_buffer_address(buffer, &address), "vx_buffer_address");
        return address;
    }

    template <typename Arg>
    void launch(const Arg& arg, uint32_t grid, uint32_t block) {
        if (finished_ || grid == 0u || block == 0u) {
            throw std::invalid_argument("invalid GPU launch");
        }
        vx_launch_info_t info{};
        info.struct_size = sizeof(info);
        info.kernel = kernel_;
        info.args_host = &arg;
        info.args_size = sizeof(arg);
        info.ndim = 1;
        info.grid_dim[0] = grid;
        info.block_dim[0] = block;
        if (launches_ == 0u) {
            wall_start_ = std::chrono::steady_clock::now();
        }
        vortex_check(vx_enqueue_launch(queue_, &info, 0, nullptr, nullptr),
                     "vx_enqueue_launch");
        ++launches_;
    }

    gpu_result_t finish() {
        if (finished_ || launches_ == 0u) {
            throw std::logic_error("GPU run has no pending launches");
        }
        state_t actual(state_size_);
        vx_event_h read_event = nullptr;
        vortex_check(vx_enqueue_read(queue_, actual.data(), state_buffer_, 0,
                                     actual.size() * sizeof(complex_t), 0, nullptr,
                                     &read_event), "vx_enqueue_read");
        const vx_result_t wait_result =
            vx_event_wait_value(read_event, 1, VX_TIMEOUT_INFINITE);
        vx_event_release(read_event);
        vortex_check(wait_result, "vx_event_wait_value");
        const auto wall_stop = std::chrono::steady_clock::now();
        const uint64_t cycles_after = max_core_cycles();
        if (cycles_after < cycles_before_) {
            throw std::runtime_error("Vortex cycle counter moved backwards");
        }
        finished_ = true;
        return {std::move(actual),
                std::chrono::duration<double, std::milli>(wall_stop - wall_start_).count(),
                cycles_after - cycles_before_, launches_, hardware_.clock_mhz};
    }

    void dump_perf() const {
        vortex_check(vx_device_dump_perf(device_, stdout), "vx_device_dump_perf");
    }

private:
    uint64_t max_core_cycles() const {
        uint64_t maximum = 0;
        for (uint32_t core = 0; core < hardware_.cores; ++core) {
            uint64_t cycles = 0;
            vortex_check(vx_device_mpm_query(device_, VX_DCR_MPM_CLASS_BASE,
                                             VX_CSR_MCYCLE, core, &cycles),
                         "vx_device_mpm_query");
            maximum = std::max(maximum, cycles);
        }
        return maximum;
    }

    void cleanup() noexcept {
        if (queue_ != nullptr) {
            vx_queue_finish(queue_, VX_TIMEOUT_INFINITE);
        }
        if (kernel_ != nullptr) vx_kernel_release(kernel_);
        if (module_ != nullptr) vx_module_release(module_);
        for (vx_buffer_h buffer : buffers_) vx_buffer_release(buffer);
        if (queue_ != nullptr) vx_queue_release(queue_);
        if (device_ != nullptr) vx_device_release(device_);
        kernel_ = nullptr;
        module_ = nullptr;
        queue_ = nullptr;
        device_ = nullptr;
        buffers_.clear();
    }

    std::size_t state_size_ = 0;
    vx_device_h device_ = nullptr;
    vx_queue_h queue_ = nullptr;
    vx_module_h module_ = nullptr;
    vx_kernel_h kernel_ = nullptr;
    vx_buffer_h state_buffer_ = nullptr;
    std::vector<vx_buffer_h> buffers_;
    vortex_hardware_t hardware_;
    uint64_t state_address_ = 0;
    uint64_t cycles_before_ = 0;
    uint32_t launches_ = 0;
    bool finished_ = false;
    std::chrono::steady_clock::time_point wall_start_{};
};

#endif
