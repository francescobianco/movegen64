#!/bin/bash
# Benchmark: movegen64 vs Stockfish perft
# Usage: ./bench.sh [max_depth]   default: 6

MAX=${1:-6}
SF=$(which stockfish 2>/dev/null)

echo "========================================"
echo " movegen64 vs Stockfish — perft bench"
echo " depth 1..$MAX  |  position: startpos"
echo "========================================"

# ---- movegen64 ----
echo ""
echo "[ movegen64 ]"
./movegen64 "$MAX" 2>/dev/null | grep -E "^(depth|-----|[0-9])"

# ---- Stockfish ----
if [ -z "$SF" ]; then
    echo ""
    echo "[ Stockfish ] not found — install with: sudo apt install stockfish"
    exit 0
fi

echo ""
echo "[ Stockfish $(stockfish --version 2>/dev/null | head -1) ]"
echo "%-6s  %15s  %10s  %12s" "depth" "nodes" "time(ms)" "Mnodes/s"
echo "$(printf '%-6s  %15s  %10s  %12s' '-----' '---------------' '--------' '--------')"

for d in $(seq 1 "$MAX"); do
    t0=$(date +%s%N)
    output=$(echo -e "position startpos\ngo perft $d\nquit" | "$SF" 2>/dev/null)
    t1=$(date +%s%N)
    ms=$(( (t1 - t0) / 1000000 ))
    nodes=$(echo "$output" | grep -i "^Nodes searched:" | awk '{print $NF}')
    if [ -z "$nodes" ]; then
        nodes=$(echo "$output" | grep -i "nodes" | tail -1 | awk '{print $NF}')
    fi
    if [ -n "$nodes" ] && [ "$ms" -gt 0 ]; then
        mnps=$(awk "BEGIN { printf \"%.2f\", ($nodes / 1000000.0) / ($ms / 1000.0) }")
    else
        mnps="n/a"
    fi
    printf "%-6d  %15s  %10d  %12s\n" "$d" "$nodes" "$ms" "$mnps"
done

echo ""
echo "========================================"
echo " NOTE: Stockfish perft includes move"
echo " generation + make/unmake overhead."
echo " movegen64 uses path_mask tensor only."
echo "========================================"
