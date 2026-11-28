#!/usr/bin/env python3
"""Repeat Step 2 SimX sweeps and save one JSON record per checked run."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]
APP_ROOT = ROOT / "third_party/vortex/build/tests/regression"
RUNTIME = ROOT / "third_party/vortex/build/sw/runtime"


def cases(suite):
    if suite == "profile":
        for algorithm, qubits, extra, label, tuning in (
            ("hadamard", 12, [], "baseline", ["-b16", "-w1"]),
            ("hadamard", 12, [], "tuned", ["-b8", "-w8"]),
            ("qft", 10, [], "baseline", ["-b16", "-w1"]),
            ("qft", 10, [], "tuned", ["-b16", "-w4"]),
            ("grover", 12, ["-i1"], "baseline", ["-b16", "-w1", "-r64"]),
            ("grover", 12, ["-i1"], "tuned", ["-b16", "-w8", "-r16"]),
        ):
            yield {"suite": "profile", "algorithm": algorithm,
                   "label": f"n{qubits}-{label}",
                   "args": [f"-n{qubits}", *extra, *tuning, "-sindividual"]}

    if suite == "validation":
        for algorithm, qubits, extra, label, tuning in (
            ("hadamard", 15, [], "baseline", ["-b16", "-w1"]),
            ("hadamard", 15, [], "n12-best", ["-b8", "-w8"]),
            ("qft", 12, [], "baseline", ["-b16", "-w1"]),
            ("qft", 12, [], "n10-best", ["-b16", "-w4"]),
            ("grover", 15, ["-i1"], "baseline", ["-b16", "-w1", "-r64"]),
            ("grover", 15, ["-i1"], "n12-best", ["-b16", "-w8", "-r16"]),
            ("grover", 15, ["-i1"], "r32", ["-b16", "-w8", "-r32"]),
            ("grover", 15, ["-i1"], "r64", ["-b16", "-w8", "-r64"]),
        ):
            yield {"suite": "validation", "algorithm": algorithm,
                   "label": f"n{qubits}-{label}",
                   "args": [f"-n{qubits}", *extra, *tuning, "-sindividual"]}

    if suite in ("all", "launches"):
        for count in (1, 4, 16, 64):
            for mode in ("individual", "commands", "draw"):
                yield {
                    "suite": "launches",
                    "algorithm": "hadamard",
                    "label": f"n4-l{count}-{mode}",
                    "args": ["-n4", f"-l{count}", "-b8", "-w1", f"-s{mode}"],
                }

    if suite in ("all", "submission"):
        for algorithm, qubits, extra, block in (
            ("hadamard", 4, [], 8),
            ("hadamard", 12, [], 16),
            ("grover", 4, ["-i3"], 8),
            ("grover", 8, ["-i12"], 16),
            ("qft", 4, [], 8),
            ("qft", 10, [], 16),
        ):
            for mode in ("individual", "commands", "draw"):
                yield {
                    "suite": "submission",
                    "algorithm": algorithm,
                    "label": f"n{qubits}-{mode}",
                    "args": [f"-n{qubits}", *extra, f"-b{block}", "-w1", f"-s{mode}"],
                }

    if suite in ("all", "geometry"):
        for algorithm, qubits, extra in (
            ("hadamard", 12, []),
            ("qft", 10, []),
            ("grover", 12, ["-i1"]),
        ):
            settings = [(4, 1, []), (8, 1, []), (16, 1, []),
                        (16, 2, []), (16, 4, []), (16, 8, []),
                        (8, 4, []), (4, 4, [])]
            if algorithm == "grover":
                settings += [(16, 1, ["-r1"]), (16, 1, ["-r4"]),
                             (16, 1, ["-r16"]), (16, 1, ["-p4"]),
                             (16, 1, ["-p16"])]
            for block, work, reduction in settings:
                label = f"n{qubits}-b{block}-w{work}"
                if reduction:
                    label += reduction[0]
                yield {
                    "suite": "geometry",
                    "algorithm": algorithm,
                    "label": label,
                    "args": [f"-n{qubits}", *extra, f"-b{block}", f"-w{work}",
                             *reduction, "-sindividual"],
                }

    if suite in ("all", "extended"):
        for algorithm, qubits, extra in (
            ("hadamard", 12, []),
            ("qft", 10, []),
            ("grover", 12, ["-i1"]),
        ):
            settings = [(16, 16, []), (16, 32, []),
                        (8, 8, []), (8, 16, [])]
            if algorithm == "grover":
                settings += [(16, 1, [f"-r{ctas}"])
                             for ctas in (8, 12, 20, 24, 32)]
                settings += [(16, work, ["-r16"])
                             for work in (2, 4, 8, 16)]
                settings += [(8, 8, ["-r16"]), (8, 4, ["-r32"]),
                             (8, 8, ["-r32"])]
            for block, work, reduction in settings:
                label = f"n{qubits}-b{block}-w{work}"
                if reduction:
                    label += reduction[0]
                yield {
                    "suite": "geometry",
                    "algorithm": algorithm,
                    "label": label,
                    "args": [f"-n{qubits}", *extra, f"-b{block}",
                             f"-w{work}", *reduction, "-sindividual"],
                }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("all", "submission", "geometry",
                                            "extended", "launches", "validation", "profile"),
                        default="all")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--algorithm", choices=("hadamard", "grover", "qft"))
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "reports/step2_measurements.jsonl")
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")

    if not args.no_build:
        subprocess.run(["make", "hadamard-simx", "HADAMARD_QUBITS=4"],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["make", "grover-build", "qft-build"],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)

    env = os.environ.copy()
    env["VORTEX_DRIVER"] = "simx"
    env["LD_LIBRARY_PATH"] = str(RUNTIME) + ":" + env.get("LD_LIBRARY_PATH", "")
    if args.suite == "profile":
        env["VORTEX_PROFILING"] = "1"
    all_cases = [case for case in cases(args.suite)
                 if args.algorithm is None or case["algorithm"] == args.algorithm]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    failures = 0
    with args.output.open("w") as destination:
        for case_index, case in enumerate(all_cases, 1):
            for repeat in range(args.repeats):
                algorithm = case["algorithm"]
                app_dir = APP_ROOT / algorithm
                started = time.perf_counter()
                process = subprocess.run([str(app_dir / algorithm), *case["args"]],
                                         cwd=app_dir, env=env, text=True,
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, timeout=180)
                elapsed_ms = (time.perf_counter() - started) * 1000.0
                result_line = next((line.removeprefix("RESULT_JSON: ")
                                    for line in process.stdout.splitlines()
                                    if line.startswith("RESULT_JSON: ")), None)
                if result_line is None:
                    raise RuntimeError(f"{case['label']} produced no RESULT_JSON:\n"
                                       + process.stdout[-3000:])
                result = json.loads(result_line)
                record = {**case, "repeat": repeat + 1,
                          "process_wall_ms": elapsed_ms, **result}
                if args.suite == "profile":
                    record["perf_lines"] = [line for line in process.stdout.splitlines()
                                            if line.startswith("PERF: ")
                                            and not line.startswith("PERF: core")]
                destination.write(json.dumps(record, sort_keys=True) + "\n")
                destination.flush()
                if process.returncode != 0 or not result["passed"]:
                    failures += 1
                    print(f"FAILED {case['label']} repeat {repeat + 1}:\n"
                          + process.stdout[-3000:], flush=True)
                else:
                    print(f"[{case_index}/{len(all_cases)}] {case['label']} "
                          f"repeat {repeat + 1}: {result['device_cycles']} cycles, "
                          f"{elapsed_ms / 1000.0:.1f}s host", flush=True)
    print(f"Saved {len(all_cases) * args.repeats} runs to {args.output}; "
          f"failures={failures}", flush=True)
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
