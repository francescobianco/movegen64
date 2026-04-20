#pragma once
#include "types.h"

/*
 * path_mask[from][to] — the 64x64 path tensor from path.g64
 * Each cell is a bitmask of squares strictly between from and to
 * along the ray (rank/file/diagonal). Zero for non-collinear pairs.
 * This is the core of the collision-free move filtering.
 */
extern BB path_mask[64][64];

/* geometric reach per piece type per square (ignoring occupation) */
extern BB geo_mask[NPIECES][64];

/* pawn movement tables (direction-aware) */
extern BB pawn_push[2][64];   /* single push target */
extern BB pawn_dpush[2][64];  /* double push target (starting rank only) */
extern BB pawn_atk[2][64];    /* diagonal attack squares */

void tables_init(void);
