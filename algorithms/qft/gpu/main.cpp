#include "common.h"
#include "../cpu/reference.h"
#include "../../../simulator/benchmark.h"
#include "../../../simulator/gpu_tuning.h"

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
        gpu_tuning_t tuning;
        for (int index = 1; index < argc; ++index) {
            const std::string option(argv[index]);
            if (option.rfind("-n", 0) == 0) {
                qubits = parse_uint32(option.substr(2));
            } else if (!parse_gpu_tuning_option(option, tuning)) {
                throw std::invalid_argument(
                    "usage: qft [-nQUBITS] [-bBLOCK] [-wWORK] [-sMODE]");
            }
        }
        if (tuning.reduction_ctas != 0u || tuning.reduction_work_per_thread != 0u) {
            throw std::invalid_argument("reduction tuning applies only to Grover");
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

        vortex_runner_t runner(initial, tuning.submission);
        const vortex_hardware_t& hardware = runner.hardware();
        const uint32_t block = select_block(tuning, hardware, pairs);
        const uint32_t pair_grid = work_grid(pairs, block, tuning.work_per_thread);
        const uint32_t amplitude_grid = work_grid(size, block, tuning.work_per_thread);
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
                  << " pair CTAs, " << amplitude_grid << " amplitude CTAs x "
                  << block << " threads, approximately " << tuning.work_per_thread
                  << " items per thread; submission="
                  << submission_mode_name(tuning.submission) << '\n';
        for (uint32_t target = qubits; target-- > 0u;) {
            arg.op_type = QFT_HADAMARD;
            arg.target_qubit = target;
            runner.launch(arg, pair_grid, block);
            for (uint32_t control = target; control-- > 0u;) {
                const double angle = pi / static_cast<double>(
                    1u << (target - control));
                arg.op_type = QFT_CONTROLLED_PHASE;
                arg.other_qubit = control;
                arg.cosine = static_cast<float>(std::cos(angle));
                arg.sine = static_cast<float>(std::sin(angle));
                runner.launch(arg, amplitude_grid, block);
            }
        }
        for (uint32_t first = 0; first < qubits / 2u; ++first) {
            arg.op_type = QFT_SWAP;
            arg.target_qubit = first;
            arg.other_qubit = qubits - first - 1u;
            runner.launch(arg, amplitude_grid, block);
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
        benchmark_report_t report{"qft", qubits, gpu.launches,
                                  cpu.median_ms, gpu.sim_wall_ms,
                                  gpu.device_cycles, gpu.clock_mhz,
                                  error, norm, passed};
        report.submission_mode = submission_mode_name(tuning.submission);
        report.block = block;
        report.work_per_thread = tuning.work_per_thread;
        report.host_prepare_ms = gpu.host_prepare_ms;
        report.host_submit_ms = gpu.host_submit_ms;
        report.queue_wait_ms = gpu.queue_wait_ms;
        report.queue_execution_ms = gpu.queue_execution_ms;
        print_benchmark_report(report);
        runner.dump_perf();
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
