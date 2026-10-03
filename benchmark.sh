#!/bin/bash
# =========================================================
#  BENCHMARK.SH  —  warm-up + 10 runs, median, CSV output
# =========================================================
set -euo pipefail

QUERY="${1:-query.png}"
RUNS=10
THREADS=(1 2 4 8 16)
CSV="benchmark_results.csv"

if [ ! -f "$QUERY" ]; then
    echo "ERROR: query image '$QUERY' not found"
    exit 1
fi

# Check all binaries exist
for bin in retrieval_seq retrieval_mt retrieval_pool; do
    if [ ! -x "./$bin" ]; then
        echo "ERROR: ./$bin not found or not executable.  Run 'make all' first."
        exit 1
    fi
done

# CSV header
echo "method,threads,run,t_index,t_search,t_total,vol_csw,invol_csw" > "$CSV"

run_benchmark() {
    local BIN=$1
    local METHOD=$2
    local THREADS=$3

    echo "  Warm-up..."
    if [ "$METHOD" = "sequential" ]; then
        ./$BIN "$QUERY" > /dev/null 2>&1
    else
        ./$BIN "$QUERY" "$THREADS" > /dev/null 2>&1
    fi

    for ((r=1; r<=RUNS; r++)); do
        echo -n "  Run $r/$RUNS: "
        if [ "$METHOD" = "sequential" ]; then
            OUTPUT=$(./$BIN "$QUERY" 2>&1)
        else
            OUTPUT=$(./$BIN "$QUERY" "$THREADS" 2>&1)
        fi

        T_INDEX=$(echo "$OUTPUT" | grep 'T_index:' | awk '{print $2}')
        T_SEARCH=$(echo "$OUTPUT" | grep 'T_search:' | awk '{print $2}')
        T_TOTAL=$(echo "$OUTPUT" | grep 'T_total:' | awk '{print $2}')
        
        CSW=$(echo "$OUTPUT" | grep 'Context switches:') || true
        if [ -n "$CSW" ]; then
            VOL_CSW=$(echo "$CSW" | awk '{print $3}')
            INVOL_CSW=$(echo "$CSW" | awk '{print $5}')
        else
            VOL_CSW="0"
            INVOL_CSW="0"
        fi

        echo "index=$T_INDEX  search=$T_SEARCH  total=$T_TOTAL"
        echo "$METHOD,$THREADS,$r,$T_INDEX,$T_SEARCH,$T_TOTAL,$VOL_CSW,$INVOL_CSW" >> "$CSV"
    done
}

echo "========================================"
echo " MULTI-THREADED IMAGE RETRIEVAL BENCHMARK"
echo "========================================"
echo "Runs per config: $RUNS"
echo "CPU cores: $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo unknown)"
echo

# Sequential
echo "--- Sequential ---"
run_benchmark retrieval_seq sequential 1
echo

# Static multithreading
for t in "${THREADS[@]}"; do
    echo "--- Static MT, $t threads ---"
    run_benchmark retrieval_mt static "$t"
    echo
done

# Thread pool
for t in "${THREADS[@]}"; do
    echo "--- Thread Pool, $t threads ---"
    run_benchmark retrieval_pool pool "$t"
    echo
done

echo "Results written to $CSV"
echo "Run: python3 graphs/generate_graphs.py $CSV"
