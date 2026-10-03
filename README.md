# Multi-Threaded Image Retrieval

This project implements a multi-threaded image retrieval system, comparing sequential, static multithreading, and thread-pool (producer-consumer) approaches.

## Requirements

- GCC (with C11 support)
- libpng (`libpng-dev`)
- Python 3 with `matplotlib` for graph generation

## Building

To build all versions of the project, run:

```sh
make all
```

This will produce the following executables:
- `retrieval_seq`: The sequential baseline.
- `retrieval_mt`: The statically-partitioned multithreaded version.
- `retrieval_pool`: The thread-pool based version using a bounded queue and dynamic scheduling.

## Running Tests

To verify that all versions produce identical results across thread counts:

```sh
./verify.sh query.png
```
(Do not time anything until this passes!)

## Benchmarking

To run the full suite (warm-up, 10 runs per configuration, median times), and save the results to a CSV file:

```sh
./benchmark.sh query.png
```

This script will output `benchmark_results.csv`.

## Generating Graphs

To plot execution time and speedup (vs sequential) from the generated CSV file:

```sh
python3 graphs/generate_graphs.py benchmark_results.csv
```
This generates PNG graphs in the current directory.
