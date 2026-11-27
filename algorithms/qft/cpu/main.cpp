#include "reference.h"

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
        if (argc == 2) {
            qubits = parse_uint32(argv[1]);
        } else if (argc != 1) {
            throw std::invalid_argument("usage: qft-cpu [QUBITS]");
        }
        if (qubits < 1u || qubits > 20u) {
            throw std::invalid_argument("QUBITS must be between 1 and 20");
        }
        const state_t initial = make_qft_initial(qubits);
        const cpu_result_t cpu = benchmark_cpu(initial, [qubits](state_t& state) {
            run_qft_cpu(state, qubits);
        });
        const double norm = state_norm(cpu.state);
        const double dft_error = qubits <= 6u
                                     ? qft_dft_error(initial, cpu.state)
                                     : 0.0;
        std::cout << std::fixed << std::setprecision(6)
                  << "QFT: " << qubits << " qubits, " << initial.size()
                  << " amplitudes\n"
                  << "CPU median execution time: " << cpu.median_ms << " ms\n"
                  << "Total probability: " << norm << '\n';
        if (qubits <= 6u) {
            std::cout << "Direct DFT error: " << dft_error << '\n';
        }
        return std::abs(norm - 1.0) <= 1.0e-3 && dft_error <= 5.0e-5
                   ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
