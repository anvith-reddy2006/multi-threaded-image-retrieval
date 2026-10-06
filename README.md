# Multi-Threaded Image Retrieval

Content-based image retrieval over 5000 CIFAR-10 images (32x32 PNG). Each image is
described by a 256-bin grayscale histogram; the query is compared with every database
histogram using squared Euclidean distance and the top-5 closest images are returned.

**Parallelism scope: data parallelism.** The image set is partitioned into chunks; every
thread runs the same algorithm (decode + histogram, then distance + local top-K) on its
chunk, and the per-thread top-K lists are merged into the final top-K.

Four implementations are compared:

| Binary | Description |
|---|---|
| `retrieval_seq` | Sequential baseline |
| `retrieval_mt` | Static partitioning: N equal chunks, one thread per chunk |
| `retrieval_pool` | Thread pool, bounded producer/consumer queue, blocks of 64 images, dynamic scheduling |
| `retrieval_pool_barrier` | Same pool design, but one set of workers with a barrier between the indexing and search phases |

## Requirements

- GCC (C11/gnu11), `make`
- libpng (`libpng-dev`)
- Python 3 with `Pillow`, `numpy`, `matplotlib`

## 1. Dataset (5000 images)

```sh
python3 src/convert_cifar10.py
```
Downloads CIFAR-10, writes 5000 PNGs to `dataset/train/` and the query image `query.png`
(an image that is *not* in the database).

## 2. Build

```sh
make all
```

## 3. Verify (do this before timing anything)

```sh
./verify.sh query.png
```
Checks that every version and thread count (1, 2, 4, 8, 16) returns exactly the same top-5
as the sequential version.

## 4. Optional runtime settings (environment variables)

| Variable | Effect |
|---|---|
| `MAX_IMAGES=N` | Use only the first N files (sorted order) of `dataset/train`. Used for the data-size sweep. |
| `SEARCH_REPEAT=R` | Scan the in-memory histograms R times in the search phase. Results are unchanged; it only enlarges the search workload so `t_search` becomes measurable (at 5000 images it is only a few ms, while PNG decoding dominates). |

Example: `MAX_IMAGES=1000 SEARCH_REPEAT=50 ./retrieval_mt query.png 4`

## 5. Query scenarios

Queries must be **32x32 PNG** files.

```sh
python3 tools/make_queries.py        # default: database images 0, 1, 2
./run_scenarios.sh
```

`make_queries.py` writes to `queries/`:
- `exact_<id>.png` - exact copy of a database image
- `histeq_<id>.png` - histogram-equalised version
- `gauss_<id>.png` - Gaussian-smoothed version (sigma = 1.0)

`query.png` is the "same domain, different image" scenario.

`run_scenarios.sh` runs every query through all versions (2, 4, 8 threads), checks that all
agree with sequential, prints the top-5 and the rank/distance of the source image, and writes
`scenario_results.csv` (scenario, query_file, method, threads, rank, result_file, distance).

Expected behaviour: the exact image is rank 1 with distance 0; a Gaussian-smoothed image
changes the histogram only slightly (often still near the top); histogram equalisation
changes the histogram strongly, so the source image usually is not retrieved - the
grayscale histogram feature is not invariant to contrast changes.

## 6. Benchmarking

```sh
sudo ./benchmark.sh query.png                    # data-size sweep: 500, 1000, 2500, 5000 images
sudo ./benchmark.sh query.png search_repeat      # SEARCH_REPEAT = 1, 10, 50, 100 at full size
```
- 1 warm-up + `RUNS` runs (default 10) per configuration, medians are used for plots.
- Runs use `nice -n -20` when permitted (hence `sudo`) to reduce interference from other
  processes; otherwise a warning is printed. Close other programs while benchmarking.
- `TASKSET_CPUS=0-3 ./benchmark.sh ...` additionally pins runs to those CPUs.
- `RUNS=3 ./benchmark.sh ...` for a quick test; `CSV=name.csv` changes the output file.
- Output: `benchmark_results.csv` (columns method, threads, images, search_repeat, run,
  t_index, t_search, t_total, vol_csw, invol_csw) and `benchmark_meta.txt` (date, core count,
  priority/pinning settings).

`T_total` includes indexing and search but excludes directory scanning and query parsing
(serial setup costs, Amdahl's law).

## 7. Graphs

```sh
python3 graphs/generate_graphs.py benchmark_results.csv
```
Writes PNGs to `graphs/output/`: time vs threads (`t_index`, `t_search`, `t_total`),
`speedup`, `efficiency`, the crossover plots (`crossover_total`, `crossover_search`: time vs
number of images, sequential vs 2/4/8 threads) and `search_repeat_scaling`. If you ran both
benchmark modes into different CSV files, pass both files to the script.
