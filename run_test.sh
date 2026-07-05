#!/bin/bash
# Builds, runs the full pipeline on a 1GB test file, and validates outputs.

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
CLIENT_EXIT=$?
if [[ $CLIENT_EXIT -eq 0 ]]; then
    pass "client -> operations pipeline exited 0"
else
    fail "client -> operations pipeline exited $CLIENT_EXIT — see client.log"
fi

echo "=== Step 4: Validate reassembled.dat ==="
if [[ -f reassembled.dat ]]; then
    HASH_A=$(md5sum original.dat | awk '{print $1}')
    HASH_B=$(md5sum reassembled.dat | awk '{print $1}')
    if [[ "$HASH_A" == "$HASH_B" ]]; then
        pass "md5sum match (original.dat == reassembled.dat)"
    else
        fail "md5sum mismatch"
    fi

    if diff <(hexdump -C original.dat) <(hexdump -C reassembled.dat) > /dev/null; then
        pass "hexdump diff: 0 differences"
    else
        fail "hexdump diff: mismatch found"
    fi
else
    fail "reassembled.dat not found"
fi

echo "=== Step 5: Validate min/max against ground truth ==="
if [[ -f result_min.txt && -f result_max.txt ]]; then
    GROUND_TRUTH=$(python3 -c "
import struct
with open('original.dat', 'rb') as f:
    data = f.read()
n = len(data) // 4
vals = struct.unpack('<%di' % n, data)
print(min(vals), max(vals))
")
    TRUE_MIN=$(echo "$GROUND_TRUTH" | awk '{print $1}')
    TRUE_MAX=$(echo "$GROUND_TRUTH" | awk '{print $2}')

    FILE_MIN=$(grep -oP 'MIN=\K-?[0-9]+' result_min.txt)
    FILE_MAX=$(grep -oP 'MAX=\K-?[0-9]+' result_max.txt)

    if [[ "$FILE_MIN" == "$TRUE_MIN" ]]; then
        pass "result_min.txt matches ground truth ($FILE_MIN)"
    else
        fail "result_min.txt: got '$FILE_MIN', expected '$TRUE_MIN'"
    fi

    if [[ "$FILE_MAX" == "$TRUE_MAX" ]]; then
        pass "result_max.txt matches ground truth ($FILE_MAX)"
    else
        fail "result_max.txt: got '$FILE_MAX', expected '$TRUE_MAX'"
    fi
else
    fail "result_min.txt / result_max.txt not found"
fi

echo "=== Step 6: Validate result_sorted.dat ==="
if [[ -f result_sorted.dat ]]; then
    if python3 -c "
import struct, sys
with open('original.dat', 'rb') as f:
    data = f.read()
n = len(data) // 4
expected = struct.pack('<%di' % n, *sorted(struct.unpack('<%di' % n, data)))
with open('result_sorted.dat', 'rb') as f:
    actual = f.read()
sys.exit(0 if expected == actual else 1)
"; then
        pass "result_sorted.dat is correctly and completely sorted"
    else
        fail "result_sorted.dat does not match sorted ground truth"
    fi
else
    fail "result_sorted.dat not found"
fi

echo "=== Step 7: Validate execution_log.txt format ==="
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
