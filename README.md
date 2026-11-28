# Quantum state-vector simulator on Vortex

This project implements three quantum circuits as manually written Vortex GPU
kernels: a layer of Hadamards, Grover search, and the quantum Fourier transform
(QFT). Each has a matched C++ CPU implementation. The GPU programs run in
**Vortex SimX**, a simulator of the GPU; the reported GPU time is derived from
simulated device cycles, not measured on a physical GPU.

The current goal is to compare the same computation on the C++ CPU reference
and the Vortex kernel, then study how kernel submission and thread geometry
affect performance. [The project plan](PROJECT_PLAN.md) describes the later
noise, fusion, and batching work.

## Run a CPU/GPU comparison

CPU programs need only a C++ compiler. For example, run a 12-qubit Hadamard circuit:

```bash
make hadamard-cpu-run HADAMARD_QUBITS=12
```

Before the first SimX run, install and build the Vortex submodule and toolchain:

```bash
./scripts/setup-vortex.sh
```

Then run the **same 12-qubit circuit** on the modeled GPU:

```bash
make hadamard-simx HADAMARD_QUBITS=12
```

The SimX command also times the matched C++ reference, so the separate CPU
command is optional. It is useful when you want the CPU result by itself. The
GPU program compares both final state vectors and prints `PASSED` only if its
checks succeed.

| Circuit | CPU command | SimX command | Default qubits |
| --- | --- | --- | ---: |
| Hadamard layer | `make hadamard-cpu-run` | `make hadamard-simx` | 15 |
| Grover search | `make grover-cpu-run` | `make grover-simx` | 4 |
| QFT | `make qft-cpu-run` | `make qft-simx` | 4 |

Set `HADAMARD_QUBITS`, `GROVER_QUBITS`, or `QFT_QUBITS` on both corresponding commands. For example:

```bash
make qft-cpu-run QFT_QUBITS=10
make qft-simx QFT_QUBITS=10
```

The `*-simx` targets build the Vortex host program and kernel before running them. `*-build` builds without running. `*-rtlsim` runs the RTL simulator instead of SimX.

## What each circuit computes

- **Hadamard:** Starts in `|0...0⟩` and applies one Hadamard to each qubit. The GPU launches one kernel per gate. `HADAMARD_LAUNCHES` repeats the gate sequence on a fixed-size state for the launch-count experiment; leave it unset for the usual circuit.
- **Grover:** Starts in the uniform superposition and marks basis state 2. Its GPU implementation combines the oracle sign flip with a partial sum, computes the global mean, and applies diffusion: **three kernel launches per iteration**. `GROVER_ITERATIONS` overrides the usual `floor((π/4)√(2^n))` iteration count. A full 15-qubit search uses 142 iterations and is slow to simulate in SimX. For a short 15-qubit correctness check, run `make grover-simx GROVER_QUBITS=15 GROVER_ITERATIONS=1`.
- **QFT:** Applies Hadamards, controlled phases, and final qubit-order swaps to
  a deterministic dense complex state. Each operation has its own GPU launch.
  For six or fewer qubits, the result is also checked against a direct DFT.

All three use complex single-precision amplitudes. State index bit 0 is qubit 0. The CPU and GPU paths for each circuit use the same input state and operation order.

## Which timing number to compare

A SimX run prints several times because they measure different things:

| Output field | Meaning |
| --- | --- |
| `CPU median execution time` | Median of five warmed executions of the matched C++ CPU computation. |
| `Vortex device cycles` | Simulated device cycles for the GPU run. |
| `Modeled device time` | Device cycles divided by the reported Vortex clock rate. **Use this for the CPU versus GPU comparison.** |
| `Simulator wall time` | Time the host computer spent running SimX and reading back the result. It is not GPU execution time. |
| `Host preparation/enqueue` and `Queue wait/execution` | Submission and SimX queue diagnostics. Queue execution is host-side simulator work, not modeled device time. |
| `Kernel launches`, `Maximum amplitude error`, `Total probability` | Work count and correctness checks. |

For example, at a 400 MHz modeled clock, 40,000 device cycles mean `0.100 ms` of modeled device time. SimX may take seconds on the host to simulate those cycles. A `RESULT_JSON` line repeats the measurements for benchmark scripts.

The matched C++ implementation is the primary CPU baseline. The optional Qiskit Aer script below implements Grover as a gate circuit, while this project's CPU and GPU Grover implementations use the specialized mean-reflection update. Aer is useful as a separate reference, but its timing is not a like-for-like speed comparison with the specialized kernel.

## Tune the GPU launch geometry

These Makefile variables change execution geometry without changing the circuit:

| Variable | Effect |
| --- | --- |
| `<ALGORITHM>_BLOCK` | Threads per CTA. On the default 4-warp × 4-thread core, use a power of two no larger than 16. |
| `<ALGORITHM>_WORK` | Approximate pairs or amplitudes handled by each thread. Increasing it reduces the number of CTAs; each thread uses a grid-stride loop. |
| `<ALGORITHM>_SUBMISSION` | `individual`, `commands`, or `draw`. All three execute the same ordered kernel launches. The latter two submit them as a batch; they do not fuse gates. |
| `GROVER_REDUCTION_CTAS` | Exact number of CTAs in Grover's partial reduction. |
| `GROVER_REDUCTION_WORK` | Alternative way to request reduction work per thread. Do not set it together with `GROVER_REDUCTION_CTAS`. |

Replace `<ALGORITHM>` with `HADAMARD`, `GROVER`, or `QFT`. For each pass, the
grid has `ceil(items / (BLOCK × WORK))` CTAs. Threads loop with a stride of
`grid × BLOCK`; `WORK` is a target, not a strict per-thread count. Hadamard and
Grover updates count amplitude pairs. QFT counts pairs for Hadamards and
amplitudes for phases and swaps.

The best tested settings for the Step 2 benchmark sizes are:

```bash
make hadamard-simx HADAMARD_QUBITS=12 HADAMARD_BLOCK=8 HADAMARD_WORK=8
make qft-simx QFT_QUBITS=10 QFT_BLOCK=16 QFT_WORK=4
make grover-simx GROVER_QUBITS=12 GROVER_ITERATIONS=1 GROVER_BLOCK=16 GROVER_WORK=8 GROVER_REDUCTION_CTAS=16
```

These settings reduced modeled cycles relative to the original geometry, but the modeled GPU time is still slower than the matched CPU reference at those sizes. The best geometry can change with qubit count and hardware configuration. See the [Step 2 report](reports/step2_geometry.md) for every tested geometry, submission-mode results, and the larger-qubit checks.

To rerun the sweep (three checked runs per configuration):

```bash
python3 scripts/benchmark_step2.py --repeats 3
```

The script writes [raw measurements](reports/step2_measurements.jsonl). The [plot script](scripts/plot_step2.py) regenerates the report chart from those records; it requires Matplotlib.

## Optional Qiskit Aer reference for Grover

```bash
python3 -m venv venv
venv/bin/python -m pip install qiskit qiskit-aer
venv/bin/python algorithms/grover/cpu/qiskit_benchmark.py -n 15 --threads 1
```

The script reports Aer execution time and `run().result()` wall time for the gate-level circuit. Use `-i` to match a chosen Grover iteration count.

## Code layout

- `algorithms/<name>/cpu/`: matched C++ reference; Grover also has the optional Aer script.
- `algorithms/<name>/gpu/`: Vortex host launcher and device kernel.
- `simulator/`: shared state type, CPU operations, benchmark output, Vortex runner, and build rules.
- `scripts/`: Vortex setup and benchmark scripts.
- `reports/`: Step 2 results and raw measurements.
