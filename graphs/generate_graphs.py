import matplotlib.pyplot as plt
import csv
import sys
from pathlib import Path

# ============================================================
# MULTI-THREADED IMAGE RETRIEVAL - GRAPH GENERATION
# ============================================================

if len(sys.argv) < 2:
    print(f"Usage: python {sys.argv[0]} [benchmark_results.csv]")
    sys.exit(1)

csv_file = sys.argv[1]

threads_set = set()
sequential_times = []
static_times_dict = {}
pool_times_dict = {}

with open(csv_file, 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        method = row['method']
        t = int(row['threads'])
        t_total = float(row['t_total'])
        
        if method == 'sequential':
            sequential_times.append(t_total)
        elif method == 'static':
            threads_set.add(t)
            if t not in static_times_dict:
                static_times_dict[t] = []
            static_times_dict[t].append(t_total)
        elif method == 'pool':
            threads_set.add(t)
            if t not in pool_times_dict:
                pool_times_dict[t] = []
            pool_times_dict[t].append(t_total)

threads = sorted(list(threads_set))
if sequential_times:
    sequential_time = sum(sequential_times) / len(sequential_times)
else:
    print("Error: No sequential times found in CSV")
    sys.exit(1)

# ------------------------------------------------------------
# Calculate averages
# ------------------------------------------------------------

static_avg = [
    sum(static_times_dict[t]) / len(static_times_dict[t])
    for t in threads
]

threadpool_avg = [
    sum(pool_times_dict[t]) / len(pool_times_dict[t])
    for t in threads
]

# ------------------------------------------------------------
# Calculate speedup (AGAINST SEQUENTIAL)
# ------------------------------------------------------------

static_speedup = [
    sequential_time / t
    for t in static_avg
]

threadpool_speedup = [
    sequential_time / t
    for t in threadpool_avg
]

ideal_speedup = [t for t in threads]

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

plt.plot(
    threads,
    ideal_speedup,
    linestyle="--",
    color="black",
    label="Ideal Speedup"
)

plt.xlabel("Number of Threads")
plt.ylabel("Speedup vs Sequential")
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

print("\nGraphs created:")
print("  sequential_vs_multithreading.png")
print("  execution_time_comparison.png")
print("  speedup_comparison.png")
