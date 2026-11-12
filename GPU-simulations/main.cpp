#include <vortex2.h>

#include "common.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define VX_CHECK(operation)                                                   \
    do {                                                                      \
        const vx_result_t result = (operation);                               \
        if (result != VX_SUCCESS) {                                           \
            std::fprintf(stderr, "%s failed: %s\n", #operation,             \
                         vx_result_string(result));                           \
            std::exit(EXIT_FAILURE);                                          \
        }                                                                     \
    } while (0)

namespace {

void apply_hadamard_cpu(std::vector<complex_t>& state, uint32_t target) {
    const std::size_t pair_distance = std::size_t{1} << target;
    const std::size_t block_size = 2 * pair_distance;
    const float inverse_sqrt_two = 1.0f / std::sqrt(2.0f);

    for (std::size_t base = 0; base < state.size(); base += block_size) {
        for (std::size_t offset = 0; offset < pair_distance; ++offset) {
            const std::size_t zero_index = base + offset;
            const std::size_t one_index = zero_index + pair_distance;
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
}

uint32_t parse_qubit_count(int argc, char** argv) {
    if (argc == 1) {
        return 3;
    }
    if (argc != 2 || std::string(argv[1]).rfind("-n", 0) != 0) {
        throw std::invalid_argument("usage: quantum-gate [-nNUM_QUBITS]");
    }

    const uint32_t count = std::stoul(std::string(argv[1]).substr(2));
    if (count == 0 || count >= 32) {
        throw std::invalid_argument("NUM_QUBITS must be between 1 and 31");
    }
    return count;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const uint32_t num_qubits = parse_qubit_count(argc, argv);
        const std::size_t state_size = std::size_t{1} << num_qubits;
        const uint32_t pair_count = uint32_t{1} << (num_qubits - 1u);
        const uint64_t state_bytes = state_size * sizeof(complex_t);

        std::vector<complex_t> initial(state_size, {0.0f, 0.0f});
        initial[0] = {1.0f, 0.0f};
        std::vector<complex_t> expected = initial;
        for (uint32_t target = 0; target < num_qubits; ++target) {
            apply_hadamard_cpu(expected, target);
        }

        vx_device_h device = nullptr;
        VX_CHECK(vx_device_open(0, &device));

        uint64_t core_count = 0;
        uint64_t warps_per_core = 0;
        uint64_t threads_per_warp = 0;
        VX_CHECK(vx_device_query(device, VX_CAPS_NUM_CORES, &core_count));
        VX_CHECK(vx_device_query(device, VX_CAPS_NUM_WARPS,
                                 &warps_per_core));
        VX_CHECK(vx_device_query(device, VX_CAPS_NUM_THREADS,
                                 &threads_per_warp));

        // Use every hardware thread in a core. A Vortex CTA cannot span cores.
        const uint32_t max_threads_per_cta = static_cast<uint32_t>(
            warps_per_core * threads_per_warp);
        const uint32_t threads_per_cta =
            std::min(max_threads_per_cta, pair_count);
        const uint32_t cta_count =
            (pair_count + threads_per_cta - 1u) / threads_per_cta;
        std::cout << "Hardware configuration: " << core_count << " cores x "
                  << warps_per_core << " warps x " << threads_per_warp
                  << " threads\n"
                  << "Launch configuration per gate: " << cta_count
                  << " CTAs x " << threads_per_cta << " threads\n";

        vx_queue_info_t queue_info{};
        queue_info.struct_size = sizeof(queue_info);
        queue_info.priority = VX_QUEUE_PRIORITY_NORMAL;
        vx_queue_h queue = nullptr;
        VX_CHECK(vx_queue_create(device, &queue_info, &queue));

        vx_buffer_h state_buffer = nullptr;
        VX_CHECK(vx_buffer_create(device, state_bytes, VX_MEM_READ_WRITE,
                                  &state_buffer));

        vx_module_h module = nullptr;
        vx_kernel_h kernel = nullptr;
        VX_CHECK(vx_module_load_file(device, "kernel.vxbin", &module));
        VX_CHECK(vx_module_get_kernel(module, "main", &kernel));

        kernel_arg_t kernel_arg{};
        VX_CHECK(vx_buffer_address(state_buffer, &kernel_arg.state_addr));
        kernel_arg.pair_count = pair_count;

        VX_CHECK(vx_enqueue_write(queue, state_buffer, 0, initial.data(),
                                  state_bytes, 0, nullptr, nullptr));

        vx_launch_info_t launch{};
        launch.struct_size = sizeof(launch);
        launch.kernel = kernel;
        launch.args_host = &kernel_arg;
        launch.args_size = sizeof(kernel_arg);
        launch.ndim = 1;
        launch.grid_dim[0] = cta_count;
        launch.block_dim[0] = threads_per_cta;

        vx_event_h last_launch = nullptr;
        const auto start = std::chrono::steady_clock::now();
        for (uint32_t target = 0; target < num_qubits; ++target) {
            kernel_arg.target_qubit = target;
            vx_event_h* event = target + 1u == num_qubits
                                    ? &last_launch
                                    : nullptr;
            VX_CHECK(vx_enqueue_launch(queue, &launch, 0, nullptr, event));
        }

        std::vector<complex_t> actual(state_size);
        vx_event_h read_event = nullptr;
        VX_CHECK(vx_enqueue_read(queue, actual.data(), state_buffer, 0,
                                 state_bytes, 1, &last_launch, &read_event));
        VX_CHECK(vx_event_wait_value(read_event, 1, VX_TIMEOUT_INFINITE));
        const auto stop = std::chrono::steady_clock::now();

        float max_error = 0.0f;
        for (std::size_t index = 0; index < state_size; ++index) {
            max_error = std::max(
                max_error, std::abs(actual[index].real - expected[index].real));
            max_error = std::max(
                max_error, std::abs(actual[index].imag - expected[index].imag));
        }

        const std::chrono::duration<double, std::milli> elapsed = stop - start;
        std::cout << std::fixed << std::setprecision(6)
                  << "Simulator wall time (not target time): "
                  << elapsed.count() << " ms\n"
                  << "Maximum amplitude error: " << max_error << '\n';

        vx_event_release(read_event);
        vx_event_release(last_launch);
        VX_CHECK(vx_device_dump_perf(device, stdout));
        vx_kernel_release(kernel);
        vx_module_release(module);
        vx_buffer_release(state_buffer);
        vx_queue_release(queue);
        vx_device_release(device);

        if (max_error > 1.0e-5f) {
            std::cerr << "FAILED\n";
            return EXIT_FAILURE;
        }
        std::cout << "PASSED\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
