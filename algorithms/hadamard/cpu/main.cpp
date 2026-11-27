#include "../../../simulator/benchmark.h"
#include "../../../simulator/cpu_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        uint32_t qubits = 3;
        if (argc == 2) {
            qubits = parse_uint32(argv[1]);
        } else if (argc != 1) {
            throw std::invalid_argument("usage: hadamard-cpu [QUBITS]");
        }
        if (qubits < 1u || qubits > 20u) {
            throw std::invalid_argument("QUBITS must be between 1 and 20");
        }
        const uint32_t size = 1u << qubits;
        state_t initial(size, {0.0f, 0.0f});
        initial[0] = {1.0f, 0.0f};
        const cpu_result_t result = benchmark_cpu(initial, [qubits](state_t& state) {
            for (uint32_t target = 0; target < qubits; ++target) {
                apply_hadamard_cpu(state, target);
            }
        });
        std::cout << std::fixed << std::setprecision(7);
        if (qubits <= 4u) {
            for (uint32_t index = 0; index < size; ++index) {
                const complex_t amplitude = result.state[index];
                std::cout << '|' << binary_label(index, qubits) << ">: "
                          << amplitude.real
                          << (amplitude.imag >= 0.0f ? "+" : "")
                          << amplitude.imag << "i\n";
            }
        }
        const double norm = state_norm(result.state);
        std::cout << "CPU median execution time: " << result.median_ms << " ms\n"
                  << "Total probability: " << norm << '\n';
        return std::abs(norm - 1.0) <= 1.0e-3 ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
