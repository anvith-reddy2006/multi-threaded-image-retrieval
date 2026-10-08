#!/bin/bash
# Shared helpers for verify.sh, run_scenarios.sh and benchmark.sh  (source this file)

QUERY_PRESENT="queries/present.png"      # an image that IS in the database
QUERY_ABSENT="query_absent.png"          # an image that is NOT in the database
QUERY_S4="query_scenario4.png"           # scenario 4: same domain, different image
SCENARIO_NAMES=("" "Original" "Histogram equalization" "Gaussian smoothing" "Same domain, different image")

ensure_setup() {
    local b
    for b in retrieval_seq retrieval_mt retrieval_pool; do
        [ -x "./$b" ] || { echo "ERROR: ./$b missing. Run 'make all' first."; exit 1; }
    done
    ls dataset/train/*.png >/dev/null 2>&1 || { echo "ERROR: dataset/train is empty. Run: python3 src/convert_cifar10.py"; exit 1; }
    for b in "$QUERY_ABSENT" "$QUERY_S4"; do
        [ -f "$b" ] || { echo "ERROR: $b not found in the project folder."; exit 1; }
    done
    # "query present" = an exact copy of a database image (the first one, sorted by name)
    mkdir -p queries
    if [ ! -f "$QUERY_PRESENT" ]; then
        cp "dataset/train/$(ls dataset/train | sort | head -1)" "$QUERY_PRESENT"
    fi
}

# priority: nice -n -20 when allowed (needs sudo), optional CPU pinning via TASKSET_CPUS
setup_priority() {
    PRIO=()
    if [ "$(nice -n -20 nice 2>/dev/null || echo 0)" = "-20" ]; then
        PRIO=(nice -n -20); PRIO_NOTE="nice -n -20 (active)"
    else
        PRIO_NOTE="NOT set (run with sudo for nice -n -20) - timings may be noisy"
        echo "WARNING: cannot raise priority; run with sudo for cleaner timings."
    fi
    PIN_NOTE="none"
    if [ -n "${TASKSET_CPUS:-}" ] && command -v taskset >/dev/null 2>&1; then
        PRIO+=(taskset -c "$TASKSET_CPUS"); PIN_NOTE="taskset -c $TASKSET_CPUS"
    fi
}

# run_prog IMPL QUERY SCENARIO THREADS  -> program output on stdout
# IMPL = seq | static | pool     (IMAGE_LIMIT in the environment limits the dataset size)
run_prog() {
    local impl=$1 q=$2 s=$3 t=$4
    case $impl in
        seq)    "${PRIO[@]+"${PRIO[@]}"}" ./retrieval_seq  "$q" "$s"      2>&1 ;;
        static) "${PRIO[@]+"${PRIO[@]}"}" ./retrieval_mt   "$q" "$s" "$t" 2>&1 ;;
        pool)   "${PRIO[@]+"${PRIO[@]}"}" ./retrieval_pool "$q" "$s" "$t" 2>&1 ;;
    esac
}

# parse program output (stdin)
get_time() { grep -i 'execution time' | awk '{print $3}'; }
get_top5() { grep -E '^[0-9]+\. ' | head -5 | sed -E 's/^([0-9]+)\. ([^ ]+) \| Similarity: ([0-9.]+)/\1|\2|\3/'; }

median() {  # numbers on stdin -> median
    sort -n | awk '{a[NR]=$1} END{ if (NR==0) print "nan"; else if (NR%2) print a[(NR+1)/2]; else printf "%.6f\n", (a[NR/2]+a[NR/2+1])/2 }'
}
