#!/usr/bin/env bash
# =========================================================
#  BENCHMARK.SH - items 1, 3, 4, 5 of the remaining work
#
#  Experiment 1 "scaling"  : Static Pthreads and Thread Pool with
#                            1, 2, 4, 8, 16, 32 threads on the full dataset
#  Experiment 2 "crossover": execution time vs number of images
#                            (IMAGE_LIMIT = 100 ... 5000) for Sequential,
#                            Static and Pool -> finds the crossover point
#                            where parallel execution becomes beneficial.
#  Experiment 3 "batch"    : Thread Pool with 1, 4, 16, 64, 256, 1024 images
#                            per task (POOL_BATCH) on the full dataset;
#                            batch 1 = the original one-image-per-task design.
#
#  Metrics (computed per row, parallel vs sequential, same conditions):
#     speedup      = sequential time / parallel time
#     time saved   = sequential time - parallel time        (negative = parallel is slower)
#     efficiency   = speedup / threads                      (1.00 = perfect; lower = time lost to overhead)
#
#  Usage:   sudo ./benchmark.sh [scaling|crossover|batch|all]      (default: all)
#           (sudo => every run uses  nice -n -20 )
#  Env:     RUNS=10  SCENARIO=1  QUERY=queries/present.png
#           CROSS_THREADS="4 8"  TASKSET_CPUS=0-3 (optional pinning)
#  Output:  live tables on screen, benchmark_table.txt,
#           benchmark_results.csv (every single run), benchmark_meta.txt
# =========================================================
set -uo pipefail
source ./common.sh
ensure_setup
setup_priority

MODE="${1:-all}"
case $MODE in scaling|crossover|batch|all) ;; *) echo "Usage: $0 [scaling|crossover|batch|all]"; exit 1;; esac

RUNS="${RUNS:-10}"
SCENARIO="${SCENARIO:-1}"
QUERY="${QUERY:-$QUERY_PRESENT}"
SCALE_THREADS=(1 2 4 8 16 32)
CROSS_THREADS=(${CROSS_THREADS:-4 8})
ALL_SIZES=(100 250 500 1000 2500 5000 7500 10000)
BATCH_SIZES=(1 4 16 64 256 1024)
CSV="benchmark_results.csv"; TABLE="benchmark_table.txt"; META="benchmark_meta.txt"

