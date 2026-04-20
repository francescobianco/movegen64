#pragma once
#include "types.h"

typedef struct {
    Move moves[256];
    int  count;
} MoveList;

void gen_moves(const Pos *pos, MoveList *ml);
bool in_check(const Pos *pos, int color);
bool sq_attacked(const Pos *pos, int sq, int attacker);
