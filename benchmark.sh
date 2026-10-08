#!/bin/bash
# =========================================================
#  BENCHMARK.SH - items 1, 3, 4, 5 of the remaining work
#
#  Experiment 1 "scaling"  : Static Pthreads and Thread Pool with
#                            1, 2, 4, 8, 16, 32 threads on the full dataset
#  Experiment 2 "crossover": execution time vs number of images
#                            (IMAGE_LIMIT = 100 ... 5000) for Sequential,
#                            Static and Pool -> finds the crossover point
#                            where parallel execution becomes beneficial.
#
#  Metrics (computed per row, parallel vs sequential, same conditions):
#     speedup      = sequential time / parallel time
#     overhead     = parallel time - sequential time        (negative = gain)
#     overhead %   = (parallel - sequential) / sequential * 100
#
#  Usage:   sudo ./benchmark.sh [scaling|crossover|all]      (default: all)
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
case $MODE in scaling|crossover|all) ;; *) echo "Usage: $0 [scaling|crossover|all]"; exit 1;; esac

RUNS="${RUNS:-10}"
SCENARIO="${SCENARIO:-1}"
QUERY="${QUERY:-$QUERY_PRESENT}"
SCALE_THREADS=(1 2 4 8 16 32)
CROSS_THREADS=(${CROSS_THREADS:-4 8})
ALL_SIZES=(100 250 500 1000 2500 5000 7500 10000)
CSV="benchmark_results.csv"; TABLE="benchmark_table.txt"; META="benchmark_meta.txt"

TOTAL_IMAGES=$(ls dataset/train/*.png | wc -l)
SIZES=(); for n in "${ALL_SIZES[@]}"; do [ "$n" -le "$TOTAL_IMAGES" ] && SIZES+=("$n"); done
[ "${SIZES[-1]}" -ne "$TOTAL_IMAGES" ] && SIZES+=("$TOTAL_IMAGES")

{
    echo "date:       $(date -Iseconds)"
    echo "mode:       $MODE"
    echo "runs:       $RUNS (+1 warm-up per configuration)"
    echo "cpu cores:  $(nproc 2>/dev/null || echo unknown)"
    echo "priority:   $PRIO_NOTE"
    echo "pinning:    $PIN_NOTE"
    echo "dataset:    $TOTAL_IMAGES images"
    echo "query:      $QUERY   scenario: $SCENARIO (${SCENARIO_NAMES[$SCENARIO]})"
} > "$META"
cat "$META"

echo "experiment,method,threads,images,scenario,run,time_s" > "$CSV"
: > "$TABLE"
declare -A MED       # MED[experiment|method|threads|images] = median time

# bench EXPERIMENT IMPL THREADS IMAGES   -> stores median, appends raw runs to CSV
bench() {
    local exp=$1 impl=$2 t=$3 n=$4 th=$3 times="" r v
    [ "$impl" = "seq" ] && th=1
    IMAGE_LIMIT=$n run_prog "$impl" "$QUERY" "$SCENARIO" "$th" >/dev/null     # warm-up
    for ((r=1; r<=RUNS; r++)); do
        v=$(IMAGE_LIMIT=$n run_prog "$impl" "$QUERY" "$SCENARIO" "$th" | get_time)
        echo "$exp,$impl,$th,$n,$SCENARIO,$r,$v" >> "$CSV"
        times+="$v"$'\n'
    done
    MED["$exp|$impl|$th|$n"]=$(printf '%s' "$times" | median)
}

pr() { printf "$@" | tee -a "$TABLE"; }
header() {  # title, first column name
    { echo; echo "=== $1 ==="
      printf "%-8s %-8s %6s %12s %9s %13s %11s\n" "$2" method threads "time(s)" speedup "overhead(s)" "overhead%"
      printf "%-8s %-8s %6s %12s %9s %13s %11s\n" -------- -------- ------ ------------ --------- ------------- -----------; } | tee -a "$TABLE"
}
row() {  # label method threads time seq_time
    local lab=$1 m=$2 t=$3 tm=$4 sq=$5 sp ov ovp
    if [ "$m" = "sequential" ]; then sp="-"; ov="-"; ovp="-"
    else
        sp=$(awk -v s="$sq" -v p="$tm" 'BEGIN{printf "%.2f", s/p}')
        ov=$(awk -v s="$sq" -v p="$tm" 'BEGIN{printf "%+.6f", p-s}')
        ovp=$(awk -v s="$sq" -v p="$tm" 'BEGIN{printf "%+.1f", (p-s)/s*100}')
    fi
    printf "%-8s %-8s %6s %12.6f %9s %13s %11s\n" "$lab" "$m" "$t" "$tm" "$sp" "$ov" "$ovp" | tee -a "$TABLE"
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

echo
echo "Saved: $CSV (all runs), $TABLE (tables), $META"
echo "Next:  python3 graphs/generate_graphs.py"
