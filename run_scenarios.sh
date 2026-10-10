#!/usr/bin/env bash
# =========================================================
#  RUN_SCENARIOS.SH - items 1, 2, 8 of the remaining work
#  Runs the 4 scenarios with query-PRESENT and query-ABSENT cases on all
#  three implementations (Sequential, Static Pthreads, Thread Pool),
#  records the execution time (median of RUNS runs) and checks that all
#  three return the same top-5.
#
#    scenarios 1,2,3 : query present in the database  and  query absent
#    scenario  4     : same domain, different image (query_scenario4.png)
#
#  Usage:  sudo ./run_scenarios.sh            (sudo => nice -n -20)
#  Env:    RUNS=5 (default)  THREADS=4 (default)  TASKSET_CPUS=0-3 (optional)
#  Output: live table on screen, scenario_table.txt,
#          scenario_times.csv, scenario_results.csv (top-5 of every run)
# =========================================================
set -uo pipefail
source ./common.sh
ensure_setup
setup_priority

RUNS="${RUNS:-5}"
T="${THREADS:-4}"
TABLE="scenario_table.txt"; TIMES="scenario_times.csv"; RES="scenario_results.csv"

COMBOS=("1:present:$QUERY_PRESENT" "1:absent:$QUERY_ABSENT"
        "2:present:$QUERY_PRESENT" "2:absent:$QUERY_ABSENT"
        "3:present:$QUERY_PRESENT" "3:absent:$QUERY_ABSENT"
        "4:different:$QUERY_S4")

echo "scenario,scenario_name,query_type,query_file,method,threads,median_time_s" > "$TIMES"
echo "scenario,query_type,method,threads,rank,file,similarity" > "$RES"
: > "$TABLE"

{
echo "Scenario-wise comparison  (threads=$T, median of $RUNS runs, priority: $PRIO_NOTE)"
printf "%-3s %-29s %-10s %11s %11s %11s %9s  %-26s %s\n" Sc "Scenario" "Query" "Seq(s)" "Static(s)" "Pool(s)" "Top1sim" "Top1 file" "Same top-5?"
printf "%-3s %-29s %-10s %11s %11s %11s %9s  %-26s %s\n" -- ----------------------------- ---------- ----------- ----------- ----------- --------- -------------------------- -----------
} | tee -a "$TABLE"

ALL_OK=true
for c in "${COMBOS[@]}"; do
    s=${c%%:*}; rest=${c#*:}; qtype=${rest%%:*}; qfile=${rest#*:}
    declare -A MT=()
    declare -A TOP=()
    for impl in seq static pool; do
        th=$T; [ "$impl" = "seq" ] && th=1
        run_prog "$impl" "$qfile" "$s" "$th" >/dev/null          # warm-up
        times=""
        for ((r=1; r<=RUNS; r++)); do
            OUT=$(run_prog "$impl" "$qfile" "$s" "$th")
            times+="$(echo "$OUT" | get_time)"$'\n'
            TOP[$impl]=$(echo "$OUT" | get_top5)
        done
        MT[$impl]=$(printf '%s' "$times" | median)
        echo "$s,\"${SCENARIO_NAMES[$s]}\",$qtype,$qfile,$impl,$th,${MT[$impl]}" >> "$TIMES"
        echo "${TOP[$impl]}" | while IFS='|' read -r rank file sim; do
            echo "$s,$qtype,$impl,$th,$rank,$(basename "$file"),$sim" >> "$RES"
        done
    done
    if [ "${TOP[seq]}" = "${TOP[static]}" ] && [ "${TOP[seq]}" = "${TOP[pool]}" ]; then same="YES"; else same="NO"; ALL_OK=false; fi
    top1_sim=$(echo "${TOP[seq]}" | head -1 | cut -d'|' -f3)
    top1_file=$(basename "$(echo "${TOP[seq]}" | head -1 | cut -d'|' -f2)")
    printf "%-3s %-29s %-10s %11.6f %11.6f %11.6f %9s  %-26s %s\n" "$s" "${SCENARIO_NAMES[$s]}" "$qtype" \
        "${MT[seq]}" "${MT[static]}" "${MT[pool]}" "$top1_sim" "$top1_file" "$same" | tee -a "$TABLE"
    unset MT TOP
done

echo | tee -a "$TABLE"
if $ALL_OK; then echo "All three implementations returned the same top-5 in every scenario." | tee -a "$TABLE"
else echo "WARNING: some scenario gave different top-5 results - see the NO rows." | tee -a "$TABLE"; fi
echo "Saved: $TABLE, $TIMES, $RES"
