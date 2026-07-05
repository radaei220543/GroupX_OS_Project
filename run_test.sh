#!/bin/bash
# Builds everything, runs the full pipeline on a real 1GB file, and
# checks every output the assignment requires — using only plain
# shell commands (md5sum, cmp, stat, grep), nothing compiled.

set -uo pipefail

PASS_COUNT=0
FAIL_COUNT=0
pass() { echo "[PASS] $1"; PASS_COUNT=$((PASS_COUNT+1)); }
fail() { echo "[FAIL] $1"; FAIL_COUNT=$((FAIL_COUNT+1)); }

SERVER_PID=""
cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

echo "=== Step 0: Build ==="
if make all > build.log 2>&1; then
    pass "Build (make all)"
else
    fail "Build (make all) — see build.log"
    exit 1
fi

echo "=== Step 1: Generate 1GB test file ==="
rm -f original.dat reassembled.dat result_min.txt result_max.txt result_sorted.dat execution_log.txt
dd if=/dev/urandom of=original.dat bs=1M count=1024 status=none
if [[ -f original.dat && $(stat -c%s original.dat) -eq 1073741824 ]]; then
    pass "Generated 1GB original.dat"
else
    fail "original.dat generation"
    exit 1
fi

echo "=== Step 2: Start server ==="
./server original.dat > server.log 2>&1 &
SERVER_PID=$!
sleep 1
if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "Server started (pid $SERVER_PID)"
else
    fail "Server failed to start — see server.log"
    exit 1
fi

echo "=== Step 3: Run client -p 4 (auto-launches ./operations -t 8) ==="
./client -p 4 -t 8 > client.log 2>&1
if [[ $? -eq 0 ]]; then
    pass "client -> operations pipeline exited 0"
else
    fail "client -> operations pipeline exited non-zero — see client.log"
fi

echo "=== Step 4: Check reassembled.dat is byte-identical to original.dat ==="
if [[ -f reassembled.dat ]] && cmp -s original.dat reassembled.dat; then
    pass "reassembled.dat matches original.dat (md5sum + byte comparison)"
    md5sum original.dat reassembled.dat
else
    fail "reassembled.dat missing or does not match original.dat"
fi

echo "=== Step 5: Check result_min.txt / result_max.txt format ==="
if [[ -f result_min.txt ]] && grep -qE "^MIN=-?[0-9]+" result_min.txt; then
    pass "result_min.txt exists and matches required format"
    cat result_min.txt
else
    fail "result_min.txt missing or wrong format"
fi
if [[ -f result_max.txt ]] && grep -qE "^MAX=-?[0-9]+" result_max.txt; then
    pass "result_max.txt exists and matches required format"
    cat result_max.txt
else
    fail "result_max.txt missing or wrong format"
fi

echo "=== Step 6: Check result_sorted.dat exists and is the right size ==="
if [[ -f result_sorted.dat ]]; then
    ORIG_SIZE=$(stat -c%s original.dat)
    SORTED_SIZE=$(stat -c%s result_sorted.dat)
    if [[ "$ORIG_SIZE" -eq "$SORTED_SIZE" ]]; then
        pass "result_sorted.dat is the same size as original.dat ($SORTED_SIZE bytes)"
    else
        fail "result_sorted.dat size ($SORTED_SIZE) differs from original.dat ($ORIG_SIZE)"
    fi
else
    fail "result_sorted.dat not found"
fi

echo "=== Step 7: Check execution_log.txt format ==="
if [[ -f execution_log.txt ]]; then
    LOG_OK=1
    grep -q "^\[PART1\] CHUNKS=.*PROCS=.*SYNC_USED=mutex,sem,condvar" execution_log.txt || LOG_OK=0
    grep -q "^\[PART2\] THREADS=.*DATA_PARALLEL=min,max.*TASK_PARALLEL=sort" execution_log.txt || LOG_OK=0
    grep -q "^\[PART2\] TIME_MS=.*SORT_ALGO=parallel_merge_sort" execution_log.txt || LOG_OK=0
    grep -q "^\[STATUS\] SUCCESS" execution_log.txt || LOG_OK=0
    if [[ $LOG_OK -eq 1 ]]; then
        pass "execution_log.txt matches required format"
    else
        fail "execution_log.txt missing required field(s) — see file"
    fi
else
    fail "execution_log.txt not found"
fi

echo ""
echo "=== Summary: $PASS_COUNT passed, $FAIL_COUNT failed ==="
[[ $FAIL_COUNT -eq 0 ]] && exit 0 || exit 1
