# Quantum simulator project plan

## Goal and measurement policy

Port a varied set of quantum algorithms to manually written Vortex GPU kernels and determine when their **modeled device execution time** beats a matched, optimized C++ CPU simulation. Build a reusable simulator framework around those kernels, then study batching, noise, gate fusion, and reuse across noisy shots. Report slower cases as well as speedups.

Use the same initial state, operations, precision policy, and output checks for each CPU/GPU comparison. The primary CPU baseline is the matched C++ implementation; Qiskit Aer is a secondary reference. Report Vortex device cycles and configured clock separately from SimX wall time, which measures the cost of running the simulator on the host. For batched or noisy experiments, compare completion time for the same number of inputs or shots.

## Step 1 — Shared framework and third workload (agreed)

**Implementation status:** complete. The shared runner, CPU references, and
Hadamard, Grover, and QFT GPU workloads are in the repository.

Keep a manually written GPU kernel path for each algorithm. Share the host-side machinery so that an algorithm contributes parameters, an initial state, an optimized C++ reference, a GPU launch sequence, and result checks. Separate Vortex executables are acceptable because the current build creates one kernel binary per algorithm.

Use one state convention throughout: `complex<float>` amplitudes indexed from `0` to `2^n - 1`, with qubit 0 as the least significant index bit. Inputs must be deterministic and reproducible. The common runner owns device setup, buffers, launch recording, state comparison, and the performance report.

Move the existing Hadamard and Grover workloads onto the runner without changing their kernel mathematics. Add quantum Fourier transform (QFT) as the third workload, using manual Hadamard, controlled-phase, and final bit-reversal operations. Use a nontrivial input state so controlled phases affect the result. Give QFT a matched C++ reference; Aer remains an optional secondary cross-check.

Every workload reports CPU median execution time, Vortex cycles and clock rate, kernel launch count, SimX wall time, maximum amplitude error, state norm, and an algorithm-specific result check. The modeled device-time estimate must be labeled as modeled time, rather than measured GPU hardware time.

**Completion check:** Hadamard, Grover, and QFT run at configurable qubit counts through the project build, pass CPU/GPU state comparisons, and produce the same report format.

## Step 2 — Unfused GPU execution and launch submission (agreed)

Keep gate and algorithm mathematics unchanged while comparing three submission paths: individual `vx_enqueue_launch` calls, ordered `vx_enqueue_commands`, and `vx_enqueue_draw`. Build a launch-count microbenchmark and run short and long versions of Hadamard, Grover, and QFT. Record modeled device cycles, configured clock, host queue/event timing, SimX wall time, and launch count separately. The batched APIs reduce host submission work but retain separate device kernel executions; attribute any improvement to the timing component that actually changes.

With submission mode held fixed, tune kernel geometry **per algorithm**. Sweep block size and work per thread for amplitude updates; for Grover, also sweep reduction CTAs and reduction work per thread. Keep the kernels manual and retain a documented configuration for each workload. Compare geometry variants against the same CPU result and output checks. Do not fuse gates or change algorithm mathematics in this step.

**Completion check:** each submission mode and chosen geometry passes the same state checks, and the benchmark report identifies whether dispatch, memory movement, or computation dominates each workload. The fastest validated unfused configuration becomes that algorithm's baseline for Step 3.

## Step 3 — Noise and trajectories (agreed)

Use stochastic statevector trajectories: each shot starts from the workload's input state, samples errors at explicit noise sites, runs the corresponding GPU noise operations, and contributes a measurement outcome. Start with configurable single- and two-qubit depolarizing noise. A reusable GPU kernel applies sampled Pauli errors while each algorithm retains its manual unitary kernels. Keep readout bit flips separate from state evolution.

Generate noise choices reproducibly from a seed, shot ID, and noise-site ID. Feed the same choices to the CPU reference and GPU for trajectory-by-trajectory state comparisons. Run shots sequentially in this step, then use this implementation as the unfused, unbatched many-shot baseline for later work. Report total modeled device time for a fixed shot count, noise-related launch count, and output accuracy.

Noise sites are explicit workload metadata. Hadamard and QFT use gate-level sites. Specialized Grover uses a clearly labeled iteration-level noise model because its oracle and diffuser are implemented as a mathematical macro; do not compare that model as if it were gate-level Aer noise.

**Completion check:** zero noise reproduces Step 2; Pauli trajectories preserve norm; CPU and GPU states agree for identical sampled errors; ensemble output distributions agree within sampling error.

## Step 4 — Gate fusion (agreed)

Build bounded fused groups for gate sequences exposed by the workloads: small-support Hadamard and other unitary groups, plus runs of diagonal controlled-phase gates where useful. Compile each candidate once and retain the original gate sequence as a fallback. Do not try to fuse across Grover's global reduction; its existing specialized macro remains a measured comparison case.

