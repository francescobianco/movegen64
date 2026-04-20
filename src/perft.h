#pragma once
#include "types.h"
#include "state64.h"

long long perft(const Pos *pos, int depth);
long long state64_perft(const State64 *s, int depth);
long long state64_perft_depth(const State64 *s, int depth, Move prev_move);
void      perft_divide(const Pos *pos, int depth);
