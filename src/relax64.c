#include "relax64.h"
#include "bb.h"
#include "position.h"
#include <string.h>

/*
 * One direction, all 64 source rows at once. The propagator is the same
 * word for every row, the shift amount is a compile-time constant: the
 * loops are straight-line data movement, friendly to auto-vectorization.
 */
#define SH(x, s) ((s) >= 0 ? (x) << (s) : (x) >> -(s))

#define RELAX_DIR(sft, wrapmask)                                          \
    do {                                                                  \
        const BB mask = (wrapmask);                                       \
        BB pro0 = empty & mask;                                           \
        BB pro;                                                           \
        /* seed: one source bit per row */                                \
        for (int i = 0; i < 64; i++) g[i] = bit(i);                       \
        /* layer 0 */                                                     \
        pro = pro0;                                                       \
        for (int i = 0; i < 64; i++) g[i] |= pro & SH(g[i], (sft));       \
        pro &= SH(pro, (sft));                                            \
        for (int i = 0; i < 64; i++) g[i] |= pro & SH(g[i], 2*(sft));     \
        pro &= SH(pro, 2*(sft));                                          \
        for (int i = 0; i < 64; i++) g[i] |= pro & SH(g[i], 4*(sft));     \
        for (int i = 0; i < 64; i++) {                                    \
            BB a = SH(g[i], (sft)) & mask;                                \
            r->L0[i] |= a;                                                \
            g[i] = a & occ;            /* first blockers, next seed */    \
        }                                                                 \
        /* layer 1 */                                                     \
        pro = pro0;                                                       \
        for (int i = 0; i < 64; i++) g[i] |= pro & SH(g[i], (sft));       \
        pro &= SH(pro, (sft));                                            \
        for (int i = 0; i < 64; i++) g[i] |= pro & SH(g[i], 2*(sft));     \
        pro &= SH(pro, 2*(sft));                                          \
        for (int i = 0; i < 64; i++) g[i] |= pro & SH(g[i], 4*(sft));     \
        for (int i = 0; i < 64; i++) {                                    \
            BB a = SH(g[i], (sft)) & mask;                                \
            r->L1[i] |= a;                                                \
            g[i] = a & occ;                                               \
        }                                                                 \
        /* layer 2 */                                                     \
        pro = pro0;                                                       \
        for (int i = 0; i < 64; i++) g[i] |= pro & SH(g[i], (sft));       \
        pro &= SH(pro, (sft));                                            \
        for (int i = 0; i < 64; i++) g[i] |= pro & SH(g[i], 2*(sft));     \
        pro &= SH(pro, 2*(sft));                                          \
        for (int i = 0; i < 64; i++) g[i] |= pro & SH(g[i], 4*(sft));     \
        for (int i = 0; i < 64; i++)                                      \
            r->L2[i] |= SH(g[i], (sft)) & mask;                           \
    } while (0)

void relax64_step(BB occ, Relax64 *r) {
    BB empty = ~occ;
    BB g[64];

    memset(r, 0, sizeof *r);

    RELAX_DIR(+8, ~0ULL);      /* north      */
    RELAX_DIR(-8, ~0ULL);      /* south      */
    RELAX_DIR(+1, ~FILE_A);    /* east       */
    RELAX_DIR(-1, ~FILE_H);    /* west       */
    RELAX_DIR(+9, ~FILE_A);    /* north-east */
    RELAX_DIR(+7, ~FILE_H);    /* north-west */
    RELAX_DIR(-7, ~FILE_A);    /* south-east */
    RELAX_DIR(-9, ~FILE_H);    /* south-west */
}

/* ------------------------------------------------------------------ */
/* Plane-based legal generation.                                       */
/* ------------------------------------------------------------------ */

/* line masks as computed constants — geometry as arithmetic, no tables */
#define MAIN_DIAG 0x8040201008040201ULL   /* a1-h8 */
#define ANTI_DIAG 0x0102040810204080ULL   /* h1-a8 */

static inline BB rank_line(int s) { return 0xFFULL << (s & 56); }
static inline BB file_line(int s) { return FILE_A << (s & 7); }
static inline BB diag_line(int s) {
    int d = (s >> 3) - (s & 7);
    return d >= 0 ? MAIN_DIAG << (8 * d) : MAIN_DIAG >> (-8 * d);
}
static inline BB anti_line(int s) {
    int a = (s >> 3) + (s & 7) - 7;
    return a >= 0 ? ANTI_DIAG << (8 * a) : ANTI_DIAG >> (-8 * a);
}
static inline BB orth_lines(int s) { return rank_line(s) | file_line(s); }
static inline BB diag_lines(int s) { return diag_line(s) | anti_line(s); }

