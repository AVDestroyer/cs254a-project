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

void apply_gate_cpu(std::vector<complex_t>& state, uint32_t op_type,
                    uint32_t target, uint32_t target_index) {
    const std::size_t pair_distance = std::size_t{1} << target;
    const std::size_t block_size = 2 * pair_distance;
    const float inverse_sqrt_two = 1.0f / std::sqrt(2.0f);

    for (std::size_t base = 0; base < state.size(); base += block_size) {
        for (std::size_t offset = 0; offset < pair_distance; ++offset) {
            const std::size_t zero_index = base + offset;
            const std::size_t one_index = zero_index + pair_distance;
            const complex_t zero = state[zero_index];
            const complex_t one = state[one_index];

            if (op_type == OP_HADAMARD) {
                state[zero_index] = {
                    (zero.real + one.real) * inverse_sqrt_two,
                    (zero.imag + one.imag) * inverse_sqrt_two,
                };
                state[one_index] = {
                    (zero.real - one.real) * inverse_sqrt_two,
                    (zero.imag - one.imag) * inverse_sqrt_two,
                };
            } else if (op_type == OP_PAULI_X) {
                state[zero_index] = one;
                state[one_index] = zero;
            } else if (op_type == OP_PHASE_FLIP) {
                if (zero_index == target_index) {
                    state[zero_index] = {-zero.real, -zero.imag};
                }
                if (one_index == target_index) {
                    state[one_index] = {-one.real, -one.imag};
                }
            }
        }
    }
}

void run_grover_cpu(std::vector<complex_t>& state, uint32_t num_qubits,
                    uint32_t oracle_target, uint32_t iterations) {
    const uint32_t all_ones = (1u << num_qubits) - 1u;

    // 1. Initial superposition H^{\otimes n}
    for (uint32_t q = 0; q < num_qubits; ++q) {
        apply_gate_cpu(state, OP_HADAMARD, q, 0);
    }

    // 2. Grover iterations
    for (uint32_t iter = 0; iter < iterations; ++iter) {
        // Oracle: flip phase of |0010>
        apply_gate_cpu(state, OP_PHASE_FLIP, 0, oracle_target);

        // Diffuser: H^{\otimes n} X^{\otimes n} MCZ X^{\otimes n} H^{\otimes n}
        for (uint32_t q = 0; q < num_qubits; ++q) {
            apply_gate_cpu(state, OP_HADAMARD, q, 0);
        }
        for (uint32_t q = 0; q < num_qubits; ++q) {
            apply_gate_cpu(state, OP_PAULI_X, q, 0);
        }
        apply_gate_cpu(state, OP_PHASE_FLIP, 0, all_ones);
        for (uint32_t q = 0; q < num_qubits; ++q) {
            apply_gate_cpu(state, OP_PAULI_X, q, 0);
        }
        for (uint32_t q = 0; q < num_qubits; ++q) {
            apply_gate_cpu(state, OP_HADAMARD, q, 0);
        }
    }
}

}  // namespace

