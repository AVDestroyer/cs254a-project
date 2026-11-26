# Running

```bash
./scripts/setup-vortex.sh
```

Downloads the Vortex submodule and builds its toolchain and simulator; run this once before the GPU simulation.

```bash
make cpu-run
```

Builds and runs the optimized single-threaded CPU simulator with the default 15-qubit circuit.

```bash
make vortex-simx
```

Builds and runs the tuned 15-qubit circuit on the 16-core Vortex simulator.

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
