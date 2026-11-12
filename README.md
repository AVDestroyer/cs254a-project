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
