#pragma once
#include "types.h"
#include "movegen.h"

/*
 * Incremental engine: one global position + one legal matrix.
 * make/unmake update only the rows in the influence zone of the
 * moved squares — typically 5-10 rows out of 64.
 */

#define UNDO_MAX_ZONE 56   /* max squares in influence zone of 2 squares */

typedef struct {
    /* position snapshot (for unmake) */
    BB  pieces_snap[2][NPIECES];
    int old_ep, old_castling, old_halfmove, old_fullmove, old_turn;
    int ep_cap_sq;          /* EP captured pawn square, -1 otherwise */
    /* legal matrix delta */
    int n;
    int sq[UNDO_MAX_ZONE];
    BB  saved[UNDO_MAX_ZONE];
} Undo;

void      engine_init(const Pos *pos);
void      engine_make(Move m, Undo *u);
void      engine_unmake(const Undo *u);
bool      engine_in_check(void);
void      engine_gen_moves(MoveList *ml);
long long engine_perft(int depth);
const Pos *engine_cur(void);
