#pragma once
#include "types.h"

/*
 * Shift-arithmetic primitives shared by legal64 and cost64.
 * No lookup tables anywhere: the 64-bit word is treated as an 8x8 grid
 * via wrap masks, and sliding rays are the geometric series of the
 * one-step shift operator masked by empty squares (Kogge-Stone).
 */

#define FILE_A 0x0101010101010101ULL
#define FILE_H 0x8080808080808080ULL

typedef struct { int shift; BB mask; } BBDir;

static const BBDir BB_DIRS[8] = {
    { +8, ~0ULL    },   /* 0 north */
    { -8, ~0ULL    },   /* 1 south */
    { +1, ~FILE_A  },   /* 2 east  */
    { -1, ~FILE_H  },   /* 3 west  */
    { +9, ~FILE_A  },   /* 4 north-east */
    { +7, ~FILE_H  },   /* 5 north-west */
    { -7, ~FILE_A  },   /* 6 south-east */
    { -9, ~FILE_H  },   /* 7 south-west */
};
/* dirs 0..3 are rook lines, 4..7 bishop lines */
#define BB_ORTH(d) ((d) < 4)

static inline BB bb_shl(BB b, int s) { return s >= 0 ? b << s : b >> -s; }

/* gen | gen·E·D | gen·(E·D)^2 | ... closed under 7 steps, 3 squarings */
static inline BB bb_occl_fill(BB gen, BB empty, int d) {
    int s  = BB_DIRS[d].shift;
    BB pro = empty & BB_DIRS[d].mask;
    gen |= pro & bb_shl(gen, s);
    pro &= bb_shl(pro, s);
    gen |= pro & bb_shl(gen, 2*s);
    pro &= bb_shl(pro, 2*s);
    gen |= pro & bb_shl(gen, 4*s);
    return gen;
}

/* squares reached along d: empty run + first blocker (cost-0 layer) */
static inline BB bb_attacks_dir(BB gen, BB empty, int d) {
    return bb_shl(bb_occl_fill(gen, empty, d), BB_DIRS[d].shift) & BB_DIRS[d].mask;
}

static inline BB bb_knight_atk(BB n) {
    BB l1 = (n >> 1) & ~FILE_H;
    BB l2 = (n >> 2) & ~(FILE_H | (FILE_H >> 1));
    BB r1 = (n << 1) & ~FILE_A;
    BB r2 = (n << 2) & ~(FILE_A | (FILE_A << 1));
    BB h1 = l1 | r1;
    BB h2 = l2 | r2;
    return (h1 << 16) | (h1 >> 16) | (h2 << 8) | (h2 >> 8);
}

static inline BB bb_king_atk(BB k) {
    BB a = ((k << 1) & ~FILE_A) | ((k >> 1) & ~FILE_H);
    BB r = a | k;
    return (a | (r << 8) | (r >> 8)) & ~k;
}

static inline BB bb_pawn_atk(int color, BB p) {
    if (color == WHITE)
        return ((p << 7) & ~FILE_H) | ((p << 9) & ~FILE_A);
    return ((p >> 9) & ~FILE_H) | ((p >> 7) & ~FILE_A);
}