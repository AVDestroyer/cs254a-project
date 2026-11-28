#!/usr/bin/env python3
"""Plot the Step 2 geometry sweep from the checked JSONL records."""

import json
import os
from collections import defaultdict
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / ".cache/matplotlib"))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


DATA = ROOT / "reports/step2_measurements.jsonl"
OUTPUT = ROOT / "reports/step2_geometry.svg"


def main():
    grouped = defaultdict(list)
    for line in DATA.read_text().splitlines():
        row = json.loads(line)
        if row["suite"] == "geometry":
            grouped[(row["algorithm"], row["label"])].append(row)

    points = []
    for (algorithm, label), values in grouped.items():
        assert len(values) == 3 and all(value["passed"] for value in values)
        point = dict(values[0])
        point["modeled_us"] = median(value["modeled_device_ms"] for value in values) * 1000
        points.append(point)

    fig, axes = plt.subplots(2, 2, figsize=(11, 7.5), constrained_layout=True)
    for ax, algorithm, title in (
        (axes[0, 0], "hadamard", "Hadamard: 12 qubits"),
        (axes[0, 1], "qft", "QFT: 10 qubits"),
        (axes[1, 0], "grover", "Grover update: 12 qubits, r=64"),
    ):
        candidates = [point for point in points if point["algorithm"] == algorithm]
        if algorithm == "grover":
            candidates = [point for point in candidates if point["reduction_ctas"] == 64]
        for block in (4, 8, 16):
            block_points = sorted((point for point in candidates if point["block"] == block),
                                  key=lambda point: point["work_per_thread"])
            ax.plot([point["work_per_thread"] for point in block_points],
                    [point["modeled_us"] for point in block_points],
                    marker="o", label=f"{block} threads/CTA")
        ax.set_xscale("log", base=2)
        ax.set_xticks((1, 2, 4, 8, 16, 32), labels=("1", "2", "4", "8", "16", "32"))
        ax.set_title(title)
        ax.set_xlabel("Approximate work per thread")
        ax.set_ylabel("Modeled device time (μs)")
        ax.grid(alpha=0.25)
        ax.legend(frameon=False, fontsize=8)

    ax = axes[1, 1]
    reduction = [point for point in points
                 if point["algorithm"] == "grover"
                 and point["block"] == 16
                 and point["work_per_thread"] == 1
                 and "-p" not in point["label"]]
    reduction.sort(key=lambda point: point["reduction_ctas"])
    ax.plot([point["reduction_ctas"] for point in reduction],
            [point["modeled_us"] for point in reduction], marker="o", color="tab:purple")
    ax.set_xscale("log", base=2)
    ax.set_xticks((1, 4, 8, 16, 32, 64), labels=("1", "4", "8", "16", "32", "64"))
    ax.set_title("Grover reduction: b=16, w=1")
    ax.set_xlabel("Partial-reduction CTAs")
    ax.set_ylabel("Modeled device time (μs)")
    ax.grid(alpha=0.25)

    fig.suptitle("Step 2 geometry sweep: lower is better", fontsize=14)
    fig.savefig(OUTPUT)
    fig.savefig(OUTPUT.with_suffix(".png"), dpi=160)
    print(OUTPUT.with_suffix(".png"))


if __name__ == "__main__":
    main()
