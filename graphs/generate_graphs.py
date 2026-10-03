import matplotlib.pyplot as plt
import csv
from pathlib import Path

# ============================================================
# MULTI-THREADED IMAGE RETRIEVAL - GRAPH GENERATION
# ============================================================

# Thread counts used in the experiments
threads = [1, 2, 4, 8, 16]
sequential_time = 0.034070
# ------------------------------------------------------------
# Experimental execution times
# 10 runs for each thread count
# ------------------------------------------------------------

static_times = {
    1: [0.028972, 0.045428, 0.031412, 0.028857, 0.029991,
        0.028764, 0.028938, 0.028397, 0.029668, 0.028919],

    2: [0.022195, 0.021340, 0.021532, 0.021674, 0.021400,
        0.040479, 0.023359, 0.021931, 0.022955, 0.021358],

    4: [0.022217, 0.023098, 0.022246, 0.022160, 0.021929,
        0.022537, 0.030267, 0.022024, 0.022514, 0.022550],

    8: [0.020444, 0.022488, 0.022935, 0.023149, 0.022353,
        0.030538, 0.023932, 0.023783, 0.022604, 0.022060],

    16: [0.022968, 0.021375, 0.022297, 0.019204, 0.021979,
         0.022151, 0.023641, 0.021826, 0.022322, 0.022551]
}

threadpool_times = {
    1: [0.092366, 0.088523, 0.087945, 0.085701, 0.086761,
        0.089685, 0.092847, 0.086436, 0.090431, 0.087183],

    2: [0.130699, 0.112797, 0.134406, 0.152223, 0.131377,
        0.157198, 0.103416, 0.204597, 0.111520, 0.171351],

    4: [0.091772, 0.089852, 0.112241, 0.101583, 0.071675,
        0.092983, 0.079796, 0.091543, 0.074587, 0.082590],

    8: [0.115490, 0.118263, 0.103227, 0.107403, 0.102476,
        0.107138, 0.104712, 0.104297, 0.108541, 0.106741],

    16: [0.144692, 0.145413, 0.161916, 0.163975, 0.155983,
         0.166741, 0.192690, 0.163601, 0.146103, 0.149168]
}

# ------------------------------------------------------------
# Calculate averages
# ------------------------------------------------------------

static_avg = [
    sum(static_times[t]) / len(static_times[t])
    for t in threads
]

threadpool_avg = [
    sum(threadpool_times[t]) / len(threadpool_times[t])
    for t in threads
]

# ------------------------------------------------------------
# Calculate speedup
# ------------------------------------------------------------

static_baseline = static_avg[0]
threadpool_baseline = threadpool_avg[0]

static_speedup = [
    static_baseline / t
    for t in static_avg
]

threadpool_speedup = [
    threadpool_baseline / t
    for t in threadpool_avg
]

# ------------------------------------------------------------
# Calculate parallel efficiency
# Efficiency = Speedup / Number of Threads
# ------------------------------------------------------------

static_efficiency = [
    (static_speedup[i] / threads[i]) * 100
    for i in range(len(threads))
]

threadpool_efficiency = [
    (threadpool_speedup[i] / threads[i]) * 100
    for i in range(len(threads))
]

# ------------------------------------------------------------
# Create output directory
# ------------------------------------------------------------

output_dir = Path(".")
output_dir.mkdir(exist_ok=True)
# ============================================================
# GRAPH 0: SEQUENTIAL VS STATIC MULTITHREADING
# ============================================================

plt.figure(figsize=(8, 5))

plt.axhline(
    y=sequential_time,
    linestyle="--",
    label="Sequential Baseline"
)

plt.plot(
    threads,
    static_avg,
    marker="o",
    label="Static Multithreading"
)

plt.xlabel("Number of Threads")
plt.ylabel("Average Execution Time (seconds)")
plt.title("Sequential vs Static Multithreading")
plt.xticks(threads)
plt.grid(True)
plt.legend()
plt.tight_layout()

