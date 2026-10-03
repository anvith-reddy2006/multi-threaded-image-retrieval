#!/bin/bash
# =========================================================
#  VERIFY.SH  —  check that all versions give identical top-5
# =========================================================
set -euo pipefail

QUERY="${1:-query.png}"
THREADS=(1 2 4 8 16)
PASS=true

if [ ! -f "$QUERY" ]; then
    echo "ERROR: query image '$QUERY' not found"
    exit 1
fi

for bin in retrieval_seq retrieval_mt retrieval_pool; do
    if [ ! -x "./$bin" ]; then
        echo "ERROR: ./$bin not found or not executable.  Run 'make all' first."
        exit 1
    fi
done

# Get sequential baseline (always deterministic)
BASELINE=$(./retrieval_seq "$QUERY" 2>/dev/null | grep '^[0-9]\.' | head -5)
if [ -z "$BASELINE" ]; then
    echo "ERROR: Baseline run failed or returned no results."
    exit 1
fi
echo "=== Sequential baseline ==="
echo "$BASELINE"
echo

for t in "${THREADS[@]}"; do
    echo "--- Threads: $t ---"

    MT_OUT=$(./retrieval_mt "$QUERY" "$t" 2>/dev/null | grep '^[0-9]\.' | head -5)
    POOL_OUT=$(./retrieval_pool "$QUERY" "$t" 2>/dev/null | grep '^[0-9]\.' | head -5)

    if [ "$MT_OUT" != "$BASELINE" ]; then
        echo "FAIL: retrieval_mt at $t threads differs from sequential"
        echo "  Expected: $BASELINE"
        echo "  Got:      $MT_OUT"
        PASS=false
    else
        echo "  retrieval_mt:   PASS"
    fi

    if [ "$POOL_OUT" != "$BASELINE" ]; then
        echo "FAIL: retrieval_pool at $t threads differs from sequential"
        echo "  Expected: $BASELINE"
        echo "  Got:      $POOL_OUT"
        PASS=false
    else
        echo "  retrieval_pool: PASS"
    fi
done

echo
if $PASS; then
    echo "ALL TESTS PASSED"
else
    echo "SOME TESTS FAILED"
    exit 1
fi
