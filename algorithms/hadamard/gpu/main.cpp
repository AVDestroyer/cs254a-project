#include "common.h"
#include "../../../simulator/benchmark.h"
#include "../../../simulator/cpu_ops.h"
#include "../../../simulator/vortex_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        uint32_t qubits = 3;
        if (argc == 2 && std::string(argv[1]).rfind("-n", 0) == 0) {
            qubits = parse_uint32(std::string(argv[1]).substr(2));
        } else if (argc != 1) {
            throw std::invalid_argument("usage: hadamard [-nQUBITS]");
        }
        if (qubits < 1u || qubits > 20u) {
            throw std::invalid_argument("QUBITS must be between 1 and 20");
        }

        const uint32_t size = 1u << qubits;
        const uint32_t pairs = size / 2u;
        state_t initial(size, {0.0f, 0.0f});
        initial[0] = {1.0f, 0.0f};
        const cpu_result_t cpu = benchmark_cpu(initial, [qubits](state_t& state) {
            for (uint32_t target = 0; target < qubits; ++target) {
                apply_hadamard_cpu(state, target);
            }
        });

        vortex_runner_t runner(initial);
        const vortex_hardware_t& hardware = runner.hardware();
        const uint32_t block = static_cast<uint32_t>(
            std::min<uint64_t>(hardware.warps_per_core * hardware.threads_per_warp,
                               pairs));
        const uint32_t grid = (pairs + block - 1u) / block;
        kernel_arg_t arg{};
        arg.state_addr = runner.state_address();
        arg.pair_count = pairs;
        std::cout << "Hadamard circuit: " << qubits << " qubits, " << size
                  << " amplitudes\n"
                  << "Hardware configuration: " << hardware.cores << " cores x "
                  << hardware.warps_per_core << " warps x "
                  << hardware.threads_per_warp << " threads\n"
                  << "Launch configuration: " << grid << " CTAs x "
                  << block << " threads\n";
        for (uint32_t target = 0; target < qubits; ++target) {
            arg.target_qubit = target;
            runner.launch(arg, grid, block);
        }
        const gpu_result_t gpu = runner.finish();
        const double error = max_amplitude_error(gpu.state, cpu.state);
        const double norm = state_norm(gpu.state);
        const float uniform = 1.0f / std::sqrt(static_cast<float>(size));
        bool analytic_match = true;
        for (const complex_t& amplitude : gpu.state) {
            analytic_match = analytic_match &&
                std::abs(amplitude.real - uniform) < 1.0e-5f &&
                std::abs(amplitude.imag) < 1.0e-5f;
        }
        const bool passed = error <= 1.0e-5 &&
                            std::abs(norm - 1.0) <= 1.0e-3 && analytic_match;
        print_benchmark_report({"hadamard", qubits, gpu.launches,
                                cpu.median_ms, gpu.sim_wall_ms,
                                gpu.device_cycles, gpu.clock_mhz,
                                error, norm, passed});
        runner.dump_perf();
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
