# Multi-Threaded Image Retrieval (histogram intersection)

Content-based image retrieval over 5000 CIFAR-10 images (32x32 PNG).
Each image is turned into a normalised 256-bin grayscale histogram, and the query is compared
with every database image using **histogram intersection** (similarity = sum of the bin-wise
minimum; 1.0000 = identical histograms, larger = more similar). The 5 most similar images are returned.

**Parallelism scope: data parallelism** - the database is split among threads, every thread runs the
same similarity computation on its part, and the partial results are merged and sorted.

| Binary | Description |
|---|---|
| `retrieval_seq` | Sequential baseline |
| `retrieval_mt` | Static Pthreads: equal chunks, one thread per chunk |
| `retrieval_pool` | Thread pool: producer + workers, bounded queue, dynamic scheduling |

Only the similarity computation is timed (images are loaded into memory before the timer starts).
Ties in similarity are ordered by file name, so all versions return identical top-5 lists.

## Requirements
GCC, make, libpng (`sudo apt install build-essential libpng-dev`), Python 3 with `pillow numpy matplotlib`.

## Setup
```sh
python3 src/convert_cifar10.py     # creates dataset/train (5000 images)
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
sudo ./benchmark.sh                # 1,3,4,5. thread scaling 1-32 + crossover (use: scaling | crossover | all)
python3 graphs/generate_graphs.py  # 6. graphs -> graphs/output/
```
Close other programs while benchmarking. Useful settings: `RUNS=10` (default for benchmark; 5 for scenarios),
`THREADS=4` (scenario runs), `CROSS_THREADS="4 8"`, `TASKSET_CPUS=0-3` (pin to cores), `QUERY=`, `SCENARIO=`.

Metrics (printed in every benchmark table): speedup = T_seq / T_par, overhead = T_par - T_seq,
overhead % = (T_par - T_seq) / T_seq x 100. The crossover point (smallest data size where parallel
beats sequential) is printed at the end of the crossover experiment and marked on the crossover graphs.

Outputs (not committed): `benchmark_table.txt`, `scenario_table.txt`, `benchmark_results.csv`,
`scenario_times.csv`, `scenario_results.csv`, `benchmark_meta.txt` (core count, priority), `graphs/output/*.png`.
