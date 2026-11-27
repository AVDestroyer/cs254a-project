#ifndef QUANTUM_SIMULATOR_STATE_H
#define QUANTUM_SIMULATOR_STATE_H

// The state-vector layout is shared by host references and Vortex kernels.
// Qubit 0 is the least-significant bit of the amplitude index.
struct complex_t {
    float real;
    float imag;
};

#endif
