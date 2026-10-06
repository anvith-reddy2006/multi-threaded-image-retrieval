#!/bin/bash
# =========================================================
#  RUN_SCENARIOS.SH  -  workflow scenarios from the design
#  Runs every query in queries/ (+ query.png) through all versions,
#  checks every version's top-5 equals the sequential top-5, and
#  writes scenario_results.csv
#  Usage: ./run_scenarios.sh        (run tools/make_queries.py first)
# =========================================================
set -uo pipefail

THREADS=(2 4 8)
CSV="scenario_results.csv"
BINS=(retrieval_mt:static retrieval_pool:pool retrieval_pool_barrier:pool_barrier)

for b in retrieval_seq retrieval_mt retrieval_pool retrieval_pool_barrier; do
    [ -x "./$b" ] || { echo "ERROR: ./$b missing. Run 'make all' first."; exit 1; }
done
ls queries/*.png >/dev/null 2>&1 || { echo "ERROR: no queries/. Run: python3 tools/make_queries.py"; exit 1; }

echo "scenario,query_file,method,threads,rank,result_file,distance" > "$CSV"

top5() {  # prints "rank|file|dist" lines from a program's stdout
    grep -E '^[0-9]+\. ' | sed -E 's/^([0-9]+)\. ([^ ]+) \| Euclidean Dist: ([0-9]+)/\1|\2|\3/' | head -5
}

append_csv() {  # scenario query method threads  (reads rank|file|dist on stdin)
    while IFS='|' read -r rank file dist; do
        echo "$1,$2,$3,$4,$rank,$(basename "$file"),$dist" >> "$CSV"
    done
}

ALL_OK=true
QUERIES=(queries/*.png query.png)

for q in "${QUERIES[@]}"; do
    base=$(basename "$q" .png)
    case "$base" in
        exact_*)  scenario="exact";   src="${base#exact_}" ;;
        histeq_*) scenario="histeq";  src="${base#histeq_}" ;;
        gauss_*)  scenario="gauss";   src="${base#gauss_}" ;;
        *)        scenario="same_domain"; src="" ;;
    esac

    BASE_OUT=$(./retrieval_seq "$q" 2>/dev/null | top5)
    echo "$BASE_OUT" | append_csv "$scenario" "$q" sequential 1

    echo "=== $scenario : $q ==="
    echo "$BASE_OUT" | awk -F'|' '{n=split($2,a,"/"); printf "  %s. %s  dist=%s\n",$1,a[n],$3}'

    if [ -n "$src" ]; then
        hit=$(echo "$BASE_OUT" | awk -F'|' -v id="image_${src}_" 'index($2,id){print $1"|"$3}')
        if [ -n "$hit" ]; then
            echo "  -> source image_${src} found at rank ${hit%%|*} (distance ${hit##*|})"
        else
            echo "  -> source image_${src} NOT in top-5"
        fi
    fi

    for entry in "${BINS[@]}"; do
        bin=${entry%%:*}; method=${entry##*:}
        for t in "${THREADS[@]}"; do
            OUT=$(./$bin "$q" "$t" 2>/dev/null | top5)
            echo "$OUT" | append_csv "$scenario" "$q" "$method" "$t"
            if [ "$OUT" != "$BASE_OUT" ]; then
                echo "  FAIL: $method @ $t threads differs from sequential"
                ALL_OK=false
            fi
        done
    done
    echo
done

if $ALL_OK; then
    echo "All versions agree with sequential on every query."
else
    echo "SOME VERSIONS DISAGREED - see FAIL lines above."; exit 1
fi
echo "Results written to $CSV"
