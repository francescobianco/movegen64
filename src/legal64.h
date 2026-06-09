#pragma once
#include "types.h"
#include "movegen.h"

/*
 * legal64 — direct legal move generation, no lookup tables, no make/unmake.
 *
 * Sliding attacks are computed as the geometric series of the one-step
 * shift operator masked by empty squares (Kogge-Stone fills):
 *
 *     A_d = D_d (E D_d)*        boolean semiring, closed form in 3 squarings
 *
 * Re-running the series from the first blocker gives the cost-1 layer
 * (x-rays), which is where pins live. Check evasion, pin restriction and
 * king safety are pure mask intersections; the rare en-passant cases are
 * decided by re-running the fill on a functionally modified occupancy.
 * Every generated move is legal by construction.
 */
void legal64_gen(const Pos *pos, MoveList *ml);
long long legal64_perft(const Pos *pos, int depth);