Evaluate **two versions**. In the noiseless version, skip noise scheduling and apply the fused groups directly. In the noisy version, sample all gate-level noise choices as in Step 3. If a group has no sampled error at an internal noise site, use its prebuilt fused kernel; otherwise execute the original gates and noise operations for that group. Apply any error at the group boundary in its original position. This keeps the noisy trajectory exact without compiling a new operator for every shot.

Compare fused and unfused runs with identical initial states and, for noisy runs, identical noise schedules. Report modeled device time, launch count, the fraction of groups using the fast path, and host planning cost at zero, low, and higher error rates. Retain the fused-operator description for Step 5's batched execution path.

**Completion check:** noiseless and noisy fused outputs match their unfused references within the established tolerance; the report identifies when saved statevector passes outweigh extra fused-kernel arithmetic and when noise forces fallback.

## Step 5 — Batched simulation and BQSim techniques (agreed)

Use a fixed batch of distinct input statevectors undergoing the same circuit. Compare three execution paths on that exact workload: repeated single-state GPU runs from Step 4, batched versions of the manual kernels, and a general ELL sparse matrix–matrix GPU kernel applied to Step 4's fused operators. Compare all three with a matched C++ CPU batch. Store the state matrix with batch index as the contiguous dimension, and include layout-conversion and data-movement costs in the complete-workload result.

Implement an actual decision-diagram (DD) to ELL compilation path as a required experiment. Start with direct ELL construction to validate the ELL kernel, then build/fuse gate operators in DD form and materialize their ELL rows. Measure DD construction, DD-to-ELL conversion, and repeated batch execution separately, then report their combined time as the number of batches grows. Cap ELL materialization by available memory and maximum nonzeros per row; fall back to the manual kernel path for unsuitable operators, including dense Grover diffusion.

After the noiseless batch experiment works, group Step 4 trajectories that share a no-error fused operator and run them through the ELL path. Keep trajectories with internal sampled errors on their exact fallback path. Count grouping, state movement, and fallback costs rather than assuming all noisy shots can share one operator.

**Completion check:** all three GPU paths agree with the CPU result for each input state; the benchmark identifies batch sizes and operator structures where DD-to-ELL compilation is amortized and where ELL loses to manual kernels.

## Step 6 — TQSim-style shot reuse (agreed)

Use Step 3's noisy trajectories and Step 5's batched segment execution. First implement a fixed tree: partition a circuit at explicit operation/noise boundaries, execute a noisy prefix, save its statevector on the device, and branch independently sampled suffix continuations. Count segment executions, state copies, modeled device time, and peak device memory. Add exact reuse of identical sampled prefixes as an accuracy-preserving control.

Implement TQSim-style approximate tree sampling as a required experiment. Choose circuit cuts and branch counts using measured state-copy cost, segment execution cost, noise rates, available memory, and requested outcome count. Descendant shots intentionally share sampled ancestors, so evaluate the resulting correlation and accuracy loss across multiple seeds rather than treating the leaves as independent trajectories.

Compare four execution modes at the same outcome count: independent trajectories, independent batched trajectories, tree reuse without batching, and tree reuse with batching of nodes needing the same next segment. Report modeled device time, copy and memory overhead, output-distribution and observable differences versus independent trajectories, and runtime–accuracy tradeoffs. Use gate-level noisy QFT as the primary benchmark and label Grover's iteration-level noise model separately.

**Completion check:** fixed and adaptive trees produce the requested number of outcomes, exact-prefix reuse matches the independent-shot result, and approximate tree sampling yields a measured speed–accuracy curve with its benefit separated from batching.

## Step 7 — Complex matrix multiplication (agreed)

Use a separate Vortex build with the tensor core unit (TCU) enabled; the project's current default configuration leaves it disabled. Validate the TCU path with Vortex's existing SGEMM example before using it for quantum states. Compare every scalar, ELL, manual, and MMA result on the **same TCU-enabled hardware configuration**, and label earlier results from the original configuration separately.

Target dense fused local operators applied to batched statevectors. Pack real and imaginary components into real matrices and first implement complex multiplication using four real FP32 MMA operations. Measure packing, execution, and unpacking separately and together. Consider a three-MMA formulation or lower-precision inputs only after the FP32 path passes the state checks, and report any accuracy cost.

Sweep fused-operator size and batch size to find a crossover against scalar dense multiplication, the Step 5 ELL kernel, and manual kernels. Keep diagonal phases, sparse operators, and Grover's rank-one diffusion on their existing paths unless measured evidence favors MMA. Report modeled cycles, data movement, tensor utilization, and output error; include a dense local-operator benchmark if the existing algorithms do not provide a suitable tile.

**Completion check:** the MMA path matches the reference within its stated precision tolerance and the study identifies where it wins or confirms that packing and tile costs outweigh its benefit on the project's workloads.
