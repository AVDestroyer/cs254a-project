#ifndef QUANTUM_SIMULATOR_BENCHMARK_H
#define QUANTUM_SIMULATOR_BENCHMARK_H

#include "state.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using state_t = std::vector<complex_t>;

inline uint32_t parse_uint32(const std::string& text) {
    if (text.empty() || text[0] == '-') {
        throw std::invalid_argument("expected a nonnegative integer");
    }
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed);
    if (consumed != text.size() || value > UINT32_MAX) {
        throw std::invalid_argument("invalid integer: " + text);
    }
    return static_cast<uint32_t>(value);
}

inline std::string binary_label(uint32_t value, uint32_t bits) {
    std::string label(bits, '0');
    for (uint32_t bit = 0; bit < bits; ++bit) {
        label[bits - bit - 1u] = ((value >> bit) & 1u) ? '1' : '0';
    }
    return label;
}

struct cpu_result_t {
    state_t state;
    double median_ms = 0.0;
};

template <typename Run>
cpu_result_t benchmark_cpu(const state_t& initial, Run run,
                           uint32_t repeats = 5u) {
    if (repeats == 0u) {
        throw std::invalid_argument("CPU repeats must be positive");
    }
    state_t warmup = initial;
    run(warmup);

    std::vector<double> times;
    times.reserve(repeats);
    state_t expected;
    volatile double checksum = 0.0;
    for (uint32_t repeat = 0; repeat < repeats; ++repeat) {
        state_t state = initial;
        const auto start = std::chrono::steady_clock::now();
        run(state);
        const auto stop = std::chrono::steady_clock::now();
        times.push_back(
            std::chrono::duration<double, std::milli>(stop - start).count());
        for (const complex_t& amplitude : state) {
            checksum += amplitude.real + amplitude.imag;
        }
        if (repeat == 0u) {
            expected = std::move(state);
        }
    }
    (void)checksum;
    std::sort(times.begin(), times.end());
    const double median = times.size() % 2u == 1u
                              ? times[times.size() / 2u]
                              : 0.5 * (times[times.size() / 2u - 1u] +
                                       times[times.size() / 2u]);
    return {std::move(expected), median};
}

inline double max_amplitude_error(const state_t& actual,
                                  const state_t& expected) {
    if (actual.size() != expected.size()) {
        throw std::invalid_argument("state sizes differ");
    }
    double error = 0.0;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        error = std::max(error, static_cast<double>(
            std::abs(actual[index].real - expected[index].real)));
        error = std::max(error, static_cast<double>(
            std::abs(actual[index].imag - expected[index].imag)));
    }
    return error;
}

inline double state_norm(const state_t& state) {
    double norm = 0.0;
    for (const complex_t& amplitude : state) {
        norm += static_cast<double>(amplitude.real) * amplitude.real +
                static_cast<double>(amplitude.imag) * amplitude.imag;
    }
    return norm;
}

struct benchmark_report_t {
    std::string algorithm;
    uint32_t qubits = 0;
    uint32_t launches = 0;
    double cpu_median_ms = 0.0;
    double sim_wall_ms = 0.0;
    uint64_t device_cycles = 0;
    uint64_t clock_mhz = 0;
    double max_error = 0.0;
    double norm = 0.0;
    bool passed = false;
};

inline void print_benchmark_report(const benchmark_report_t& report) {
    const double modeled_ms = report.clock_mhz == 0u
        ? std::numeric_limits<double>::quiet_NaN()
        : static_cast<double>(report.device_cycles) /
              (static_cast<double>(report.clock_mhz) * 1000.0);
    std::cout << std::fixed << std::setprecision(9)
              << "CPU median execution time: " << report.cpu_median_ms << " ms\n"
              << "Vortex device cycles: " << report.device_cycles << '\n'
              << "Vortex clock: " << report.clock_mhz << " MHz\n"
              << "Modeled device time (not measured GPU hardware time): "
              << modeled_ms << " ms\n"
              << "Simulator wall time (not target time): "
              << report.sim_wall_ms << " ms\n"
              << "Kernel launches: " << report.launches << '\n'
              << "Maximum amplitude error: " << report.max_error << '\n'
              << "Total probability: " << report.norm << '\n'
              << "RESULT_JSON: {\"algorithm\":\"" << report.algorithm
              << "\",\"qubits\":" << report.qubits
              << ",\"cpu_median_ms\":" << report.cpu_median_ms
              << ",\"device_cycles\":" << report.device_cycles
              << ",\"clock_mhz\":" << report.clock_mhz
              << ",\"modeled_device_ms\":" << modeled_ms
              << ",\"sim_wall_ms\":" << report.sim_wall_ms
              << ",\"launches\":" << report.launches
              << ",\"max_error\":" << report.max_error
              << ",\"norm\":" << report.norm
              << ",\"passed\":" << (report.passed ? "true" : "false")
              << "}\n"
              << (report.passed ? "PASSED\n" : "FAILED\n");
}

#endif
