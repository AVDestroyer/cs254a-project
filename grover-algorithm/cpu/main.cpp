#include "reference.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
                throw std::invalid_argument("usage: grover-cpu [-nQUBITS] [-iITERATIONS]");
            }
        }
        if (num_qubits < 2 || num_qubits > 20) {
            throw std::invalid_argument("QUBITS must be between 2 and 20");
        }

        constexpr uint32_t target = 2;
        constexpr double pi = 3.14159265358979323846;
        const uint32_t state_size = 1u << num_qubits;
        if (!iterations_given) {
            iterations = static_cast<uint32_t>(
                std::floor((pi / 4.0) * std::sqrt(static_cast<double>(state_size))));
        }
        if (iterations == 0) {
            throw std::invalid_argument("ITERATIONS must be positive");
        }

        const float initial_amplitude = 1.0f / std::sqrt(static_cast<float>(state_size));
        std::vector<complex_t> state(state_size, {initial_amplitude, 0.0f});
        const auto start = std::chrono::steady_clock::now();
        run_grover_cpu(state, target, iterations);
        const auto stop = std::chrono::steady_clock::now();

        const double target_probability =
            static_cast<double>(state[target].real) * state[target].real +
            static_cast<double>(state[target].imag) * state[target].imag;
        double total_probability = 0.0;
        for (const complex_t& amplitude : state) {
            total_probability +=
                static_cast<double>(amplitude.real) * amplitude.real +
                static_cast<double>(amplitude.imag) * amplitude.imag;
        }
        const std::chrono::duration<double, std::milli> elapsed = stop - start;
        std::cout << std::fixed << std::setprecision(6)
                  << "Grover search: " << num_qubits << " qubits, "
                  << iterations << " iterations, target index " << target << '\n'
                  << "Target probability: " << target_probability * 100.0 << "%\n"
                  << "Total probability: " << total_probability << '\n'
                  << "CPU execution time: " << elapsed.count() << " ms\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
