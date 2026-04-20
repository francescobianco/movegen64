#pragma once
#include "types.h"
#include "state64.h"

long long perft(const Pos *pos, int depth);
long long state64_perft(const State64 *s, int depth);
void      perft_divide(const Pos *pos, int depth);