/* the one line through both a and b (caller guarantees collinearity) */
static inline BB line_through(int a, int b) {
    BB t = bit(b);
    if (rank_line(a) & t) return rank_line(a);
    if (file_line(a) & t) return file_line(a);
    if (diag_line(a) & t) return diag_line(a);
    return anti_line(a);
}

/* squares with index strictly above / below s (ray sides are monotonic) */
static inline BB above_sq(int s) { return ~((bit(s) << 1) - 1); }
static inline BB below_sq(int s) { return bit(s) - 1; }

static inline int piece_on(const Pos *pos, int color, int sqr) {
    for (int p = 0; p < NPIECES; p++)
        if (pos->pieces[color][p] & bit(sqr)) return p;
    return NO_PIECE;
}

static inline void add(MoveList *ml, int from, int to, int piece,
                       int cap, int promo, int flags) {
    Move *m    = &ml->moves[ml->count++];
    m->from    = (uint8_t)from;
    m->to      = (uint8_t)to;
    m->piece   = (uint8_t)piece;
    m->capture = (uint8_t)cap;
    m->promo   = (uint8_t)promo;
    m->flags   = (uint8_t)flags;
}

/* en passant: two squares empty at once — re-run the fill functionally
   on the modified occupancy (rare; identical to legal64's test) */
static bool ep_legal_r(const Pos *pos, int from, int to, int csq, int us) {
    int them = 1 - us;
    BB  kbb  = pos->pieces[us][KING];

    if (bb_knight_atk(kbb) & pos->pieces[them][KNIGHT]) return false;
    if (bb_pawn_atk(us, kbb) & (pos->pieces[them][PAWN] & ~bit(csq))) return false;

    BB occ2   = (pos->occupied ^ bit(from) ^ bit(csq)) | bit(to);
    BB empty2 = ~occ2;
    BB erq = pos->pieces[them][ROOK]   | pos->pieces[them][QUEEN];
    BB ebq = pos->pieces[them][BISHOP] | pos->pieces[them][QUEEN];

    for (int d = 0; d < 4; d++)
        if (bb_attacks_dir(kbb, empty2, d) & occ2 & erq) return false;
    for (int d = 4; d < 8; d++)
        if (bb_attacks_dir(kbb, empty2, d) & occ2 & ebq) return false;
    return true;
}

