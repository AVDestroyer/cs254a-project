#include "common.h"
#include "../cpu/reference.h"
#include "../../../simulator/benchmark.h"
#include "../../../simulator/vortex_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        uint32_t qubits = 4;
        if (argc == 2 && std::string(argv[1]).rfind("-n", 0) == 0) {
            qubits = parse_uint32(std::string(argv[1]).substr(2));
        } else if (argc != 1) {
            throw std::invalid_argument("usage: qft [-nQUBITS]");
        }
        if (qubits < 1u || qubits > 20u) {
            throw std::invalid_argument("QUBITS must be between 1 and 20");
        }

        const uint32_t size = 1u << qubits;
        const uint32_t pairs = size / 2u;
        const state_t initial = make_qft_initial(qubits);
        const cpu_result_t cpu = benchmark_cpu(initial, [qubits](state_t& state) {
            run_qft_cpu(state, qubits);
        });

        vortex_runner_t runner(initial);
        const vortex_hardware_t& hardware = runner.hardware();
        const uint32_t capacity = static_cast<uint32_t>(
            hardware.warps_per_core * hardware.threads_per_warp);
        const uint32_t pair_block = std::min(capacity, pairs);
        const uint32_t amplitude_block = std::min(capacity, size);
        const uint32_t pair_grid = (pairs + pair_block - 1u) / pair_block;
        const uint32_t amplitude_grid =
            (size + amplitude_block - 1u) / amplitude_block;
        kernel_arg_t arg{};
        arg.state_addr = runner.state_address();
        arg.state_size = size;
        arg.pair_count = pairs;
        constexpr double pi = 3.14159265358979323846;

        std::cout << "QFT: " << qubits << " qubits, " << size
                  << " amplitudes, dense deterministic input\n"
                  << "Hardware configuration: " << hardware.cores << " cores x "
                  << hardware.warps_per_core << " warps x "
                  << hardware.threads_per_warp << " threads\n"
                  << "Launch configuration: " << pair_grid
                  << " pair CTAs, " << amplitude_grid << " amplitude CTAs\n";
        for (uint32_t target = qubits; target-- > 0u;) {
            arg.op_type = QFT_HADAMARD;
            arg.target_qubit = target;
            runner.launch(arg, pair_grid, pair_block);
            for (uint32_t control = target; control-- > 0u;) {
                const double angle = pi / static_cast<double>(
                    1u << (target - control));
                arg.op_type = QFT_CONTROLLED_PHASE;
                arg.other_qubit = control;
                arg.cosine = static_cast<float>(std::cos(angle));
                arg.sine = static_cast<float>(std::sin(angle));
                runner.launch(arg, amplitude_grid, amplitude_block);
            }
        }
        for (uint32_t first = 0; first < qubits / 2u; ++first) {
            arg.op_type = QFT_SWAP;
            arg.target_qubit = first;
            arg.other_qubit = qubits - first - 1u;
            runner.launch(arg, amplitude_grid, amplitude_block);
        }

        const gpu_result_t gpu = runner.finish();
        const double error = max_amplitude_error(gpu.state, cpu.state);
        const double norm = state_norm(gpu.state);
        const double dft_error = qubits <= 6u
                                     ? qft_dft_error(initial, gpu.state)
                                     : 0.0;
        if (qubits <= 6u) {
            std::cout << std::fixed << std::setprecision(6)
                      << "Direct DFT error: " << dft_error << '\n';
        }
        const bool passed = error <= 5.0e-4 &&
                            std::abs(norm - 1.0) <= 1.0e-3 &&
                            dft_error <= 5.0e-5;
        print_benchmark_report({"qft", qubits, gpu.launches,
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
