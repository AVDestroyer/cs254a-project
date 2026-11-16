import time
from qiskit import QuantumCircuit
from qiskit_aer import AerSimulator

def benchmark_grover_simulation():
    n = 4
    qc = QuantumCircuit(n, n)

    # 1. Build the circuit (Superposition, Oracle for |0010>, Diffusion, Measurement)
    qc.h(range(n))

    iterations = 2
    for _ in range(iterations):
        # Oracle for |0010>
        qc.x([0, 2, 3])
        qc.h(3)
        qc.mcx([0, 1, 2], 3)
        qc.h(3)
        qc.x([0, 2, 3])

        # Diffusion Operator
        qc.h(range(n))
        qc.x(range(n))
        qc.h(3)
        qc.mcx([0, 1, 2], 3)
        qc.h(3)
        qc.x(range(n))
        qc.h(range(n))

    qc.measure(range(n), range(n))
    
    # 2. Initialize the ideal noiseless simulator
    simulator = AerSimulator()
    
    # 3. Time only the execution phase
    start_time = time.perf_counter()
    
    job = simulator.run(qc, shots=1024)
    result = job.result()
    
    end_time = time.perf_counter()
    
    # Calculate elapsed time in milliseconds
    elapsed_ms = (end_time - start_time) * 1000
    counts = result.get_counts(qc)
    
    return counts, elapsed_ms

if __name__ == "__main__":
    counts, sim_time = benchmark_grover_simulation()
    
    print("--- Benchmark Results ---")
    print(f"Measurement Counts: {counts}")
    print(f"Simulation Time on Computer: {sim_time:.4f} ms")