#pragma once
#include "types.h"

void pos_set_start(Pos *pos);
void pos_from_fen(Pos *pos, const char *fen);
Pos  pos_after(const Pos *pos, Move m);
void pos_print(const Pos *pos);
