#pragma once
#include "types.h"

/*
 * relax64 — the transport-only autofeedback step.
 *
 * The whole cost structure is recomputed unconditionally from occupancy:
 * no dirty tracking, no deltas, no copy, no history. One step is pure data
 * movement (shifts/and/or over 64 row-bitboards, the same Kogge-Stone
 * closure as everywhere else), identical instruction flow regardless of
 * the position: the memoryless relaxation  M = F(occ).
 *
 * Layers are stored as bit-planes: row s of L_k is the bitboard of squares
 * reachable from s across exactly k blockers (k = 0,1,2). Together with
 * L0|L1|L2 these are the cost layers of the Cost64 byte matrix; direction
 * is recoverable from square geometry when needed.
 */
typedef struct {
    BB L0[64];   /* cost-0: slides / direct attacks  */
    BB L1[64];   /* cost-1: x-rays, pins             */
    BB L2[64];   /* cost-2: e.p. discovered geometry */
} Relax64;

void relax64_step(BB occ, Relax64 *r);

/*
 * Plane-based legal generation: every legality concept is a boolean
 * combination of plane rows and arithmetic line masks. Highlights:
 *   pinned piece       = L0[king] & L0[pinner]      (one AND)
 *   checkmask          = L0[king] & L0[checker] on their line
 *   forbidden retreat  = L1[attacker] beyond the king
 */
#include "movegen.h"
void relax64_gen(const Pos *pos, const Relax64 *r, MoveList *ml);
