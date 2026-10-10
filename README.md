# Multi-Threaded Image Retrieval (histogram intersection)

Content-based image retrieval over 10000 CIFAR-10 images (32x32 PNG).
Each image is turned into a normalised 256-bin grayscale histogram, and the query is compared
with every database image using **histogram intersection** (similarity = sum of the bin-wise
minimum; 1.0000 = identical histograms, larger = more similar). The 5 most similar images are returned.

**Parallelism scope: data parallelism** - the database is split among threads, every thread runs the
same similarity computation on its part, and the partial results are merged and sorted.

| Binary | Description |
|---|---|
| `retrieval_seq` | Sequential baseline |
| `retrieval_mt` | Static Pthreads: equal chunks, one thread per chunk |
| `retrieval_pool` | Thread pool: producer + workers, bounded queue, dynamic scheduling; each task is a batch of 64 images (`POOL_BATCH=N` to change, `POOL_BATCH=1` = one image per task) |

Only the similarity computation is timed (images are loaded into memory before the timer starts).
Ties in similarity are ordered by file name, so all versions return identical top-5 lists.

## Requirements
GCC, make, libpng (`sudo apt install build-essential libpng-dev`), Python 3 with `pillow numpy matplotlib`.

## Setup
```sh
python3 src/convert_cifar10.py     # creates dataset/train (10000 images)
make all
chmod +x *.sh
```
(Faster download: `aria2c -x 16 -s 16 -c https://www.cs.toronto.edu/~kriz/cifar-10-python.tar.gz`
in the project folder before running the converter.)

## Running one query
```sh
./retrieval_seq  <query.png> <scenario>
./retrieval_mt   <query.png> <scenario> <threads>      # 1..32
./retrieval_pool <query.png> <scenario> <threads>      # 1..32
```
Scenarios: 1 = original, 2 = histogram equalization, 3 = Gaussian smoothing, 4 = same domain / different image
(scenarios 2 and 3 are applied to the query inside the program).
Optional: `IMAGE_LIMIT=1000 ./retrieval_seq query.png 1` uses only the first 1000 database images
(used for the data-size / crossover experiment).

Query files: `query_absent.png` (not in the database), `query_scenario4.png` (scenario 4),
`queries/present.png` (an exact copy of a database image; created automatically from the first database image).

## Final experiments (run in this order, with sudo for `nice -n -20`)
```sh
./verify.sh                        # 8. correctness: all versions give the same top-5, present/absent works
sudo ./run_scenarios.sh            # 1,2. 4 scenarios x query present/absent x 3 implementations
sudo ./benchmark.sh                # 1,3,4,5. thread scaling 1-32 + crossover + pool batch size (use: scaling | crossover | batch | all)
python3 graphs/generate_graphs.py  # 6. graphs -> graphs/output/
```
Close other programs while benchmarking. Useful settings: `RUNS=10` (default for benchmark; 5 for scenarios),
`THREADS=4` (scenario runs), `CROSS_THREADS="4 8"`, `TASKSET_CPUS=0-3` (pin to cores), `QUERY=`, `SCENARIO=`.

Metrics (printed in every benchmark table): speedup = T_seq / T_par, time saved = T_seq - T_par
(negative = parallel is slower), efficiency = speedup / threads (1.00 = perfect, lower = time lost to overhead). The crossover point (smallest data size where parallel
beats sequential) is printed at the end of the crossover experiment and marked on the crossover graphs.

Outputs: the final `benchmark_table.txt`, `scenario_table.txt` and `graphs/output/*.png` are committed
(the results below); the raw `benchmark_results.csv`, `scenario_times.csv`, `scenario_results.csv` and
`benchmark_meta.txt` (core count, priority) are not.

## Results (Apple M5, 10 cores = 4 performance + 6 efficiency, 10000 images)
All runs used `sudo` (`nice -n -20`), with the median of 10 runs (5 for scenarios) and a warm-up run before each configuration.
Thread pool results use 64 images per task unless stated otherwise. Graphs: `graphs/output/`.

