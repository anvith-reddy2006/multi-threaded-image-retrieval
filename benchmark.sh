#!/bin/bash

echo "========================================"
echo " MULTI-THREADED IMAGE RETRIEVAL BENCHMARK"
echo "========================================"

THREADS=(1 2 4 8 16)
RUNS=10

for t in "${THREADS[@]}"
do
    echo ""
    echo "----------------------------------------"
    echo "Threads: $t"
    echo "----------------------------------------"

    for ((i=1; i<=RUNS; i++))
    do
        echo -n "Run $i: "

        ./retrieval_mt query.png "$t" 2>/dev/null |
        grep "Execution Time"
    done
done
