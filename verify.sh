#!/bin/bash
# =========================================================
#  VERIFY.SH - correctness check (item 8 of the remaining work)
#  For every query (present / absent / scenario-4) and every scenario 1-4,
#  checks that Static Pthreads and Thread Pool return exactly the same
#  top-5 (file names AND similarity values) as the Sequential version,
#  for thread counts 1,2,4,8,16,32.
#  Also checks: a query that is present in the database is found with
#  similarity 1.0000 (scenario 1), and an absent query is not.
#  Usage: ./verify.sh
# =========================================================
set -uo pipefail
source ./common.sh
ensure_setup
PRIO=()          # correctness only - no need for priority

THREADS=(1 2 4 8 16 32)
QUERIES=("present:$QUERY_PRESENT" "absent:$QUERY_ABSENT" "scenario4:$QUERY_S4")
FAIL=0; TOTAL=0

for qe in "${QUERIES[@]}"; do
    qtype=${qe%%:*}; qfile=${qe#*:}
    for s in 1 2 3 4; do
        BASE=$(run_prog seq "$qfile" "$s" 1 | get_top5)
        if [ -z "$BASE" ]; then echo "FAIL: sequential gave no result ($qtype, scenario $s)"; FAIL=$((FAIL+1)); continue; fi
        line="  [$qtype | scenario $s ${SCENARIO_NAMES[$s]}]"
        bad=""
        for impl in static pool; do
            for t in "${THREADS[@]}"; do
                TOTAL=$((TOTAL+1))
                OUT=$(run_prog "$impl" "$qfile" "$s" "$t" | get_top5)
                if [ "$OUT" != "$BASE" ]; then bad+=" $impl@$t"; FAIL=$((FAIL+1)); fi
            done
        done
        if [ -z "$bad" ]; then echo "$line PASS (static+pool, all thread counts)"; else echo "$line FAIL:$bad"; fi
        # sanity: present query found exactly (scenario 1 only)
        if [ "$s" = "1" ]; then
            top1=$(echo "$BASE" | head -1 | cut -d'|' -f3)
            if [ "$qtype" = "present" ]; then
                [ "$top1" = "1.0000" ] && echo "      present query found with similarity 1.0000: OK" \
                                       || { echo "      FAIL: present query top-1 similarity is $top1 (expected 1.0000)"; FAIL=$((FAIL+1)); }
            elif [ "$qtype" = "absent" ]; then
                [ "$top1" != "1.0000" ] && echo "      absent query: best similarity $top1 (< 1.0000): OK" \
                                        || { echo "      FAIL: absent query matched with 1.0000 - it is in the database"; FAIL=$((FAIL+1)); }
            fi
        fi
    done
done

echo
if [ "$FAIL" -eq 0 ]; then echo "ALL TESTS PASSED ($TOTAL comparisons)"; else echo "$FAIL PROBLEM(S) FOUND"; exit 1; fi