**Correctness.** All three implementations return the same top-5 in every scenario, with the query present and absent
(`verify.sh`: 144/144 comparisons). A query that is in the database scores 1.0000 (exact match).
Histogram equalization (scenario 2) changes the intensity distribution the most, so the present query's best score
falls to 0.47 and a different image ranks first. Gaussian smoothing (scenario 3) keeps the histogram shape roughly
intact (0.77). Execution time does not depend on the scenario (4.5-4.9 ms static, 5.2-5.6 ms pool, 4 threads),
because every query is compared with every database image the same way.

**Thread scaling** (sequential = 19.6 ms)

| Threads | Static (ms) | Speedup | Efficiency | Pool (ms) | Speedup | Efficiency |
|---|---|---|---|---|---|---|
| 1  | 18.66 | 1.05 | 1.05 | 19.47 | 1.01 | 1.01 |
| 2  |  9.43 | 2.08 | 1.04 | 10.15 | 1.93 | 0.97 |
| 4  |  4.76 | 4.13 | 1.03 |  5.44 | 3.61 | 0.90 |
| 8  |  2.65 | 7.40 | 0.92 |  3.20 | 6.13 | 0.77 |
| 16 |  2.59 | 7.59 | 0.47 |  2.93 | 6.69 | 0.42 |
| 32 |  2.52 | 7.78 | 0.24 |  3.26 | 6.02 | 0.19 |

- **Static Pthreads scale almost perfectly up to 4 threads and well up to 8 (7.4x)**, then level off at about 7.8x.
  Past about 10 threads there are no free cores left, and the 6 efficiency cores are slower than the 4 performance
  cores, so the slowest thread decides the total time. Efficiency is slightly above 1 at 1-4 threads because static
  with 1 thread is already 5% faster than the sequential program (small differences in the loop code). This is not
  a super-linear speedup.
- **The thread pool scales too, but stays behind static** (3.6x vs 4.1x at 4 threads, 6.1x vs 7.4x at 8). The
  work divides evenly (every image costs the same), so dynamic scheduling has nothing to balance. What remains is
  pure overhead: one producer thread, a lock and condition-variable operation per task, and workers waiting on the
  queue. Its best result is 6.7x at 16 threads, where the extra threads keep the slower cores busy.

**Thread pool task granularity: images per task** (`POOL_BATCH`; 1 = the original one-image-per-task design)

| Images per task | 1 | 4 | 16 | 64 | 256 | 1024 |
|---|---|---|---|---|---|---|
| Speedup, 4 threads | 1.04 | 2.05 | 3.18 | 3.60 | **3.70** | 3.16 |
| Speedup, 8 threads | 0.79 | 2.91 | 5.47 | **6.14** | 6.10 | 4.41 |

With one image per task, each task is about 2 µs of work, and the queue's locking and signalling cost about as
much as the work itself. Adding threads only adds contention on the lock, so 8 threads are *slower* than
sequential. Larger batches pay that cost less often, and speedup peaks at 64-256 images per task.
At 1024 images per task there are only 10 tasks for 10000 images, so with 8 threads two threads take a second task
while six sit idle, and speedup falls again. Task size has to balance synchronization cost against load balance.

**Crossover: smallest data size where parallel is faster than sequential**

| | 4 threads | 8 threads |
|---|---|---|
| Static Pthreads | 100 images (1.95x) | 100 images (1.29x) |
| Thread Pool | 250 images (2.10x) | 250 images (1.60x) |

Static threading already pays off at 100 images (0.19 ms of sequential work). The pool does not: at 64 images per
task, 100 images make only 2 tasks, so at most 2 workers do anything while the cost of creating all the threads
is still paid. At small sizes 4 threads beat 8, because creating extra threads costs more than the little work
they each get. From 1000 images upward, 8 threads are faster.

**Conclusion.** For this workload (fine-grained, evenly balanced, no shared state while it runs), static partitioning is
the best choice. It reaches 7.4x at 8 threads with almost no synchronization. A thread pool can come close
(6.1x), but only once tasks are coarse enough. With one image per task it is no faster than sequential. The pool's
dynamic scheduling would pay off for workloads where tasks vary in cost (for example, images of different sizes)
or where new work keeps arriving.

**Measurement notes.** Only the similarity computation is timed: images are loaded before the timer starts,
and the merge and final sort happen after it stops (the same boundary in all three programs). The pool's timer was
originally stopped after the merge, which added about 0.7 ms (copying 10000 results) that the static version
did not pay. The numbers above use the corrected boundary.
