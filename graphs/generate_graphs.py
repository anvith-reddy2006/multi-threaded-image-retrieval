#!/usr/bin/env python3
"""
Graphs for the final report (item 6 of the remaining work).
Reads  benchmark_results.csv  (from ./benchmark.sh)  and  scenario_times.csv  (from ./run_scenarios.sh).
Writes PNGs to graphs/output/.

Usage:  python3 graphs/generate_graphs.py [--threads 4]
        --threads N : thread count used for the "vs Implementation" bar charts (default 4)

Graphs:
  1 exec_time_vs_implementation.png   Execution Time vs Implementation
  2 speedup_vs_implementation.png     Speedup vs Implementation
  3 overhead_vs_implementation.png    Overhead vs Implementation
  4 exec_time_vs_threads.png          Execution Time vs Number of Threads
  5 static_vs_pool_scaling.png        Static Pthread vs Thread Pool scaling (speedup)
  6 crossover_<T>threads.png          crossover point (time vs data size)
  7 scenario_times.png                scenario-wise execution times
"""
import argparse, csv, os, statistics
from collections import defaultdict
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ap = argparse.ArgumentParser()
ap.add_argument("--threads", type=int, default=4)
ap.add_argument("--bench", default="benchmark_results.csv")
ap.add_argument("--scen", default="scenario_times.csv")
args = ap.parse_args()

out = Path(__file__).resolve().parent / "output"
out.mkdir(parents=True, exist_ok=True)
COL = {"sequential": "tab:gray", "static": "tab:blue", "pool": "tab:orange"}
LAB = {"sequential": "Sequential", "static": "Static Pthreads", "pool": "Thread Pool"}

def save(name):
    plt.tight_layout(); plt.savefig(out / name, dpi=300); plt.close()
    print("  wrote", out / name)

# ------------------------------------------------------------------ benchmark data
raw = defaultdict(list)           # (experiment, method, threads, images) -> [times]
if os.path.exists(args.bench):
    with open(args.bench, newline="") as f:
        for r in csv.DictReader(f):
            raw[(r["experiment"], r["method"], int(r["threads"]), int(r["images"]))].append(float(r["time_s"]))
med = {k: statistics.median(v) for k, v in raw.items()}