plt.savefig(
    output_dir / "sequential_vs_multithreading.png",
    dpi=300
)

plt.close()
# ============================================================
# GRAPH 1: EXECUTION TIME
# ============================================================

plt.figure(figsize=(8, 5))

plt.plot(
    threads,
    static_avg,
    marker="o",
    label="Static Multithreading"
)

plt.plot(
    threads,
    threadpool_avg,
    marker="o",
    label="Thread Pool"
)

plt.xlabel("Number of Threads")
plt.ylabel("Average Execution Time (seconds)")
plt.title("Execution Time vs Number of Threads")
plt.xticks(threads)
plt.grid(True)
plt.legend()
plt.tight_layout()

plt.savefig(
    output_dir / "execution_time_comparison.png",
    dpi=300
)

plt.close()

# ============================================================
# GRAPH 2: SPEEDUP
# ============================================================

plt.figure(figsize=(8, 5))

plt.plot(
    threads,
    static_speedup,
    marker="o",
    label="Static Multithreading"
)

plt.plot(
    threads,
    threadpool_speedup,
    marker="o",
    label="Thread Pool"
)

plt.xlabel("Number of Threads")
plt.ylabel("Speedup")
plt.title("Speedup vs Number of Threads")
plt.xticks(threads)
plt.grid(True)
plt.legend()
plt.tight_layout()

plt.savefig(
    output_dir / "speedup_comparison.png",
    dpi=300
)

plt.close()

# ============================================================
# GRAPH 3: PARALLEL EFFICIENCY
# ============================================================

plt.figure(figsize=(8, 5))

plt.plot(
    threads,
    static_efficiency,
    marker="o",
    label="Static Multithreading"
)

plt.plot(
    threads,
    threadpool_efficiency,
    marker="o",
    label="Thread Pool"
)

plt.xlabel("Number of Threads")
plt.ylabel("Parallel Efficiency (%)")
plt.title("Parallel Efficiency vs Number of Threads")
plt.xticks(threads)
plt.grid(True)
plt.legend()
plt.tight_layout()

plt.savefig(
    output_dir / "efficiency_comparison.png",
    dpi=300
)

plt.close()

# ============================================================
# SAVE NUMERICAL RESULTS TO CSV
# ============================================================

with open("performance_summary.csv", "w", newline="") as file:

    writer = csv.writer(file)

    writer.writerow([
        "Threads",
        "Static Avg Time",
        "Threadpool Avg Time",
        "Static Speedup",
        "Threadpool Speedup",
        "Static Efficiency (%)",
        "Threadpool Efficiency (%)"
    ])

    for i, t in enumerate(threads):

        writer.writerow([
            t,
            f"{static_avg[i]:.6f}",
            f"{threadpool_avg[i]:.6f}",
            f"{static_speedup[i]:.4f}",
            f"{threadpool_speedup[i]:.4f}",
            f"{static_efficiency[i]:.2f}",
            f"{threadpool_efficiency[i]:.2f}"
        ])

# ============================================================
# PRINT RESULTS
# ============================================================

print("\n==============================================")
print(" PERFORMANCE SUMMARY")
print("==============================================")

print(
    f"{'Threads':<10}"
    f"{'Static Time':<15}"
    f"{'Pool Time':<15}"
    f"{'Static Speedup':<17}"
    f"{'Pool Speedup':<15}"
)

print("----------------------------------------------")

for i, t in enumerate(threads):

    print(
        f"{t:<10}"
        f"{static_avg[i]:<15.6f}"
        f"{threadpool_avg[i]:<15.6f}"
        f"{static_speedup[i]:<17.4f}"
        f"{threadpool_speedup[i]:<15.4f}"
    )

print("\nGraphs created:")
print("  sequential_vs_multithreading.png")
print("  execution_time_comparison.png")
print("  speedup_comparison.png")
print("  efficiency_comparison.png")
print("  performance_summary.csv")
