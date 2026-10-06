#!/bin/bash
# =========================================================
#  BENCHMARK.SH  -  warm-up + RUNS runs, medians, CSV output
#
#  Usage:
#    ./benchmark.sh [query.png]                 data-size sweep (MAX_IMAGES = 500 1000 2500 5000)
#    ./benchmark.sh [query.png] search_repeat   SEARCH_REPEAT sweep (1 10 50 100) at all images
#
#  Environment:
#    RUNS=10            runs per configuration (after one warm-up)
#    TASKSET_CPUS=0-3   optional: pin every run to these CPUs with taskset
#    CSV=...            output file (default benchmark_results.csv)
#
#  Each run is started with "nice -n -20" when permitted (use sudo / root),
#  to reduce interference from other processes.
# =========================================================
set -euo pipefail

QUERY="${1:-query.png}"
MODE="${2:-sweep}"
RUNS="${RUNS:-10}"
THREADS=(1 2 4 8 16)
IMAGE_SIZES=(500 1000 2500 5000)
SR_VALUES=(1 10 50 100)
CSV="${CSV:-benchmark_results.csv}"
META="benchmark_meta.txt"

if [ ! -f "$QUERY" ]; then
    echo "ERROR: query image '$QUERY' not found"
    exit 1
fi
if [ "$MODE" != "sweep" ] && [ "$MODE" != "search_repeat" ]; then
    echo "ERROR: unknown mode '$MODE' (use: sweep | search_repeat)"
    exit 1
fi

for bin in retrieval_seq retrieval_mt retrieval_pool retrieval_pool_barrier; do
    if [ ! -x "./$bin" ]; then
        echo "ERROR: ./$bin not found or not executable.  Run 'make all' first."
        exit 1
    fi
done

