#include "common.h"
#include "../../../simulator/benchmark.h"
#include "../../../simulator/cpu_ops.h"
#include "../../../simulator/gpu_tuning.h"

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
        uint32_t gate_count = 0;
        bool gate_count_given = false;
        gpu_tuning_t tuning;
        for (int index = 1; index < argc; ++index) {
            const std::string option(argv[index]);
            if (option.rfind("-n", 0) == 0) {
                qubits = parse_uint32(option.substr(2));
            } else if (option.rfind("-l", 0) == 0) {
                gate_count = parse_uint32(option.substr(2));
                gate_count_given = true;
            } else if (!parse_gpu_tuning_option(option, tuning)) {
                throw std::invalid_argument(
                    "usage: hadamard [-nQUBITS] [-lLAUNCHES] [-bBLOCK] [-wWORK] [-sMODE]");
            }
        }
        if (tuning.reduction_ctas != 0u || tuning.reduction_work_per_thread != 0u) {
            throw std::invalid_argument("reduction tuning applies only to Grover");
        }
        if (qubits < 1u || qubits > 20u) {
            throw std::invalid_argument("QUBITS must be between 1 and 20");
        }
        if (!gate_count_given) gate_count = qubits;
        if (gate_count == 0u || gate_count > 256u) {
            throw std::invalid_argument("LAUNCHES must be between 1 and 256");
        }

        const uint32_t size = 1u << qubits;
        const uint32_t pairs = size / 2u;
        state_t initial(size, {0.0f, 0.0f});
        initial[0] = {1.0f, 0.0f};
        const cpu_result_t cpu = benchmark_cpu(initial, [qubits, gate_count](state_t& state) {
            for (uint32_t gate = 0; gate < gate_count; ++gate) {
                apply_hadamard_cpu(state, gate % qubits);
            }
        });

        vortex_runner_t runner(initial, tuning.submission);
        const vortex_hardware_t& hardware = runner.hardware();
        const uint32_t block = select_block(tuning, hardware, pairs);
        const uint32_t grid = work_grid(pairs, block, tuning.work_per_thread);
        kernel_arg_t arg{};
        arg.state_addr = runner.state_address();
        arg.pair_count = pairs;
        std::cout << "Hadamard circuit: " << qubits << " qubits, " << size
                  << " amplitudes, " << gate_count << " gates\n"
                  << "Hardware configuration: " << hardware.cores << " cores x "
                  << hardware.warps_per_core << " warps x "
                  << hardware.threads_per_warp << " threads\n"
                  << "Launch configuration: " << grid << " CTAs x "
                  << block << " threads, approximately " << tuning.work_per_thread
                  << " pairs per thread; submission="
                  << submission_mode_name(tuning.submission) << '\n';
        for (uint32_t gate = 0; gate < gate_count; ++gate) {
            arg.target_qubit = gate % qubits;
            runner.launch(arg, grid, block);
        }
        const gpu_result_t gpu = runner.finish();
        const double error = max_amplitude_error(gpu.state, cpu.state);
        const double norm = state_norm(gpu.state);
        const float uniform = 1.0f / std::sqrt(static_cast<float>(size));
        bool analytic_match = true;
        if (gate_count == qubits) {
            for (const complex_t& amplitude : gpu.state) {
                analytic_match = analytic_match &&
                    std::abs(amplitude.real - uniform) < 1.0e-5f &&
                    std::abs(amplitude.imag) < 1.0e-5f;
            }
        }
        const bool passed = error <= 1.0e-5 &&
                            std::abs(norm - 1.0) <= 1.0e-3 && analytic_match;
        benchmark_report_t report{"hadamard", qubits, gpu.launches,
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
