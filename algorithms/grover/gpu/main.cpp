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
        uint32_t iterations = 0;
        bool iterations_given = false;
        gpu_tuning_t tuning;
        for (int index = 1; index < argc; ++index) {
            const std::string option(argv[index]);
            if (option.rfind("-n", 0) == 0) {
                qubits = parse_uint32(option.substr(2));
            } else if (option.rfind("-i", 0) == 0) {
                iterations = parse_uint32(option.substr(2));
                iterations_given = true;
            } else if (!parse_gpu_tuning_option(option, tuning)) {
                throw std::invalid_argument(
                    "usage: grover [-nQUBITS] [-iITERATIONS] [-bBLOCK] "
                    "[-wWORK] [-rREDUCTION_CTAS | -pREDUCTION_WORK] [-sMODE]");
            }
        }
        if (qubits < 2u || qubits > 20u) {
            throw std::invalid_argument("QUBITS must be between 2 and 20");
        }
        constexpr uint32_t target = 2;
        constexpr double pi = 3.14159265358979323846;
        const uint32_t size = 1u << qubits;
        const uint32_t pairs = size / 2u;
        if (!iterations_given) {
            iterations = static_cast<uint32_t>(
                std::floor((pi / 4.0) * std::sqrt(static_cast<double>(size))));
        }
        if (iterations == 0u) {
            throw std::invalid_argument("ITERATIONS must be positive");
        }

        const float initial_amplitude = 1.0f / std::sqrt(static_cast<float>(size));
        const state_t initial(size, {initial_amplitude, 0.0f});
        const cpu_result_t cpu = benchmark_cpu(initial, [iterations](state_t& state) {
            run_grover_cpu(state, target, iterations);
        });

        vortex_runner_t runner(initial, tuning.submission);
        const vortex_hardware_t& hardware = runner.hardware();
        const uint32_t block = select_block(tuning, hardware, pairs);
        const uint32_t update_ctas = work_grid(pairs, block, tuning.work_per_thread);
        if (tuning.reduction_ctas != 0u && tuning.reduction_work_per_thread != 0u) {
            throw std::invalid_argument("use either reduction CTAs or reduction work");
        }
        if (tuning.reduction_work_per_thread > 64u) {
            throw std::invalid_argument("reduction work per thread must be at most 64");
        }
        const uint32_t reduction_work = std::max(1u, tuning.reduction_work_per_thread);
        const uint32_t useful_ctas = work_grid(pairs, block, reduction_work);
        const uint32_t partial_ctas = tuning.reduction_ctas == 0u
            ? static_cast<uint32_t>(std::min<uint64_t>(4u * hardware.cores, useful_ctas))
            : tuning.reduction_ctas;
        if (partial_ctas == 0u || partial_ctas >
            static_cast<uint32_t>(std::min<uint64_t>(4u * hardware.cores,
                                                    work_grid(pairs, block, 1u)))) {
            throw std::invalid_argument("reduction CTAs exceed the useful hardware limit");
        }
        const uint32_t actual_reduction_work = work_grid(pairs, block * partial_ctas, 1u);

        kernel_arg_t arg{};
        arg.state_addr = runner.state_address();
        arg.partial_addr = runner.buffer_address(
            runner.create_buffer(static_cast<uint64_t>(partial_ctas) * sizeof(complex_t)));
        arg.mean_addr = runner.buffer_address(runner.create_buffer(sizeof(complex_t)));
        arg.state_size = size;
        arg.pair_count = pairs;
        arg.partial_count = partial_ctas;
        arg.target_index = target;

        std::cout << "Grover search: " << qubits << " qubits, " << size
                  << " amplitudes, " << iterations << " iterations, target |"
                  << binary_label(target, qubits) << ">\n"
                  << "Hardware configuration: " << hardware.cores << " cores x "
                  << hardware.warps_per_core << " warps x "
                  << hardware.threads_per_warp << " threads\n"
                  << "Launch configuration: " << partial_ctas
                  << " reduction CTAs, " << update_ctas << " update CTAs x "
                  << block << " threads, approximately " << tuning.work_per_thread
                  << " update pairs/thread and " << actual_reduction_work
                  << " reduction pairs/thread; submission="
                  << submission_mode_name(tuning.submission) << '\n';
        for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
            arg.op_type = OP_PARTIAL_SUM;
            runner.launch(arg, partial_ctas, block, block * sizeof(complex_t));
            arg.op_type = OP_FINAL_SUM;
            runner.launch(arg, 1, block);
            arg.op_type = OP_DIFFUSE;
            runner.launch(arg, update_ctas, block);
        }
        const gpu_result_t gpu = runner.finish();
        const double error = max_amplitude_error(gpu.state, cpu.state);
        const double norm = state_norm(gpu.state);
        const double target_probability =
            static_cast<double>(gpu.state[target].real) * gpu.state[target].real +
            static_cast<double>(gpu.state[target].imag) * gpu.state[target].imag;
        const double theta = std::asin(1.0 / std::sqrt(static_cast<double>(size)));
        const double predicted_probability =
            std::pow(std::sin((2.0 * iterations + 1.0) * theta), 2.0);
        uint32_t most_probable = 0;
        double largest_probability = -1.0;
        for (uint32_t index = 0; index < size; ++index) {
            const double probability =
                static_cast<double>(gpu.state[index].real) * gpu.state[index].real +
                static_cast<double>(gpu.state[index].imag) * gpu.state[index].imag;
            if (probability > largest_probability) {
                largest_probability = probability;
                most_probable = index;
            }
        }
        std::cout << std::fixed << std::setprecision(6)
                  << "Most probable state: |" << binary_label(most_probable, qubits)
                  << "> (" << largest_probability * 100.0 << "%)\n"
                  << "Target probability: " << target_probability * 100.0 << "%\n"
                  << "Predicted target probability: "
                  << predicted_probability * 100.0 << "%\n";
        const bool passed = error <= 1.0e-3 &&
                            std::abs(norm - 1.0) <= 1.0e-3 &&
                            std::abs(target_probability - predicted_probability) <= 5.0e-3;
        benchmark_report_t report{"grover", qubits, gpu.launches,
                                  cpu.median_ms, gpu.sim_wall_ms,
                                  gpu.device_cycles, gpu.clock_mhz,
                                  error, norm, passed};
        report.submission_mode = submission_mode_name(tuning.submission);
        report.block = block;
        report.work_per_thread = tuning.work_per_thread;
        report.reduction_ctas = partial_ctas;
        report.reduction_work_per_thread = actual_reduction_work;
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
