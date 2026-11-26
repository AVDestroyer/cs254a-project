#include <vortex2.h>

#include "common.h"
#include "../cpu/reference.h"

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

uint32_t parse_value(const std::string& text) {
    if (text.empty() || text[0] == '-') {
        throw std::invalid_argument("expected a nonnegative integer");
    }
    std::size_t consumed = 0;
    const auto value = std::stoul(text, &consumed);
    if (consumed != text.size() || value > UINT32_MAX) {
        throw std::invalid_argument("invalid integer: " + text);
    }
    return static_cast<uint32_t>(value);
}

std::string binary_label(uint32_t value, uint32_t bits) {
    std::string label(bits, '0');
    for (uint32_t bit = 0; bit < bits; ++bit) {
        label[bits - bit - 1u] = ((value >> bit) & 1u) ? '1' : '0';
    }
    return label;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        uint32_t num_qubits = 4;
        uint32_t iterations = 0;
        bool iterations_given = false;
        for (int i = 1; i < argc; ++i) {
            const std::string option(argv[i]);
            if (option.rfind("-n", 0) == 0) {
                num_qubits = parse_value(option.substr(2));
            } else if (option.rfind("-i", 0) == 0) {
                iterations = parse_value(option.substr(2));
                iterations_given = true;
            } else {
                throw std::invalid_argument("usage: grover-algorithm [-nQUBITS] [-iITERATIONS]");
            }
        }
        if (num_qubits < 2 || num_qubits > 20) {
            throw std::invalid_argument("QUBITS must be between 2 and 20");
        }

        constexpr uint32_t oracle_target = 2;
        constexpr double pi = 3.14159265358979323846;
        const uint32_t state_size = 1u << num_qubits;
        const uint32_t pair_count = state_size / 2u;
        if (!iterations_given) {
            iterations = static_cast<uint32_t>(
                std::floor((pi / 4.0) * std::sqrt(static_cast<double>(state_size))));
        }
        if (iterations == 0) {
            throw std::invalid_argument("ITERATIONS must be positive");
        }
        const uint64_t state_bytes = static_cast<uint64_t>(state_size) * sizeof(complex_t);

        const float initial_amplitude = 1.0f / std::sqrt(static_cast<float>(state_size));
        std::vector<complex_t> initial(state_size, {initial_amplitude, 0.0f});
        std::vector<complex_t> expected = initial;
        const auto cpu_start = std::chrono::steady_clock::now();
        run_grover_cpu(expected, oracle_target, iterations);
        const auto cpu_stop = std::chrono::steady_clock::now();

        vx_device_h device = nullptr;
        VX_CHECK(vx_device_open(0, &device));

        uint64_t core_count = 0;
        uint64_t warps_per_core = 0;
        uint64_t threads_per_warp = 0;
        VX_CHECK(vx_device_query(device, VX_CAPS_NUM_CORES, &core_count));
        VX_CHECK(vx_device_query(device, VX_CAPS_NUM_WARPS, &warps_per_core));
        VX_CHECK(vx_device_query(device, VX_CAPS_NUM_THREADS, &threads_per_warp));
        if (core_count == 0 || warps_per_core == 0 || threads_per_warp == 0) {
            throw std::runtime_error("Vortex reported no execution resources");
        }

        uint32_t threads_per_cta = static_cast<uint32_t>(
            std::min<uint64_t>(warps_per_core * threads_per_warp, pair_count));
        // The local-memory reduction halves the active thread count each step.
        while ((threads_per_cta & (threads_per_cta - 1u)) != 0u) {
            --threads_per_cta;
        }
        const uint32_t update_ctas =
            (pair_count + threads_per_cta - 1u) / threads_per_cta;
        const uint32_t partial_ctas = static_cast<uint32_t>(
            std::min<uint64_t>(4u * core_count, update_ctas));

        std::cout << "Grover search: " << num_qubits << " qubits, " << state_size
                  << " amplitudes, " << iterations << " iterations, target |"
                  << binary_label(oracle_target, num_qubits) << ">\n"
                  << "Hardware configuration: " << core_count << " cores x "
                  << warps_per_core << " warps x " << threads_per_warp
                  << " threads\n"
                  << "Launch configuration: " << partial_ctas
                  << " reduction CTAs, " << update_ctas << " update CTAs x "
                  << threads_per_cta << " threads\n"
                  << "Kernel launches: " << 3u * iterations << '\n';

        vx_queue_info_t queue_info{};
        queue_info.struct_size = sizeof(queue_info);
        queue_info.priority = VX_QUEUE_PRIORITY_NORMAL;
        vx_queue_h queue = nullptr;
        VX_CHECK(vx_queue_create(device, &queue_info, &queue));

        vx_buffer_h state_buffer = nullptr;
        vx_buffer_h partial_buffer = nullptr;
        vx_buffer_h mean_buffer = nullptr;
        VX_CHECK(vx_buffer_create(device, state_bytes, VX_MEM_READ_WRITE,
                                  &state_buffer));
        VX_CHECK(vx_buffer_create(device,
                                  static_cast<uint64_t>(partial_ctas) * sizeof(complex_t),
                                  VX_MEM_READ_WRITE, &partial_buffer));
        VX_CHECK(vx_buffer_create(device, sizeof(complex_t), VX_MEM_READ_WRITE,
                                  &mean_buffer));

        vx_module_h module = nullptr;
        vx_kernel_h kernel = nullptr;
        VX_CHECK(vx_module_load_file(device, "kernel.vxbin", &module));
        VX_CHECK(vx_module_get_kernel(module, "main", &kernel));

        kernel_arg_t kernel_arg{};
        VX_CHECK(vx_buffer_address(state_buffer, &kernel_arg.state_addr));
        VX_CHECK(vx_buffer_address(partial_buffer, &kernel_arg.partial_addr));
        VX_CHECK(vx_buffer_address(mean_buffer, &kernel_arg.mean_addr));
        kernel_arg.state_size = state_size;
        kernel_arg.pair_count = pair_count;
        kernel_arg.partial_count = partial_ctas;
        kernel_arg.target_index = oracle_target;

        VX_CHECK(vx_enqueue_write(queue, state_buffer, 0, initial.data(),
                                  state_bytes, 0, nullptr, nullptr));

        vx_launch_info_t launch{};
        launch.struct_size = sizeof(launch);
        launch.kernel = kernel;
        launch.args_host = &kernel_arg;
        launch.args_size = sizeof(kernel_arg);
        launch.ndim = 1;
        launch.block_dim[0] = threads_per_cta;

        auto enqueue_stage = [&](grover_op_t op, uint32_t ctas,
                                 vx_event_h* out_event = nullptr) {
            kernel_arg.op_type = op;
            launch.grid_dim[0] = ctas;
            VX_CHECK(vx_enqueue_launch(queue, &launch, 0, nullptr, out_event));
        };

        vx_event_h last_launch = nullptr;
        const auto start = std::chrono::steady_clock::now();
        for (uint32_t iter = 0; iter < iterations; ++iter) {
            enqueue_stage(OP_PARTIAL_SUM, partial_ctas);
            enqueue_stage(OP_FINAL_SUM, 1);
            enqueue_stage(OP_DIFFUSE, update_ctas,
                          iter + 1u == iterations ? &last_launch : nullptr);
        }

        std::vector<complex_t> actual(state_size);
        vx_event_h read_event = nullptr;
        VX_CHECK(vx_enqueue_read(queue, actual.data(), state_buffer, 0,
                                 state_bytes, 1, &last_launch, &read_event));
        VX_CHECK(vx_event_wait_value(read_event, 1, VX_TIMEOUT_INFINITE));
        const auto stop = std::chrono::steady_clock::now();

        float max_error = 0.0f;
        uint32_t most_probable_state = 0;
        double max_probability = -1.0;
        double total_probability = 0.0;
        for (uint32_t index = 0; index < state_size; ++index) {
            max_error = std::max(max_error,
                                 std::abs(actual[index].real - expected[index].real));
            max_error = std::max(max_error,
                                 std::abs(actual[index].imag - expected[index].imag));
            const double probability =
                static_cast<double>(actual[index].real) * actual[index].real +
                static_cast<double>(actual[index].imag) * actual[index].imag;
            total_probability += probability;
            if (probability > max_probability) {
                max_probability = probability;
                most_probable_state = index;
            }
            if (num_qubits <= 4) {
                std::cout << "  |" << binary_label(index, num_qubits)
                          << ">: " << probability * 100.0 << "%\n";
            }
        }

        const double theta = std::asin(1.0 / std::sqrt(static_cast<double>(state_size)));
        const double predicted_probability =
            std::pow(std::sin((2.0 * iterations + 1.0) * theta), 2.0);
        const std::chrono::duration<double, std::milli> gpu_elapsed = stop - start;
        const std::chrono::duration<double, std::milli> cpu_elapsed = cpu_stop - cpu_start;
        std::cout << std::fixed << std::setprecision(6)
                  << "Most probable state: |"
                  << binary_label(most_probable_state, num_qubits) << "> ("
                  << max_probability * 100.0 << "%)\n"
                  << "Predicted target probability: "
                  << predicted_probability * 100.0 << "%\n"
                  << "Total probability: " << total_probability << '\n'
                  << "CPU reference time: " << cpu_elapsed.count() << " ms\n"
                  << "Simulator wall time (not target time): "
                  << gpu_elapsed.count() << " ms\n"
                  << "Maximum amplitude error: " << max_error << '\n';

        vx_event_release(read_event);
        vx_event_release(last_launch);
        VX_CHECK(vx_device_dump_perf(device, stdout));
        vx_kernel_release(kernel);
        vx_module_release(module);
        vx_buffer_release(mean_buffer);
        vx_buffer_release(partial_buffer);
        vx_buffer_release(state_buffer);
        vx_queue_release(queue);
        vx_device_release(device);

        if (max_error > 1.0e-3f ||
            std::abs(total_probability - 1.0) > 1.0e-3 ||
            most_probable_state != oracle_target) {
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
