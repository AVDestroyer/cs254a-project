#ifndef GROVER_CPU_REFERENCE_H
#define GROVER_CPU_REFERENCE_H

#include "../../../simulator/state.h"

#include <cstdint>
#include <vector>

// The marked-state phase flip followed by inversion about the mean.
inline void run_grover_cpu(std::vector<complex_t>& state, uint32_t target,
                           uint32_t iterations) {
    const double inverse_size = 1.0 / static_cast<double>(state.size());
    for (uint32_t iter = 0; iter < iterations; ++iter) {
        state[target].real = -state[target].real;
        state[target].imag = -state[target].imag;

        double real_sum = 0.0;
        double imag_sum = 0.0;
        for (const complex_t& amplitude : state) {
            real_sum += amplitude.real;
            imag_sum += amplitude.imag;
        }
        const float twice_real_mean = static_cast<float>(2.0 * real_sum * inverse_size);
        const float twice_imag_mean = static_cast<float>(2.0 * imag_sum * inverse_size);
        for (complex_t& amplitude : state) {
            amplitude.real = twice_real_mean - amplitude.real;
            amplitude.imag = twice_imag_mean - amplitude.imag;
        }
    }
}

#endif
