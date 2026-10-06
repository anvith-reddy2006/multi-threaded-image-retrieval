#!/usr/bin/env python3
"""
Plots from benchmark CSV(s) (medians over runs). Output: graphs/output/*.png

Usage: python3 graphs/generate_graphs.py benchmark_results.csv [more.csv ...]

CSV columns: method,threads,images,search_repeat,run,t_index,t_search,t_total,vol_csw,invol_csw
(old CSVs without 'images'/'search_repeat' are also accepted.)

Graphs:
  t_index / t_search / t_total vs threads   (largest image count, search_repeat=1)
  speedup.png, efficiency.png               (t_total based, same slice)
  crossover_total.png, crossover_search.png time vs number of images
                                            (sequential vs 2/4/8 threads)  [needs MAX_IMAGES sweep]
  search_repeat_scaling.png                 t_search vs SEARCH_REPEAT       [needs search_repeat mode]
"""
import csv, sys, statistics
from collections import defaultdict
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

if len(sys.argv) < 2:
    print(f"Usage: python {sys.argv[0]} benchmark_results.csv [more.csv ...]")
    sys.exit(1)

# data[(method, threads, images, sr)][field] -> list of values
data = defaultdict(lambda: defaultdict(list))
for path in sys.argv[1:]:
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            key = (row["method"], int(row["threads"]),
                   int(row.get("images") or 0), int(row.get("search_repeat") or 1))
            for fld in ("t_index", "t_search", "t_total"):
                data[key][fld].append(float(row[fld]))

if not data:
    sys.exit("No data rows found.")

def med(method, t, img, sr, fld):
    v = data.get((method, t, img, sr), {}).get(fld)
    return statistics.median(v) if v else None

METHODS = [("static", "Static MT"), ("pool", "Thread Pool"), ("pool_barrier", "Barrier Pool")]
out = Path(__file__).resolve().parent / "output"
out.mkdir(parents=True, exist_ok=True)

all_images = sorted({k[2] for k in data})
all_sr = sorted({k[3] for k in data})
threads = sorted({k[1] for k in data if k[0] != "sequential"})
max_img = max(all_images)

def save(name):
    plt.tight_layout()
    plt.savefig(out / name, dpi=300)
    plt.close()

# ---- 1. metrics vs threads (largest dataset, search_repeat = 1) ------------
seq = {f: med("sequential", 1, max_img, 1, f) for f in ("t_index", "t_search", "t_total")}

def vs_threads(fld, title, fname):
    plt.figure(figsize=(8, 5))
    if seq[fld] is not None:
        plt.axhline(seq[fld], linestyle="--", color="gray", label="Sequential")
    for m, label in METHODS:
        ys = [med(m, t, max_img, 1, fld) for t in threads]
        if any(y is not None for y in ys):
            pts = [(t, y) for t, y in zip(threads, ys) if y is not None]
            plt.plot(*zip(*pts), marker="o", label=label)
    plt.xlabel("Number of threads"); plt.ylabel("Time (s)")
    plt.title(f"{title} ({max_img} images)")
    plt.xticks(threads); plt.grid(True); plt.legend()
    save(fname)

vs_threads("t_index", "Indexing time (PNG decode + histogram)", "t_index.png")
vs_threads("t_search", "Search time (distance + top-K)", "t_search.png")
vs_threads("t_total", "Total execution time", "t_total.png")

if seq["t_total"] is not None:
    plt.figure(figsize=(8, 5))
    for m, label in METHODS:
        pts = [(t, seq["t_total"] / med(m, t, max_img, 1, "t_total")) for t in threads
               if med(m, t, max_img, 1, "t_total")]
        if pts:
            plt.plot(*zip(*pts), marker="o", label=label)
    plt.plot(threads, threads, linestyle="--", color="black", label="Ideal")
    plt.xlabel("Number of threads"); plt.ylabel("Speedup vs sequential")
    plt.title(f"Total speedup ({max_img} images)")
    plt.xticks(threads); plt.grid(True); plt.legend()
    save("speedup.png")

    plt.figure(figsize=(8, 5))
    for m, label in METHODS:
        pts = [(t, seq["t_total"] / med(m, t, max_img, 1, "t_total") / t * 100) for t in threads
               if med(m, t, max_img, 1, "t_total")]
        if pts:
            plt.plot(*zip(*pts), marker="o", label=label)
    plt.xlabel("Number of threads"); plt.ylabel("Efficiency (%)")
    plt.title(f"Parallel efficiency ({max_img} images)")
    plt.xticks(threads); plt.grid(True); plt.legend()
    save("efficiency.png")

# ---- 2. crossover: time vs number of images --------------------------------
if len(all_images) > 1:
    for fld, title, fname in (("t_total", "Total time vs data size", "crossover_total.png"),
                              ("t_search", "Search time vs data size", "crossover_search.png")):
        plt.figure(figsize=(8, 5))
        pts = [(n, med("sequential", 1, n, 1, fld)) for n in all_images if med("sequential", 1, n, 1, fld)]
        if pts:
            plt.plot(*zip(*pts), marker="s", color="black", linewidth=2, label="Sequential")
        for t in (2, 4, 8):
            if t not in threads:
                continue
            pts = [(n, med("static", t, n, 1, fld)) for n in all_images if med("static", t, n, 1, fld)]
            if pts:
                plt.plot(*zip(*pts), marker="o", label=f"Static MT, {t} threads")
        plt.xlabel("Number of images"); plt.ylabel("Time (s)")
        plt.title(title); plt.grid(True); plt.legend()
        save(fname)

# ---- 3. search time vs SEARCH_REPEAT ---------------------------------------
sr_vals = [s for s in all_sr if s >= 1]
if len(sr_vals) > 1:
    plt.figure(figsize=(8, 5))
    pts = [(s, med("sequential", 1, max_img, s, "t_search")) for s in sr_vals if med("sequential", 1, max_img, s, "t_search")]
    if pts:
        plt.plot(*zip(*pts), marker="s", color="black", linewidth=2, label="Sequential")
    for t in (2, 4, 8):
        if t not in threads:
            continue
        pts = [(s, med("static", t, max_img, s, "t_search")) for s in sr_vals if med("static", t, max_img, s, "t_search")]
        if pts:
            plt.plot(*zip(*pts), marker="o", label=f"Static MT, {t} threads")
    plt.xlabel("SEARCH_REPEAT (search workload multiplier)"); plt.ylabel("t_search (s)")
    plt.title(f"Search time vs workload size ({max_img} images)")
    plt.grid(True); plt.legend()
    save("search_repeat_scaling.png")

print(f"Graphs written to {out}/ (medians over runs)")
