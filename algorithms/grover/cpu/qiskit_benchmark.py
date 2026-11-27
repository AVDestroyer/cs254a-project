"""Benchmark the same single-target Grover search with Qiskit Aer."""

import argparse
import math
import statistics
import time

from qiskit import QuantumCircuit
from qiskit_aer import AerSimulator


def build_circuit(num_qubits: int, iterations: int, target: int = 2) -> QuantumCircuit:
    """Mark ``target`` and apply the standard Grover diffuser."""
    circuit = QuantumCircuit(num_qubits)
    controls = list(range(num_qubits - 1))
    last = num_qubits - 1
    zero_bits = [bit for bit in range(num_qubits) if not (target >> bit) & 1]

    circuit.h(range(num_qubits))
    for _ in range(iterations):
        # Convert the marked basis state to all ones, then apply a phase flip.
        circuit.x(zero_bits)
        circuit.h(last)
        circuit.mcx(controls, last)
        circuit.h(last)
        circuit.x(zero_bits)

        # Inversion about the mean, up to an irrelevant global phase.
        circuit.h(range(num_qubits))
        circuit.x(range(num_qubits))
        circuit.h(last)
        circuit.mcx(controls, last)
        circuit.h(last)
        circuit.x(range(num_qubits))
        circuit.h(range(num_qubits))

    circuit.save_statevector()
    return circuit


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-n", "--qubits", type=int, default=4)
    parser.add_argument("-i", "--iterations", type=int)
    parser.add_argument("--threads", type=int, default=0,
                        help="Aer CPU threads; 0 uses Aer's default")
    parser.add_argument("--runs", type=int, default=4)
    args = parser.parse_args()

    if not 2 <= args.qubits <= 20:
        parser.error("qubits must be between 2 and 20")
    if args.runs < 1 or args.threads < 0:
        parser.error("runs must be positive and threads must be nonnegative")

    state_size = 1 << args.qubits
    iterations = args.iterations
    if iterations is None:
        iterations = math.floor(math.pi / 4 * math.sqrt(state_size))
    if iterations < 1:
        parser.error("iterations must be positive")

    circuit = build_circuit(args.qubits, iterations)
    options = {"method": "statevector"}
    if args.threads:
        options["max_parallel_threads"] = args.threads
    simulator = AerSimulator(**options)

    # Warm up the simulator; circuit construction and warmup are not timed.
    warmup = simulator.run(circuit, shots=1).result()
    if not warmup.success:
        raise RuntimeError("Aer warmup failed")

    wall_times = []
    aer_times = []
    result = None
    for _ in range(args.runs):
        start = time.perf_counter()
        result = simulator.run(circuit, shots=1).result()
        wall_times.append(time.perf_counter() - start)
        if not result.success:
            raise RuntimeError("Aer simulation failed")
        aer_times.append(result.results[0].metadata["time_taken"])

    state = result.data(0)["statevector"].data
    target_probability = abs(state[2]) ** 2
    theta = math.asin(1 / math.sqrt(state_size))
    predicted_probability = math.sin((2 * iterations + 1) * theta) ** 2
    metadata = result.results[0].metadata

    print(f"Grover search: {args.qubits} qubits, {iterations} iterations, target index 2")
    print(f"Aer method: {metadata['method']}, device: {metadata['device']}")
    print(f"Aer state-update threads: {metadata.get('parallel_state_update')}")
    print(f"Target probability: {target_probability:.9%}"
          f" (predicted {predicted_probability:.9%})")
    print(f"Median Aer execution time: {statistics.median(aer_times):.6f} s")
    print(f"Median run()/result() wall time: {statistics.median(wall_times):.6f} s")
    print(f"Runs: {args.runs}; wall times: "
          + ", ".join(f"{value:.6f}" for value in wall_times))


if __name__ == "__main__":
    main()
