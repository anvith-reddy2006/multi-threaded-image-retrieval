#!/bin/bash

set -e

RUNS="${RUNS:-10}"
THREADS=(1 2 4 8 16 32)
IMAGE_SIZES=(100 250 500 1000 2500 5000 7500 10000)

QUERY="query.png"
SCENARIO=1

CSV="benchmark_results.csv"
TABLE="benchmark_table.txt"
META="benchmark_meta.txt"

echo "================================================="
echo " MULTI-THREADED IMAGE RETRIEVAL BENCHMARK"
echo "================================================="

if [ ! -f "$QUERY" ]; then
    echo "ERROR: $QUERY not found"
    exit 1
fi

for bin in retrieval_seq retrieval_mt retrieval_threadpool; do
    if [ ! -x "./$bin" ]; then
        echo "ERROR: ./$bin not found"
        exit 1
    fi
done

TOTAL_IMAGES=$(find dataset/train -maxdepth 1 -name "*.png" | wc -l)

echo "Dataset images: $TOTAL_IMAGES"
echo "Runs: $RUNS (+1 warm-up)"
echo "Thread counts: ${THREADS[*]}"
echo "Nice priority: -20"
echo

echo "date: $(date -Iseconds)" > "$META"
echo "runs: $RUNS (+1 warm-up)" >> "$META"
echo "dataset: $TOTAL_IMAGES images" >> "$META"
echo "query: $QUERY" >> "$META"
echo "scenario: $SCENARIO" >> "$META"
echo "threads: ${THREADS[*]}" >> "$META"

echo "images,method,threads,time,speedup,overhead,overhead_percent" > "$CSV"
> "$TABLE"

median()
{
    sort -n | awk '
    {
        a[NR]=$1
    }
    END {
        if (NR % 2 == 1)
            printf "%.6f\n", a[(NR+1)/2]
        else
            printf "%.6f\n", (a[NR/2] + a[NR/2+1])/2
    }'
}

run_seq()
{
    local images=$1

    sudo env IMAGE_LIMIT="$images" nice -n -20 ./retrieval_seq "$QUERY" "$SCENARIO" \
        > /dev/null

    times=""

    for ((r=1; r<=RUNS; r++)); do
        output=$(sudo env IMAGE_LIMIT="$images" nice -n -20 ./retrieval_seq "$QUERY" "$SCENARIO")

        time=$(echo "$output" | grep "Execution Time:" | awk '{print $3}')

        times="${times}${time}"$'\n'
    done

    echo "$times" | median
}

run_static()
{
    local images=$1
    local threads=$2

    sudo env IMAGE_LIMIT="$images" nice -n -20 ./retrieval_mt "$QUERY" "$SCENARIO" "$threads" \
        > /dev/null

    times=""

    for ((r=1; r<=RUNS; r++)); do
        output=$(sudo env IMAGE_LIMIT="$images" nice -n -20 ./retrieval_mt "$QUERY" "$SCENARIO" "$threads")

        time=$(echo "$output" | grep "Execution Time:" | awk '{print $3}')

        times="${times}${time}"$'\n'
    done

    echo "$times" | median
}

run_pool()
{
    local images=$1
    local threads=$2

    sudo env IMAGE_LIMIT="$images" nice -n -20 ./retrieval_threadpool "$QUERY" "$SCENARIO" "$threads" \
        > /dev/null

    times=""

    for ((r=1; r<=RUNS; r++)); do
        output=$(sudo env IMAGE_LIMIT="$images" nice -n -20 ./retrieval_threadpool "$QUERY" "$SCENARIO" "$threads")

        time=$(echo "$output" | grep -i "Execution time:" | awk '{print $3}')

        times="${times}${time}"$'\n'
    done

    echo "$times" | median
}

calculate_metrics()
{
    local seq=$1
    local parallel=$2

    awk -v s="$seq" -v p="$parallel" '
    BEGIN {
        speedup=s/p
        overhead=p-s
        overhead_percent=(overhead/s)*100

        printf "%.2f %.6f %.2f\n",
               speedup,
               overhead,
               overhead_percent
    }'
}

echo
echo "================ THREAD SCALING ================"
echo

echo "images  method       threads  time(s)   speedup   overhead(s)  overhead%"
echo "------  -----------  -------  --------  --------  -----------  ---------"

IMAGE_LIMIT=10000

