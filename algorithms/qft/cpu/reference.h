#ifndef QFT_CPU_REFERENCE_H
#define QFT_CPU_REFERENCE_H

#include "../../../simulator/benchmark.h"
#include "../../../simulator/cpu_ops.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <vector>

inline state_t make_qft_initial(uint32_t qubits) {
    state_t state(1u << qubits);
    std::mt19937 engine(0x254a2026u);
    std::uniform_int_distribution<int> value(-1000, 1000);
    double norm = 0.0;
    for (complex_t& amplitude : state) {
        amplitude.real = static_cast<float>(value(engine));
        amplitude.imag = static_cast<float>(value(engine));
        norm += static_cast<double>(amplitude.real) * amplitude.real +
                static_cast<double>(amplitude.imag) * amplitude.imag;
    }
    const float scale = static_cast<float>(1.0 / std::sqrt(norm));
    for (complex_t& amplitude : state) {
        amplitude.real *= scale;
        amplitude.imag *= scale;
    }
    return state;
}

inline void run_qft_cpu(state_t& state, uint32_t qubits) {
    constexpr double pi = 3.14159265358979323846;
    for (uint32_t target = qubits; target-- > 0u;) {
        apply_hadamard_cpu(state, target);
        for (uint32_t control = target; control-- > 0u;) {
            const double angle = pi / static_cast<double>(
                1u << (target - control));
            apply_controlled_phase_cpu(state, control, target,
                                       static_cast<float>(std::cos(angle)),
                                       static_cast<float>(std::sin(angle)));
        }
    }
    for (uint32_t first = 0; first < qubits / 2u; ++first) {
        apply_swap_cpu(state, first, qubits - first - 1u);
    }
}

// A small independent DFT check catches reversed qubit order or phase sign.
inline double qft_dft_error(const state_t& initial, const state_t& actual) {
    constexpr double pi = 3.14159265358979323846;
    const uint32_t size = static_cast<uint32_t>(initial.size());
    const double scale = 1.0 / std::sqrt(static_cast<double>(size));
    double max_error = 0.0;
    for (uint32_t output = 0; output < size; ++output) {
        std::complex<double> expected(0.0, 0.0);
        for (uint32_t input = 0; input < size; ++input) {
            const double angle = 2.0 * pi *
                static_cast<double>(output) * static_cast<double>(input) /
                static_cast<double>(size);
            expected += std::complex<double>(initial[input].real,
                                             initial[input].imag) *
                        std::complex<double>(std::cos(angle), std::sin(angle));
        }
        expected *= scale;
        max_error = std::max(max_error,
            std::abs(expected.real() - actual[output].real));
        max_error = std::max(max_error,
            std::abs(expected.imag() - actual[output].imag));
    }
    return max_error;
}

#endif
