#!/bin/sh
set -eu

BIN=${BIN:-./movegen64}

run() {
    name=$1
    depth=$2
    expected=$3
    fen=${4:-}

    if [ -n "$fen" ]; then
        out=$("$BIN" "$depth" fen "$fen")
    else
        out=$("$BIN" "$depth")
    fi

    nodes=$(printf '%s\n' "$out" | awk -v d="$depth" '$1 == d && $2 ~ /^[0-9]+$/ { print $2; exit }')
    if [ "$nodes" != "$expected" ]; then
        printf 'FAIL %-16s depth %s: expected %s, got %s\n' "$name" "$depth" "$expected" "${nodes:-missing}" >&2
        exit 1
    fi

    if printf '%s\n' "$out" | grep -q 'MISMATCH'; then
        printf 'FAIL %-16s depth %s: copy/delta mismatch\n' "$name" "$depth" >&2
        exit 1
    fi

    printf 'PASS %-16s depth %s: %s\n' "$name" "$depth" "$nodes"
}

run startpos 5 4865609
run kiwipete 4 2919688 "r3k2r/p1ppqpb1/bn2pnp1/2pP4/1p2P3/2N2N2/PPPBQPPP/R3K2R w KQkq - 0 1"
run promotion 4 422333 "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"
run endgame 5 674624 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"