scal = {k: v for k, v in med.items() if k[0] == "scaling"}
if scal:
    N = max(k[3] for k in scal)
    seq = scal[("scaling", "seq", 1, N)]
    threads = sorted({k[2] for k in scal if k[1] != "seq"})
    T = args.threads if args.threads in threads else threads[len(threads) // 2]
    if T != args.threads:
        print(f"note: --threads {args.threads} not benchmarked, using {T}")
    st, po = scal[("scaling", "static", T, N)], scal[("scaling", "pool", T, N)]

    # 1 execution time vs implementation
    plt.figure(figsize=(7, 5))
    vals = [seq, st, po]; names = ["Sequential", f"Static Pthreads\n({T} threads)", f"Thread Pool\n({T} threads)"]
    bars = plt.bar(names, vals, color=[COL["sequential"], COL["static"], COL["pool"]])
    for b, v in zip(bars, vals):
        plt.text(b.get_x() + b.get_width() / 2, v, f"{v*1000:.2f} ms", ha="center", va="bottom")
    plt.ylabel("Execution time (s)"); plt.title(f"Execution time vs implementation ({N} images)")
    save("exec_time_vs_implementation.png")

    # 2 speedup vs implementation
    plt.figure(figsize=(7, 5))
    sp = [seq / st, seq / po]
    bars = plt.bar([f"Static Pthreads\n({T} threads)", f"Thread Pool\n({T} threads)"], sp, color=[COL["static"], COL["pool"]])
    plt.axhline(1.0, color="black", linestyle="--", label="Sequential (speedup = 1)")
    for b, v in zip(bars, sp):
        plt.text(b.get_x() + b.get_width() / 2, v, f"{v:.2f}x", ha="center", va="bottom")
    plt.ylabel("Speedup = T_seq / T_parallel"); plt.title(f"Speedup vs implementation ({N} images)"); plt.legend()
    save("speedup_vs_implementation.png")

    # 3 overhead vs implementation
    plt.figure(figsize=(7, 5))
    ovp = [(st - seq) / seq * 100, (po - seq) / seq * 100]
    ovs = [st - seq, po - seq]
    bars = plt.bar([f"Static Pthreads\n({T} threads)", f"Thread Pool\n({T} threads)"], ovp, color=[COL["static"], COL["pool"]])
    plt.axhline(0, color="black")
    for b, p, s in zip(bars, ovp, ovs):
        plt.text(b.get_x() + b.get_width() / 2, p, f"{p:+.1f}%\n({s*1000:+.2f} ms)", ha="center",
                 va="bottom" if p >= 0 else "top")
    plt.ylabel("Overhead % = (T_par - T_seq) / T_seq x 100")
    plt.title(f"Overhead vs implementation ({N} images)\n(positive = slower than sequential, negative = gain)")
    save("overhead_vs_implementation.png")

    # 4 execution time vs threads
    pos = range(len(threads))
    plt.figure(figsize=(8, 5))
    plt.axhline(seq, linestyle="--", color=COL["sequential"], label="Sequential")
    for m in ("static", "pool"):
        plt.plot(pos, [scal[("scaling", m, t, N)] for t in threads], marker="o", color=COL[m], label=LAB[m])
    plt.xticks(pos, threads); plt.xlabel("Number of threads"); plt.ylabel("Execution time (s)")
    plt.title(f"Execution time vs number of threads ({N} images)"); plt.grid(True); plt.legend()
    save("exec_time_vs_threads.png")

    # 5 static vs pool scaling (speedup)
    plt.figure(figsize=(8, 5))
    for m in ("static", "pool"):
        plt.plot(pos, [seq / scal[("scaling", m, t, N)] for t in threads], marker="o", color=COL[m], label=LAB[m])
    plt.plot(pos, threads, linestyle=":", color="black", label="Ideal (speedup = threads)")
    plt.axhline(1.0, linestyle="--", color=COL["sequential"], label="Sequential")
    plt.xticks(pos, threads); plt.xlabel("Number of threads"); plt.ylabel("Speedup")
    plt.title(f"Static Pthreads vs Thread Pool scaling ({N} images)"); plt.grid(True); plt.legend()
    save("static_vs_pool_scaling.png")
else:
    print("no 'scaling' data found - run: sudo ./benchmark.sh scaling")

# 6 crossover
cross = {k: v for k, v in med.items() if k[0] == "crossover"}
for t in sorted({k[2] for k in cross if k[1] != "seq"}):
    sizes = sorted({k[3] for k in cross})
    plt.figure(figsize=(8, 5))
    plt.plot(sizes, [cross[("crossover", "seq", 1, n)] for n in sizes], marker="s", color=COL["sequential"], linewidth=2, label="Sequential")
    notes = []
    for m in ("static", "pool"):
        ys = [cross[("crossover", m, t, n)] for n in sizes]
        plt.plot(sizes, ys, marker="o", color=COL[m], label=f"{LAB[m]} ({t} threads)")
        x = next((n for n, y in zip(sizes, ys) if y < cross[("crossover", "seq", 1, n)]), None)
        if x is not None:
            plt.axvline(x, linestyle="--", color=COL[m], alpha=0.8)
            plt.annotate(f"{LAB[m]} crossover\n~{x} images", (x, cross[("crossover", m, t, x)]),
                         textcoords="offset points", xytext=(8, 22 if m == "static" else 48), color=COL[m],
                         arrowprops=dict(arrowstyle="->", color=COL[m]))
        else:
            notes.append(f"{LAB[m]}: no crossover up to {sizes[-1]} images")
    if notes:
        plt.text(0.02, 0.97, "\n".join(notes), transform=plt.gca().transAxes, va="top", fontsize=9)
    plt.xscale("log"); plt.yscale("log")
    plt.xlabel("Number of images (log scale)"); plt.ylabel("Execution time (s, log scale)")
    plt.title(f"Crossover point: where parallel beats sequential ({t} threads)"); plt.grid(True, which="both", alpha=0.4); plt.legend(loc="lower right")
    save(f"crossover_{t}threads.png")
if not cross:
    print("no 'crossover' data found - run: sudo ./benchmark.sh crossover")

# 7 scenario-wise times
if os.path.exists(args.scen):
    rows = list(csv.DictReader(open(args.scen, newline="")))
    keys = []
    for r in rows:
        k = (r["scenario"], r["query_type"])
        if k not in keys:
            keys.append(k)
    labels = [f"S{s}\n{q}" for s, q in keys]
    plt.figure(figsize=(10, 5))
    w = 0.27
    for i, m in enumerate(("seq", "static", "pool")):
        ys = [float(next(r["median_time_s"] for r in rows if (r["scenario"], r["query_type"]) == k and r["method"] == m)) for k in keys]
        plt.bar([x + (i - 1) * w for x in range(len(keys))], ys, w, label=LAB["sequential" if m == "seq" else m], color=COL["sequential" if m == "seq" else m])
    plt.xticks(range(len(keys)), labels); plt.ylabel("Execution time (s)")
    plt.title("Scenario-wise execution time (query present / absent)"); plt.legend(); plt.grid(True, axis="y", alpha=0.4)
    save("scenario_times.png")
else:
    print("no scenario_times.csv - run: sudo ./run_scenarios.sh")
