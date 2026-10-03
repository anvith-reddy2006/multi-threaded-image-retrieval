import matplotlib.pyplot as plt
import csv
import sys
import statistics
from pathlib import Path
from collections import defaultdict

if len(sys.argv) < 2:
    print(f"Usage: python {sys.argv[0]} [benchmark_results.csv]")
    sys.exit(1)

csv_file = sys.argv[1]

seq_times = defaultdict(list)
static_times = defaultdict(lambda: defaultdict(list))
pool_times = defaultdict(lambda: defaultdict(list))
threads_set = set()

with open(csv_file, 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        method = row['method']
        t = int(row['threads'])
        t_idx = float(row['t_index'])
        t_srch = float(row['t_search'])
        t_tot = float(row['t_total'])
        
        if method == 'sequential':
            seq_times['t_index'].append(t_idx)
            seq_times['t_search'].append(t_srch)
            seq_times['t_total'].append(t_tot)
        elif method == 'static':
            threads_set.add(t)
            static_times['t_index'][t].append(t_idx)
            static_times['t_search'][t].append(t_srch)
            static_times['t_total'][t].append(t_tot)
        elif method == 'pool':
            threads_set.add(t)
            pool_times['t_index'][t].append(t_idx)
            pool_times['t_search'][t].append(t_srch)
            pool_times['t_total'][t].append(t_tot)

threads = sorted(list(threads_set))

seq_med_idx = statistics.median(seq_times['t_index'])
seq_med_srch = statistics.median(seq_times['t_search'])
seq_med_tot = statistics.median(seq_times['t_total'])

static_med_idx = [statistics.median(static_times['t_index'][t]) for t in threads]
static_med_srch = [statistics.median(static_times['t_search'][t]) for t in threads]
static_med_tot = [statistics.median(static_times['t_total'][t]) for t in threads]

pool_med_idx = [statistics.median(pool_times['t_index'][t]) for t in threads]
pool_med_srch = [statistics.median(pool_times['t_search'][t]) for t in threads]
pool_med_tot = [statistics.median(pool_times['t_total'][t]) for t in threads]

static_speedup = [seq_med_tot / t for t in static_med_tot]
pool_speedup = [seq_med_tot / t for t in pool_med_tot]

static_eff = [(s / t) * 100 for s, t in zip(static_speedup, threads)]
pool_eff = [(s / t) * 100 for s, t in zip(pool_speedup, threads)]

output_dir = Path(".")
output_dir.mkdir(exist_ok=True)

def plot_metric(metric_name, seq_val, static_vals, pool_vals, title, filename, ylabel):
    plt.figure(figsize=(8, 5))
    plt.axhline(y=seq_val, linestyle="--", label="Sequential Baseline")
    plt.plot(threads, static_vals, marker="o", label="Static MT")
    plt.plot(threads, pool_vals, marker="o", label="Thread Pool")
    plt.xlabel("Number of Threads")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.xticks(threads)
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_dir / filename, dpi=300)
    plt.close()

plot_metric("t_index", seq_med_idx, static_med_idx, pool_med_idx, "Indexing Time (PNG Decode)", "t_index.png", "Time (s)")
plot_metric("t_search", seq_med_srch, static_med_srch, pool_med_srch, "Search Time (Distance Calc)", "t_search.png", "Time (s)")
plot_metric("t_total", seq_med_tot, static_med_tot, pool_med_tot, "Total Execution Time", "t_total.png", "Time (s)")

plt.figure(figsize=(8, 5))
plt.plot(threads, static_speedup, marker="o", label="Static MT")
plt.plot(threads, pool_speedup, marker="o", label="Thread Pool")
plt.plot(threads, threads, linestyle="--", color="black", label="Ideal Speedup")
plt.xlabel("Number of Threads")
plt.ylabel("Speedup vs Sequential")
plt.title("Total Speedup")
plt.xticks(threads)
plt.grid(True)
plt.legend()
plt.tight_layout()
plt.savefig(output_dir / "speedup.png", dpi=300)
plt.close()

plt.figure(figsize=(8, 5))
plt.plot(threads, static_eff, marker="o", label="Static MT")
plt.plot(threads, pool_eff, marker="o", label="Thread Pool")
plt.xlabel("Number of Threads")
plt.ylabel("Efficiency (%)")
plt.title("Parallel Efficiency")
plt.xticks(threads)
plt.grid(True)
plt.legend()
plt.tight_layout()
plt.savefig(output_dir / "efficiency.png", dpi=300)
plt.close()

print("Graphs created successfully using medians.")
