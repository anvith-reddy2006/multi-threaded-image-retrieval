# Multi-Threaded Image Retrieval

This project implements a multi-threaded image retrieval system, comparing sequential, static multithreading, and phase-based thread-pool (producer-consumer) approaches.

## Requirements

- GCC (with C11 support)
- libpng (`libpng-dev`)
- Python 3 with `matplotlib` and `Pillow` (for dataset generation and graph plotting)

## Generating the Dataset

To generate the dataset, run the provided Python script. It will download the real CIFAR-10 dataset (if not present) and convert 5000 images into standard `.png` format. 

```sh
python3 src/convert_cifar10.py
```
*(Note: Because the decoding workload is perfectly uniform across all these identical 32x32 images, a dynamic queue-based scheduler may not demonstrate a significant speedup over static load distribution. Using images of radically different sizes would show dynamic scheduling's advantage over static).*

## Building

To build all versions of the project, run:

```sh
make all
```

This will produce the following executables:
- `retrieval_seq`: The sequential baseline.
- `retrieval_mt`: The statically-partitioned multithreaded version.
- `retrieval_pool`: The thread-pool based version using a bounded queue, block-based partitioning, and dynamic scheduling.

## Running Tests

To verify that all versions produce identical results across thread counts:

```sh
./verify.sh query.png
```
(Do not time anything until this passes!)

## Benchmarking

To run the full suite (warm-up, 10 runs per configuration, median times), and save the results to a CSV file (including context switch logging):

```sh
./benchmark.sh query.png
```

This script will output `benchmark_results.csv`.
*(Note: `T_total` explicitly leaves out sequential directory scanning and initial query image parsing. These serial setup costs represent Amdahl's Law in practice in the broader context).*

## Generating Graphs

To plot `t_index`, `t_search`, and `t_total` execution time along with speedups from the generated CSV file using the statistically robust medians:

```sh
python3 graphs/generate_graphs.py benchmark_results.csv
```
This generates PNG graphs in the current directory.