TOTAL_IMAGES=$(ls dataset/train/*.png 2>/dev/null | wc -l)
if [ "$TOTAL_IMAGES" -eq 0 ]; then
    echo "ERROR: dataset/train is empty. Run: python3 src/convert_cifar10.py"
    exit 1
fi

# ---- priority / pinning --------------------------------------------------
PRIO=()
if [ "$(nice -n -20 nice 2>/dev/null || echo 0)" = "-20" ]; then
    PRIO=(nice -n -20)
    PRIO_NOTE="nice -n -20 (active)"
else
    PRIO_NOTE="NOT set (need sudo/root for nice -n -20) - results may be noisy"
    echo "WARNING: cannot raise priority; run with sudo for cleaner timings."
fi
PIN=()
PIN_NOTE="none"
if [ -n "${TASKSET_CPUS:-}" ] && command -v taskset >/dev/null 2>&1; then
    PIN=(taskset -c "$TASKSET_CPUS")
    PIN_NOTE="taskset -c $TASKSET_CPUS"
fi

# ---- metadata ------------------------------------------------------------
{
    echo "date:        $(date -Iseconds)"
    echo "mode:        $MODE"
    echo "runs:        $RUNS (+1 warm-up)"
    echo "cpu cores:   $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo unknown)"
    echo "priority:    $PRIO_NOTE"
    echo "pinning:     $PIN_NOTE"
    echo "dataset:     $TOTAL_IMAGES images"
    echo "query:       $QUERY"
} > "$META"
cat "$META"
echo

echo "method,threads,images,search_repeat,run,t_index,t_search,t_total,vol_csw,invol_csw" > "$CSV"
TABLE="${TABLE:-benchmark_table.txt}"
: > "$TABLE"
SEQ_MED_TOTAL=""

median() {  # reads numbers on stdin, prints the median
    sort -n | awk '{a[NR]=$1} END{ if (NR==0) print "nan"; else if (NR%2) print a[(NR+1)/2]; else printf "%.6f\n", (a[NR/2]+a[NR/2+1])/2 }'
}

table_header() {  # IMAGES SR
    {
        echo
        echo "=== images=$1  search_repeat=$2  (median of $RUNS runs, seconds) ==="
        printf "%-13s %4s %10s %10s %10s %9s %7s\n" method thr t_index t_search t_total speedup eff%
        printf "%-13s %4s %10s %10s %10s %9s %7s\n" ------------- ---- ---------- ---------- ---------- --------- -------
    } | tee -a "$TABLE"
}

table_row() {  # METHOD THREADS MED_INDEX MED_SEARCH MED_TOTAL
    local M=$1 T=$2 MI=$3 MS=$4 MT=$5 SP="-" EF="-"
    if [ "$M" = "sequential" ]; then
        SEQ_MED_TOTAL="$MT"
    elif [ -n "$SEQ_MED_TOTAL" ]; then
        SP=$(awk -v s="$SEQ_MED_TOTAL" -v t="$MT" 'BEGIN{printf "%.2f", s/t}')
        EF=$(awk -v s="$SEQ_MED_TOTAL" -v t="$MT" -v n="$T" 'BEGIN{printf "%.0f", s/t/n*100}')
    fi
    printf "%-13s %4s %10.6f %10.6f %10.6f %9s %7s\n" "$M" "$T" "$MI" "$MS" "$MT" "$SP" "$EF" | tee -a "$TABLE"
}

exec_bin() {  # exec_bin BIN THREADS IMAGES SR   (prints program output)
    local BIN=$1 T=$2 IMG=$3 SR=$4
    local ARGS=("$QUERY")
    [ "$BIN" != "retrieval_seq" ] && ARGS+=("$T")
    env MAX_IMAGES="$IMG" SEARCH_REPEAT="$SR" "${PRIO[@]+"${PRIO[@]}"}" "${PIN[@]+"${PIN[@]}"}" "./$BIN" "${ARGS[@]}" 2>&1
}

run_benchmark() {  # BIN METHOD THREADS IMAGES SR
    local BIN=$1 METHOD=$2 T=$3 IMG=$4 SR=$5
    exec_bin "$BIN" "$T" "$IMG" "$SR" > /dev/null          # warm-up
    local LI="" LS="" LT=""
    for ((r=1; r<=RUNS; r++)); do
        OUTPUT=$(exec_bin "$BIN" "$T" "$IMG" "$SR")
        T_INDEX=$(echo "$OUTPUT" | grep 'T_index:'  | awk '{print $2}')
        T_SEARCH=$(echo "$OUTPUT" | grep 'T_search:' | awk '{print $2}')
        T_TOTAL=$(echo "$OUTPUT" | grep 'T_total:'  | awk '{print $2}')
        CSW=$(echo "$OUTPUT" | grep 'Context switches:') || true
        if [ -n "$CSW" ]; then
            VOL_CSW=$(echo "$CSW" | awk '{print $3}')
            INVOL_CSW=$(echo "$CSW" | awk '{print $5}')
        else
            VOL_CSW=0; INVOL_CSW=0
        fi
        echo "$METHOD,$T,$IMG,$SR,$r,$T_INDEX,$T_SEARCH,$T_TOTAL,$VOL_CSW,$INVOL_CSW" >> "$CSV"
        LI+="$T_INDEX"$'\n'; LS+="$T_SEARCH"$'\n'; LT+="$T_TOTAL"$'\n'
    done
    table_row "$METHOD" "$T" "$(printf '%s' "$LI" | median)" "$(printf '%s' "$LS" | median)" "$(printf '%s' "$LT" | median)"
}

run_all_methods() {  # IMAGES SR
    local IMG=$1 SR=$2
    SEQ_MED_TOTAL=""
    table_header "$IMG" "$SR"
    run_benchmark retrieval_seq sequential 1 "$IMG" "$SR"
    for t in "${THREADS[@]}"; do
        run_benchmark retrieval_mt           static       "$t" "$IMG" "$SR"
        run_benchmark retrieval_pool         pool         "$t" "$IMG" "$SR"
        run_benchmark retrieval_pool_barrier pool_barrier "$t" "$IMG" "$SR"
    done
}

echo "========================================"
echo " MULTI-THREADED IMAGE RETRIEVAL BENCHMARK ($MODE)"
echo "========================================"

if [ "$MODE" = "sweep" ]; then
    for IMG in "${IMAGE_SIZES[@]}"; do
        [ "$IMG" -gt "$TOTAL_IMAGES" ] && continue
        run_all_methods "$IMG" 1
    done
else
    for SR in "${SR_VALUES[@]}"; do
        run_all_methods "$TOTAL_IMAGES" "$SR"
    done
fi

echo
echo "Results written to $CSV, table saved in $TABLE (metadata in $META)"
echo "Run: python3 graphs/generate_graphs.py $CSV"
