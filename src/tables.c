#include "tables.h"
#include <string.h>

BB path_mask[64][64];
BB geo_mask[NPIECES][64];
BB pawn_push[2][64];
BB pawn_dpush[2][64];
BB pawn_atk[2][64];

/*
 * Build the path tensor: for each (from, to) pair that lies on the same
 * rank, file, or diagonal, store the bitmask of squares strictly between.
 * Non-collinear pairs get 0 — they are never reachable by sliding pieces.
 */
static void init_path(void) {
    memset(path_mask, 0, sizeof(path_mask));
    for (int from = 0; from < 64; from++) {
        int ff = file_of(from), rf = rank_of(from);
        for (int to = 0; to < 64; to++) {
            if (from == to) continue;
            int ft = file_of(to), rt = rank_of(to);
            int dr = rt - rf, df = ft - ff;
            int steps, sr, sf;
            if      (dr == 0)            { steps = abs(df); sr = 0;          sf = df>0?1:-1; }
            else if (df == 0)            { steps = abs(dr); sr = dr>0?1:-1;  sf = 0;         }
            else if (abs(dr) == abs(df)) { steps = abs(dr); sr = dr>0?1:-1;  sf = df>0?1:-1; }
            else continue; /* not collinear */

            BB mask = 0;
            for (int i = 1; i < steps; i++)
                mask |= bit(sq(rf + i*sr, ff + i*sf));
            path_mask[from][to] = mask;
        }
    }
}

static void init_geo(void) {
    /* Knights */
    static const int kdr[] = {-2,-2,-1,-1, 1, 1, 2, 2};
    static const int kdf[] = {-1, 1,-2, 2,-2, 2,-1, 1};
    for (int s = 0; s < 64; s++) {
        int r = rank_of(s), f = file_of(s);
        BB m = 0;
        for (int i = 0; i < 8; i++) {
            int nr = r+kdr[i], nf = f+kdf[i];
            if (nr>=0 && nr<8 && nf>=0 && nf<8) m |= bit(sq(nr,nf));
        }
        geo_mask[KNIGHT][s] = m;
    }

    /* King */
    for (int s = 0; s < 64; s++) {
        int r = rank_of(s), f = file_of(s);
        BB m = 0;
        for (int dr = -1; dr <= 1; dr++)
            for (int df = -1; df <= 1; df++) {
                if (!dr && !df) continue;
                int nr = r+dr, nf = f+df;
                if (nr>=0 && nr<8 && nf>=0 && nf<8) m |= bit(sq(nr,nf));
            }
        geo_mask[KING][s] = m;
    }

    /* Sliding pieces: all collinear squares (path_mask handles blocking) */
    for (int s = 0; s < 64; s++) {
        BB rook = 0, bishop = 0;
        int r = rank_of(s), f = file_of(s);
        for (int t = 0; t < 64; t++) {
            if (t == s) continue;
            int dr = rank_of(t)-r, df = file_of(t)-f;
            if (dr == 0 || df == 0)      rook   |= bit(t);
            if (abs(dr) == abs(df))      bishop |= bit(t);
        }
        geo_mask[ROOK][s]   = rook;
        geo_mask[BISHOP][s] = bishop;
        geo_mask[QUEEN][s]  = rook | bishop;
    }
}

static void init_pawns(void) {
    for (int s = 0; s < 64; s++) {
        int r = rank_of(s), f = file_of(s);

        pawn_push[WHITE][s]  = (r < 7) ? bit(s+8) : 0;
        pawn_dpush[WHITE][s] = (r == 1) ? bit(s+16) : 0;
        BB wa = 0;
        if (r < 7) { if (f>0) wa|=bit(s+7); if (f<7) wa|=bit(s+9); }
        pawn_atk[WHITE][s] = wa;

        pawn_push[BLACK][s]  = (r > 0) ? bit(s-8) : 0;
        pawn_dpush[BLACK][s] = (r == 6) ? bit(s-16) : 0;
        BB ba = 0;
        if (r > 0) { if (f>0) ba|=bit(s-9); if (f<7) ba|=bit(s-7); }
        pawn_atk[BLACK][s] = ba;
    }
}

void tables_init(void) {
    init_path();
    init_geo();
    init_pawns();
}
