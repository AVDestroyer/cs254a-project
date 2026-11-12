#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Complex = std::complex<float>;

void apply_hadamard(std::vector<Complex>& state, std::size_t target_qubit) {
    const std::size_t pair_distance = std::size_t{1} << target_qubit;
    const std::size_t block_size = pair_distance * 2;
    const float inverse_sqrt_two = 1.0f / std::sqrt(2.0f);

    for (std::size_t block = 0; block < state.size(); block += block_size) {
        for (std::size_t offset = 0; offset < pair_distance; ++offset) {
            const std::size_t zero_index = block + offset;
            const std::size_t one_index = zero_index + pair_distance;
            const Complex zero_amplitude = state[zero_index];
            const Complex one_amplitude = state[one_index];

            state[zero_index] =
                (zero_amplitude + one_amplitude) * inverse_sqrt_two;
            state[one_index] =
                (zero_amplitude - one_amplitude) * inverse_sqrt_two;
        }
    }
}

std::string binary_label(std::size_t value, std::size_t num_qubits) {
    std::string label(num_qubits, '0');
    for (std::size_t bit = 0; bit < num_qubits; ++bit) {
        label[num_qubits - bit - 1] = ((value >> bit) & 1U) ? '1' : '0';
    }
    return label;
}

int main(int argc, char* argv[]) {
    try {
        const std::size_t num_qubits =
            argc > 1 ? std::stoul(argv[1]) : std::size_t{3};

        if (num_qubits == 0 ||
            num_qubits >= std::numeric_limits<std::size_t>::digits) {
            throw std::invalid_argument("invalid number of qubits");
        }

        const std::size_t state_size = std::size_t{1} << num_qubits;
        std::vector<Complex> state(state_size, Complex{0.0f, 0.0f});
        state[0] = Complex{1.0f, 0.0f};

        const auto start = std::chrono::steady_clock::now();
        for (std::size_t qubit = 0; qubit < num_qubits; ++qubit) {
            apply_hadamard(state, qubit);
        }
        const auto stop = std::chrono::steady_clock::now();

        std::cout << std::fixed << std::setprecision(7);
        for (std::size_t basis_state = 0; basis_state < state_size;
             ++basis_state) {
            std::cout << '|' << binary_label(basis_state, num_qubits) << ">: "
                      << state[basis_state].real();
            if (state[basis_state].imag() >= 0.0f) {
                std::cout << '+';
            }
            std::cout << state[basis_state].imag() << "i\n";
        }

        const std::chrono::duration<double, std::milli> elapsed = stop - start;
        std::cout << "Gate execution time: " << elapsed.count() << " ms\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        std::cerr << "Usage: " << argv[0] << " [num_qubits]\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
