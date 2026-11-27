# Running

Algorithm sources live in `algorithms/<name>/cpu` and
`algorithms/<name>/gpu`. Shared state, benchmark, and Vortex build code lives
in `simulator/`.

```bash
./scripts/setup-vortex.sh
```

Downloads the Vortex submodule and builds its toolchain and simulator; run this once before the GPU simulation.

```bash
make hadamard-cpu-run
```

Builds and runs the optimized single-threaded CPU simulator with the default 15-qubit circuit.

```bash
make hadamard-simx
```

Builds and runs the tuned 15-qubit circuit on the 16-core Vortex simulator.
The original `cpu-run` and `vortex-simx` commands remain available as aliases.

## Grover search

```bash
make grover-cpu-run GROVER_QUBITS=15
make grover-simx GROVER_QUBITS=15
```

Grover's search marks basis state 2. The qubit count is configurable from 2 to
20, with 4 as the quick default. The program chooses approximately
`(pi / 4) * sqrt(2^n)` iterations unless `GROVER_ITERATIONS` is set. For 15
qubits, that is 142 iterations over 32,768 amplitudes.

Each GPU iteration evaluates the oracle sign flip and diffusion as inversion
about the mean. A partial reduction, final reduction, and state update use
three kernel launches per iteration. The GPU program checks its state vector
against the CPU reference and reports simulator wall time and simulated device
cycles. Divide the latter by the configured device clock to estimate modeled
device time; simulator wall time is not GPU hardware time.

For a short 15-qubit check, use
`make grover-simx GROVER_QUBITS=15 GROVER_ITERATIONS=1`.

To benchmark the equivalent 15-qubit gate circuit with Qiskit Aer on the CPU:

```bash
python3 -m venv venv
venv/bin/python -m pip install qiskit qiskit-aer
venv/bin/python algorithms/grover/cpu/qiskit_benchmark.py -n 15
```

The Aer script uses the state-vector method and reports both its internal
execution time and the wall time of `run().result()`. It warms up the simulator
before timing four runs. Use `--threads 1` to compare with a single CPU thread.

## Quantum Fourier transform

```bash
make qft-cpu-run QFT_QUBITS=15
make qft-simx QFT_QUBITS=8
```

QFT applies Hadamard and controlled-phase gates, then reverses qubit order.
It starts from a deterministic dense complex state so the phase gates affect
the result. The CPU and GPU paths use the same state layout and gate order.
For up to six qubits, the result is also checked against a direct DFT.

## Benchmark output

Hadamard, Grover, and QFT use a shared host runner. Each GPU run reports the
median of five warmed matched C++ CPU executions, Vortex device cycles and
clock rate, modeled device time, simulator wall time, launch count, amplitude
error, and state norm. A `RESULT_JSON` line contains the same measurements for
scripts. The modeled device time is an estimate from Vortex cycles and its
configured clock; SimX wall time is the host time spent running the simulator.