TOTAL_IMAGES=$(ls dataset/train/*.png | wc -l)
SIZES=(); for n in "${ALL_SIZES[@]}"; do [ "$n" -le "$TOTAL_IMAGES" ] && SIZES+=("$n"); done
[ "${SIZES[-1]}" -ne "$TOTAL_IMAGES" ] && SIZES+=("$TOTAL_IMAGES")

{
    echo "date:       $(date -Iseconds)"
    echo "mode:       $MODE"
    echo "runs:       $RUNS (+1 warm-up per configuration)"
    echo "cpu cores:  $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo unknown)"
    echo "priority:   $PRIO_NOTE"
    echo "pinning:    $PIN_NOTE"
    echo "dataset:    $TOTAL_IMAGES images"
    echo "query:      $QUERY   scenario: $SCENARIO (${SCENARIO_NAMES[$SCENARIO]})"
} > "$META"
cat "$META"

echo "experiment,method,threads,images,scenario,run,time_s,batch" > "$CSV"
: > "$TABLE"
declare -A MED       # MED[experiment|method|threads|images] = median time

# bench EXPERIMENT IMPL THREADS IMAGES [BATCH]   -> stores median, appends raw runs to CSV
# BATCH (pool only) = images per task; empty = the program's default
bench() {
    local exp=$1 impl=$2 t=$3 n=$4 b=${5:-} th=$3 times="" r v key
    [ "$impl" = "seq" ] && th=1
    IMAGE_LIMIT=$n POOL_BATCH=$b run_prog "$impl" "$QUERY" "$SCENARIO" "$th" >/dev/null     # warm-up
    for ((r=1; r<=RUNS; r++)); do
        v=$(IMAGE_LIMIT=$n POOL_BATCH=$b run_prog "$impl" "$QUERY" "$SCENARIO" "$th" | get_time)
        echo "$exp,$impl,$th,$n,$SCENARIO,$r,$v,$b" >> "$CSV"
        times+="$v"$'\n'
    done
    key="$exp|$impl|$th|$n"; [ -n "$b" ] && key+="|$b"
    MED["$key"]=$(printf '%s' "$times" | median)
}

pr() { printf "$@" | tee -a "$TABLE"; }
header() {  # title, first column name
    { echo; echo "=== $1 ==="
      printf "%-8s %-8s %6s %12s %9s %13s %11s\n" "$2" method threads "time(s)" speedup "saved(s)" efficiency
      printf "%-8s %-8s %6s %12s %9s %13s %11s\n" -------- -------- ------ ------------ --------- ------------- -----------; } | tee -a "$TABLE"
}
row() {  # label method threads time seq_time
    local lab=$1 m=$2 t=$3 tm=$4 sq=$5 sp sv ef
    if [ "$m" = "sequential" ]; then sp="-"; sv="-"; ef="-"
    else
        sp=$(awk -v s="$sq" -v p="$tm" 'BEGIN{printf "%.2f", s/p}')
        sv=$(awk -v s="$sq" -v p="$tm" 'BEGIN{printf "%+.6f", s-p}')
        ef=$(awk -v s="$sq" -v p="$tm" -v n="$t" 'BEGIN{printf "%.2f", s/p/n}')
    fi
    printf "%-8s %-8s %6s %12.6f %9s %13s %11s\n" "$lab" "$m" "$t" "$tm" "$sp" "$sv" "$ef" | tee -a "$TABLE"
}

echo; echo "======== BENCHMARK ($MODE) ========"

# ---------------- Experiment 1: thread scaling ----------------
if [ "$MODE" = "scaling" ] || [ "$MODE" = "all" ]; then
    header "Thread scaling: $TOTAL_IMAGES images (median of $RUNS runs)" images
    bench scaling seq 1 "$TOTAL_IMAGES"
    SEQ=${MED["scaling|seq|1|$TOTAL_IMAGES"]}
    row "$TOTAL_IMAGES" sequential 1 "$SEQ" "$SEQ"
    for impl in static pool; do
        for t in "${SCALE_THREADS[@]}"; do
            bench scaling $impl "$t" "$TOTAL_IMAGES"
            row "$TOTAL_IMAGES" $impl "$t" "${MED["scaling|$impl|$t|$TOTAL_IMAGES"]}" "$SEQ"
        done
    done
fi

# ---------------- Experiment 2: crossover ----------------
if [ "$MODE" = "crossover" ] || [ "$MODE" = "all" ]; then
    for t in "${CROSS_THREADS[@]}"; do
        header "Crossover: execution time vs data size, $t threads (median of $RUNS runs)" images
        for n in "${SIZES[@]}"; do
            [ -z "${MED["crossover|seq|1|$n"]:-}" ] && bench crossover seq 1 "$n"
            SQ=${MED["crossover|seq|1|$n"]}
            row "$n" sequential 1 "$SQ" "$SQ"
            for impl in static pool; do
                bench crossover $impl "$t" "$n"
                row "$n" $impl "$t" "${MED["crossover|$impl|$t|$n"]}" "$SQ"
            done
        done
    done
    {
    echo; echo "=== CROSSOVER POINT (smallest data size where parallel is faster than sequential) ==="
    for t in "${CROSS_THREADS[@]}"; do
        for impl in static pool; do
            found="none up to $TOTAL_IMAGES images (sequential is faster everywhere)"
            for n in "${SIZES[@]}"; do
                if awk -v p="${MED["crossover|$impl|$t|$n"]}" -v s="${MED["crossover|seq|1|$n"]}" 'BEGIN{exit !(p<s)}'; then
                    found="$n images"; break
                fi
            done
            printf "  %-7s with %2s threads: %s\n" "$impl" "$t" "$found"
        done
    done
    } | tee -a "$TABLE"
fi

# ---------------- Experiment 3: thread pool batch size ----------------
if [ "$MODE" = "batch" ] || [ "$MODE" = "all" ]; then
    [ -z "${MED["batch|seq|1|$TOTAL_IMAGES"]:-}" ] && bench batch seq 1 "$TOTAL_IMAGES"
    SEQ=${MED["batch|seq|1|$TOTAL_IMAGES"]}
    for t in "${CROSS_THREADS[@]}"; do
        header "Thread pool batch size (images per task), $t threads, $TOTAL_IMAGES images (median of $RUNS runs)" batch
        row "-" sequential 1 "$SEQ" "$SEQ"
        for b in "${BATCH_SIZES[@]}"; do
            bench batch pool "$t" "$TOTAL_IMAGES" "$b"
            row "$b" pool "$t" "${MED["batch|pool|$t|$TOTAL_IMAGES|$b"]}" "$SEQ"
        done
    done
fi

echo
echo "Saved: $CSV (all runs), $TABLE (tables), $META"
echo "Next:  python3 graphs/generate_graphs.py"