void relax64_gen(const Pos *pos, const Relax64 *r, MoveList *ml) {
    ml->count = 0;

    int us = pos->turn, them = 1 - us;
    BB  occ      = pos->occupied;
    BB  friendly = pos->by_color[us];
    BB  enemy    = pos->by_color[them];
    BB  empty    = ~occ;
    BB  kbb      = pos->pieces[us][KING];
    int ksq      = __builtin_ctzll(kbb);

    BB erq = pos->pieces[them][ROOK]   | pos->pieces[them][QUEEN];
    BB ebq = pos->pieces[them][BISHOP] | pos->pieces[them][QUEEN];

    /* danger map: leaper shifts + slider plane rows; the squares behind
       the king are the L1 row of any slider that sees him (his shadow) */
    BB danger = bb_pawn_atk(them, pos->pieces[them][PAWN])
              | bb_knight_atk(pos->pieces[them][KNIGHT])
              | bb_king_atk(pos->pieces[them][KING]);
    BB sl = erq | ebq;
    while (sl) {
        int e = __builtin_ctzll(sl); sl &= sl - 1;
        BB geo = 0;
        if (bit(e) & erq) geo |= orth_lines(e);
        if (bit(e) & ebq) geo |= diag_lines(e);
        BB reach = r->L0[e] & geo;
        danger |= reach;
        if (reach & kbb) {
            BB l = line_through(e, ksq);
            BB side = (e < ksq) ? above_sq(ksq) : below_sq(ksq);
            danger |= r->L1[e] & l & side;
        }
    }

    /* checkers: leapers by shift sets, sliders by one plane read */
    BB checkers = (bb_knight_atk(kbb) & pos->pieces[them][KNIGHT])
                | (bb_pawn_atk(us, kbb) & pos->pieces[them][PAWN]);
    BB checkray = 0;
    BB slcheck  = (r->L0[ksq] & orth_lines(ksq) & erq)
                | (r->L0[ksq] & diag_lines(ksq) & ebq);
    checkers |= slcheck;
    while (slcheck) {
        int e = __builtin_ctzll(slcheck); slcheck &= slcheck - 1;
        BB l = line_through(ksq, e);
        checkray |= (r->L0[ksq] & r->L0[e] & l) | bit(e);
    }

    /* pins: pinner = enemy slider in the king's L1 row on matching lines;
       the pinned piece is the AND of the two cost-0 rows */
    BB pinned = 0;
    BB pin_line[64];
    BB pinners = (r->L1[ksq] & orth_lines(ksq) & erq)
               | (r->L1[ksq] & diag_lines(ksq) & ebq);
    while (pinners) {
        int p = __builtin_ctzll(pinners); pinners &= pinners - 1;
        BB l = line_through(ksq, p);
        BB b = r->L0[ksq] & r->L0[p] & l & occ;   /* the unique blocker */
        if (b & friendly) {
            pinned |= b;
            pin_line[__builtin_ctzll(b)] = ((r->L0[ksq] | r->L1[ksq]) & l) & ~b;
        }
    }

    int ncheck    = __builtin_popcountll(checkers);
    BB  checkmask = ncheck ? (checkers | checkray) : ~0ULL;

    /* king */
    {
        BB dests = bb_king_atk(kbb) & ~friendly & ~danger;
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests - 1;
            add(ml, ksq, to, KING, piece_on(pos, them, to), NO_PIECE, 0);
        }
    }

    if (ncheck >= 2) return;

    /* knights */
    BB pieces = pos->pieces[us][KNIGHT];
    while (pieces) {
        int from = __builtin_ctzll(pieces); pieces &= pieces - 1;
        BB m = checkmask;
        if (pinned & bit(from)) m &= pin_line[from];
        BB dests = bb_knight_atk(bit(from)) & ~friendly & m;
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests - 1;
            add(ml, from, to, KNIGHT, piece_on(pos, them, to), NO_PIECE, 0);
        }
    }

    /* sliders: reach is one plane row masked by the piece's lines */
    for (int pt = BISHOP; pt <= QUEEN; pt++) {
        pieces = pos->pieces[us][pt];
        while (pieces) {
            int from = __builtin_ctzll(pieces); pieces &= pieces - 1;
            BB geo = (pt == ROOK)   ? orth_lines(from) :
                     (pt == BISHOP) ? diag_lines(from) : ~0ULL;
            BB m = checkmask;
            if (pinned & bit(from)) m &= pin_line[from];
            BB dests = r->L0[from] & geo & ~friendly & m;
            while (dests) {
                int to = __builtin_ctzll(dests); dests &= dests - 1;
                add(ml, from, to, pt, piece_on(pos, them, to), NO_PIECE, 0);
            }
        }
    }

    /* pawns */
    {
        int push       = (us == WHITE) ? 8 : -8;
        int start_rank = (us == WHITE) ? 1 : 6;
        int promo_rank = (us == WHITE) ? 7 : 0;
        BB  ep_bb      = (pos->ep >= 0) ? bit(pos->ep) : 0;

        pieces = pos->pieces[us][PAWN];
        while (pieces) {
            int from = __builtin_ctzll(pieces); pieces &= pieces - 1;
            BB fb = bit(from);
            BB m  = checkmask;
            if (pinned & fb) m &= pin_line[from];

            BB p1 = bb_shl(fb, push) & empty;
            BB p2 = (p1 && rank_of(from) == start_rank) ? bb_shl(p1, push) & empty : 0;
            BB atk  = bb_pawn_atk(us, fb);
            BB dests = ((p1 | p2) | (atk & enemy)) & m;

            while (dests) {
                int to = __builtin_ctzll(dests); dests &= dests - 1;
                int cap = (bit(to) & enemy) ? piece_on(pos, them, to) : NO_PIECE;
                if (rank_of(to) == promo_rank) {
                    for (int p = KNIGHT; p <= QUEEN; p++)
                        add(ml, from, to, PAWN, cap, p, 0);
                } else {
                    add(ml, from, to, PAWN, cap, NO_PIECE, 0);
                }
            }

            if (atk & ep_bb) {
                int to  = pos->ep;
                int csq = (us == WHITE) ? to - 8 : to + 8;
                if (ep_legal_r(pos, from, to, csq, us))
                    add(ml, from, to, PAWN, PAWN, NO_PIECE, MOVE_EP);
            }
        }
    }

    /* castling */
    if (ncheck == 0) {
        if (us == WHITE) {
            if ((pos->castling & CASTLE_WK) &&
                !(occ & 0x60ULL) && !(danger & 0x60ULL))
                add(ml, 4, 6, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
            if ((pos->castling & CASTLE_WQ) &&
                !(occ & 0x0EULL) && !(danger & 0x0CULL))
                add(ml, 4, 2, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
        } else {
            if ((pos->castling & CASTLE_BK) &&
                !(occ & 0x6000000000000000ULL) && !(danger & 0x6000000000000000ULL))
                add(ml, 60, 62, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
            if ((pos->castling & CASTLE_BQ) &&
                !(occ & 0x0E00000000000000ULL) && !(danger & 0x0C00000000000000ULL))
                add(ml, 60, 58, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
        }
    }
}