SEQ_TIME=$(run_seq $IMAGE_LIMIT)

printf "%-7s %-12s %-8s %-9s %-9s %-12s %-10s\n" \
    "$IMAGE_LIMIT" "sequential" "1" "$SEQ_TIME" "-" "-" "-"

for threads in "${THREADS[@]}"; do

    STATIC_TIME=$(run_static $IMAGE_LIMIT $threads)
    METRICS=$(calculate_metrics "$SEQ_TIME" "$STATIC_TIME")

    SPEEDUP=$(echo "$METRICS" | awk '{print $1}')
    OVERHEAD=$(echo "$METRICS" | awk '{print $2}')
    OVERHEAD_PERCENT=$(echo "$METRICS" | awk '{print $3}')

    printf "%-7s %-12s %-8s %-9s %-9s %-12s %-10s\n" \
        "$IMAGE_LIMIT" "static" "$threads" "$STATIC_TIME" \
        "$SPEEDUP" "$OVERHEAD" "$OVERHEAD_PERCENT"

    echo "$IMAGE_LIMIT,static,$threads,$STATIC_TIME,$SPEEDUP,$OVERHEAD,$OVERHEAD_PERCENT" >> "$CSV"

    POOL_TIME=$(run_pool $IMAGE_LIMIT $threads)
    METRICS=$(calculate_metrics "$SEQ_TIME" "$POOL_TIME")

    SPEEDUP=$(echo "$METRICS" | awk '{print $1}')
    OVERHEAD=$(echo "$METRICS" | awk '{print $2}')
    OVERHEAD_PERCENT=$(echo "$METRICS" | awk '{print $3}')

    printf "%-7s %-12s %-8s %-9s %-9s %-12s %-10s\n" \
        "$IMAGE_LIMIT" "pool" "$threads" "$POOL_TIME" \
        "$SPEEDUP" "$OVERHEAD" "$OVERHEAD_PERCENT"

    echo "$IMAGE_LIMIT,pool,$threads,$POOL_TIME,$SPEEDUP,$OVERHEAD,$OVERHEAD_PERCENT" >> "$CSV"

done

echo
echo "================ CROSSOVER ================"
echo

echo "threads = 4 and 8"
echo

echo "images  method       threads  time(s)   speedup   overhead(s)  overhead%"
echo "------  -----------  -------  --------  --------  -----------  ---------"

for images in "${IMAGE_SIZES[@]}"; do

    if [ "$images" -gt "$TOTAL_IMAGES" ]; then
        continue
    fi

    SEQ_TIME=$(run_seq "$images")

    for threads in 4 8; do

        STATIC_TIME=$(run_static "$images" "$threads")
        METRICS=$(calculate_metrics "$SEQ_TIME" "$STATIC_TIME")

        SPEEDUP=$(echo "$METRICS" | awk '{print $1}')
        OVERHEAD=$(echo "$METRICS" | awk '{print $2}')
        OVERHEAD_PERCENT=$(echo "$METRICS" | awk '{print $3}')

        printf "%-7s %-12s %-8s %-9s %-9s %-12s %-10s\n" \
            "$images" "static" "$threads" "$STATIC_TIME" \
            "$SPEEDUP" "$OVERHEAD" "$OVERHEAD_PERCENT"

        echo "$images,static,$threads,$STATIC_TIME,$SPEEDUP,$OVERHEAD,$OVERHEAD_PERCENT" >> "$CSV"

        POOL_TIME=$(run_pool "$images" "$threads")
        METRICS=$(calculate_metrics "$SEQ_TIME" "$POOL_TIME")

        SPEEDUP=$(echo "$METRICS" | awk '{print $1}')
        OVERHEAD=$(echo "$METRICS" | awk '{print $2}')
        OVERHEAD_PERCENT=$(echo "$METRICS" | awk '{print $3}')

        printf "%-7s %-12s %-8s %-9s %-9s %-12s %-10s\n" \
            "$images" "pool" "$threads" "$POOL_TIME" \
            "$SPEEDUP" "$OVERHEAD" "$OVERHEAD_PERCENT"

        echo "$images,pool,$threads,$POOL_TIME,$SPEEDUP,$OVERHEAD,$OVERHEAD_PERCENT" >> "$CSV"

    done
done

echo
echo "Benchmark complete."
echo "CSV: $CSV"
echo "Metadata: $META"
