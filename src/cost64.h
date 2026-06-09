#pragma once
#include "types.h"
#include "movegen.h"

/*
 * cost64 — the 64x64 ray-cost matrix: the position transformed into the
 * space where legality is comparison.
 *
 * Every square is treated as a virtual queen. Cell c[from][to] encodes,
 * for collinear pairs, the ray direction and the number of occupied
 * squares strictly between from and to (the "cost"):
 *
 *     c[from][to] = (dir << 4) | cost     cost: 0,1,2, 3 = three or more
 *     c[from][to] = 0xFF                  not collinear
 *
 * Reading the matrix:
 *     cost 0                      → slider move / direct attack
 *     cost 1                      → x-ray; pin if the unique blocker is
 *                                   a friendly piece and `to` is the king
 *     cost 1 with king as blocker → square the king may not retreat to
 *     cost 2 on the e.p. rank     → the classic en-passant discovered check
 *
 * The matrix is built from shift fills only (see bb.h); cost64_gen then
 * derives the full legal move list from byte reads and comparisons.
 */

#define C64_NONE 0xFF
#define c64_dir(b)  ((b) >> 4)
#define c64_cost(b) ((b) & 0x0F)

/*
 * `valid` tracks which rows are current. Generation only ever reads rows
 * of occupied squares, so maintenance is deformed onto the occupied
 * subspace: incremental updates keep occupied rows exact and let rows of
 * empty squares go stale (they are rebuilt on demand when a piece lands).
 */
typedef struct { uint8_t c[64][64]; BB valid; } Cost64;

void cost64_build(const Pos *pos, Cost64 *m);
void cost64_gen(const Pos *pos, const Cost64 *m, MoveList *ml);

/* row/column duality: the matrix is colour-blind universal state — both
   sides' legal moves are readable from it in one pass (the off-turn side
   "as if it were to move", without en passant) */
void cost64_gen_both(const Pos *pos, const Cost64 *m,
                     MoveList *white, MoveList *black);

/* incremental transition: refill only the rays crossing flipped squares,
   reading the dirty set from the matrix itself (columns of the flips) */
void cost64_apply(const Cost64 *old, const Pos *newpos, Move mv,
                  int mover, Cost64 *out);