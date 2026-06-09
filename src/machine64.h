#pragma once
#include "types.h"
#include "movegen.h"

/*
 * machine64 — the closed-loop machine: input is the output of the columns.
 *
 * Persistent state is the diagonal alone: 64 bytes. Everything else is
 * recomputed by transport each step (the memoryless relax). One step:
 *
 *   R (emit)        rows broadcast reach from the diagonal   — relax64_step
 *   C (collect)     columns gather at the kings: checks, pins, danger
 *   A (annihilate)  row output ∧ column feedback kills illegal cells
 *   T (transport)   the selected cell moves one byte on the diagonal
 *
 * The machine's input is a cell of the legal set that the columns just
 * produced; its only memory between steps is the diagonal. Game metadata
 * travels as transport bits on the diagonal itself:
 *
 *   bit 0-2  piece+1 (0 = empty square)
 *   bit 3    colour
 *   bit 4    virgin bit (castling rights live on the K/R home cells)
 *   bit 5    en-passant ghost (set on the skipped square, expires next step)
 *   bit 7 of cell 0: side to move
 */
typedef struct { uint8_t d[64]; } Diag64;

void machine64_from_pos(const Pos *pos, Diag64 *m);
void machine64_to_pos(const Diag64 *m, Pos *pos);

/* C + A phases: the legal cells, read from the planes of the diagonal */
void machine64_gen(const Diag64 *m, MoveList *ml);

/* T phase: pure byte transport on the diagonal */
void machine64_apply(Diag64 *m, Move mv);

long long machine64_perft(const Diag64 *m, int depth);