int main() {
    try {
        constexpr uint32_t num_qubits = 4;
        constexpr uint32_t oracle_target = 0b0010;  // |0010> = index 2
        constexpr uint32_t iterations = 3;          // floor(pi/4 * sqrt(16)) = 3
        constexpr uint32_t all_ones = (1u << num_qubits) - 1u;

        const std::size_t state_size = std::size_t{1} << num_qubits;
        const uint32_t pair_count = uint32_t{1} << (num_qubits - 1u);
        const uint64_t state_bytes = state_size * sizeof(complex_t);

        std::vector<complex_t> initial(state_size, {0.0f, 0.0f});
        initial[0] = {1.0f, 0.0f};

        std::vector<complex_t> expected = initial;
        run_grover_cpu(expected, num_qubits, oracle_target, iterations);

        vx_device_h device = nullptr;
        VX_CHECK(vx_device_open(0, &device));

        uint64_t core_count = 0;
        uint64_t warps_per_core = 0;
        uint64_t threads_per_warp = 0;
        VX_CHECK(vx_device_query(device, VX_CAPS_NUM_CORES, &core_count));
        VX_CHECK(vx_device_query(device, VX_CAPS_NUM_WARPS, &warps_per_core));
        VX_CHECK(vx_device_query(device, VX_CAPS_NUM_THREADS, &threads_per_warp));

        const uint32_t max_threads_per_cta =
            static_cast<uint32_t>(warps_per_core * threads_per_warp);
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

        auto enqueue_gate = [&](uint32_t op_type, uint32_t target_qubit,
                                uint32_t target_index,
                                vx_event_h* out_event = nullptr) {
            kernel_arg.op_type = op_type;
            kernel_arg.target_qubit = target_qubit;
            kernel_arg.target_index = target_index;
            VX_CHECK(vx_enqueue_launch(queue, &launch, 0, nullptr, out_event));
        };

        vx_event_h last_launch = nullptr;
        const auto start = std::chrono::steady_clock::now();

        // Step 1: Uniform superposition H^{\otimes 4}
        for (uint32_t q = 0; q < num_qubits; ++q) {
            enqueue_gate(OP_HADAMARD, q, 0);
        }

        // Step 2: Grover iterations (Oracle + Diffuser)
        for (uint32_t iter = 0; iter < iterations; ++iter) {
            // Oracle: phase flip |0010>
            enqueue_gate(OP_PHASE_FLIP, 0, oracle_target);

            // Diffuser
            for (uint32_t q = 0; q < num_qubits; ++q) {
                enqueue_gate(OP_HADAMARD, q, 0);
            }
            for (uint32_t q = 0; q < num_qubits; ++q) {
                enqueue_gate(OP_PAULI_X, q, 0);
            }
            enqueue_gate(OP_PHASE_FLIP, 0, all_ones);
            for (uint32_t q = 0; q < num_qubits; ++q) {
                enqueue_gate(OP_PAULI_X, q, 0);
            }
            for (uint32_t q = 0; q < num_qubits; ++q) {
                const bool is_final_gate =
                    (iter + 1u == iterations) && (q + 1u == num_qubits);
                enqueue_gate(OP_HADAMARD, q, 0,
                             is_final_gate ? &last_launch : nullptr);
            }
        }

        std::vector<complex_t> actual(state_size);
        vx_event_h read_event = nullptr;
        VX_CHECK(vx_enqueue_read(queue, actual.data(), state_buffer, 0,
                                 state_bytes, 1, &last_launch, &read_event));
        VX_CHECK(vx_event_wait_value(read_event, 1, VX_TIMEOUT_INFINITE));
        const auto stop = std::chrono::steady_clock::now();

        float max_error = 0.0f;
        std::size_t measured_state = 0;
        float max_prob = -1.0f;

        std::cout << "\nFinal State Vector Probabilities:\n";
        for (std::size_t index = 0; index < state_size; ++index) {
            max_error = std::max(
                max_error, std::abs(actual[index].real - expected[index].real));
            max_error = std::max(
                max_error, std::abs(actual[index].imag - expected[index].imag));

            const float prob = actual[index].real * actual[index].real +
                               actual[index].imag * actual[index].imag;
            if (prob > max_prob) {
                max_prob = prob;
                measured_state = index;
            }

            std::cout << "  |" << ((index >> 3) & 1) << ((index >> 2) & 1)
                      << ((index >> 1) & 1) << (index & 1) << "> : amp=("
                      << std::setw(9) << std::setprecision(5) << actual[index].real
                      << ", " << std::setw(8) << actual[index].imag
                      << ")  prob=" << std::setprecision(4) << (prob * 100.0f)
                      << "%\n";
        }

        const std::chrono::duration<double, std::milli> elapsed = stop - start;
        std::cout << std::fixed << std::setprecision(6)
                  << "\nMost probable state: |" << ((measured_state >> 3) & 1)
                  << ((measured_state >> 2) & 1) << ((measured_state >> 1) & 1)
                  << (measured_state & 1) << "> (" << (max_prob * 100.0f)
                  << "%)\n"
                  << "Simulator wall time: " << elapsed.count() << " ms\n"
                  << "Maximum amplitude error: " << max_error << '\n';

        vx_event_release(read_event);
        vx_event_release(last_launch);
        VX_CHECK(vx_device_dump_perf(device, stdout));
        vx_kernel_release(kernel);
        vx_module_release(module);
        vx_buffer_release(state_buffer);
        vx_queue_release(queue);
        vx_device_release(device);

        if (max_error > 1.0e-5f || measured_state != oracle_target) {
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