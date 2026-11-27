#include "reference.h"
#include "../../../simulator/benchmark.h"

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
        for (int index = 1; index < argc; ++index) {
            const std::string option(argv[index]);
            if (option.rfind("-n", 0) == 0) {
                qubits = parse_uint32(option.substr(2));
            } else if (option.rfind("-i", 0) == 0) {
                iterations = parse_uint32(option.substr(2));
                iterations_given = true;
            } else {
                throw std::invalid_argument("usage: grover-cpu [-nQUBITS] [-iITERATIONS]");
            }
        }
        if (qubits < 2u || qubits > 20u) {
            throw std::invalid_argument("QUBITS must be between 2 and 20");
        }
        const uint32_t size = 1u << qubits;
        constexpr double pi = 3.14159265358979323846;
        if (!iterations_given) {
            iterations = static_cast<uint32_t>(
                std::floor((pi / 4.0) * std::sqrt(static_cast<double>(size))));
        }
        if (iterations == 0u) {
            throw std::invalid_argument("ITERATIONS must be positive");
        }
        constexpr uint32_t target = 2;
        const float amplitude = 1.0f / std::sqrt(static_cast<float>(size));
        const state_t initial(size, {amplitude, 0.0f});
        const cpu_result_t result = benchmark_cpu(initial, [iterations](state_t& state) {
            run_grover_cpu(state, target, iterations);
        });
        const double target_probability =
            static_cast<double>(result.state[target].real) * result.state[target].real +
            static_cast<double>(result.state[target].imag) * result.state[target].imag;
        const double norm = state_norm(result.state);
        std::cout << std::fixed << std::setprecision(6)
                  << "Grover search: " << qubits << " qubits, "
                  << iterations << " iterations, target index " << target << '\n'
                  << "Target probability: " << target_probability * 100.0 << "%\n"
                  << "Total probability: " << norm << '\n'
                  << "CPU median execution time: " << result.median_ms << " ms\n";
        return std::abs(norm - 1.0) <= 1.0e-3 ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
