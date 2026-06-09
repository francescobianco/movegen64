#include "cost64.h"
#include "bb.h"
#include <string.h>

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

/* does direction class d match slider square e of the given sets? */
static inline bool geo_ok(int d, BB e_bit, BB erq, BB ebq) {
    return BB_ORTH(d) ? (e_bit & erq) != 0 : (e_bit & ebq) != 0;
}

/* ------------------------------------------------------------------ */

static void build_ray(Cost64 *m, int s, int d, BB occ, BB empty) {
    BB g  = bit(s);
    BB a0 = bb_attacks_dir(g, empty, d);     /* cost-0 layer */
    BB b1 = a0 & occ;
    BB a1 = bb_attacks_dir(b1, empty, d);    /* cost-1 layer */
    BB b2 = a1 & occ;
    BB a2 = bb_attacks_dir(b2, empty, d);    /* cost-2 layer */
    BB a3 = bb_attacks_dir(g, ~0ULL, d)      /* rest of the ray */
          & ~(a0 | a1 | a2);

    uint8_t *row = m->c[s];
    BB t;
    t = a0; while (t) { int q = __builtin_ctzll(t); t &= t-1; row[q] = (uint8_t)((d << 4) | 0); }
    t = a1; while (t) { int q = __builtin_ctzll(t); t &= t-1; row[q] = (uint8_t)((d << 4) | 1); }
    t = a2; while (t) { int q = __builtin_ctzll(t); t &= t-1; row[q] = (uint8_t)((d << 4) | 2); }
    t = a3; while (t) { int q = __builtin_ctzll(t); t &= t-1; row[q] = (uint8_t)((d << 4) | 3); }
}

/*
 * Row/column duality: cost is symmetric (blockers between a and b do not
 * depend on travel direction), only the direction nibble flips. A full
 * build therefore needs just the 4 positive directions, writing each cell
 * and its transpose together — half the fills of the naive build.
 */
static const uint8_t OPP8[8] = { 1, 0, 3, 2, 7, 6, 5, 4 };

static void build_ray_sym(Cost64 *m, int s, int d, BB occ, BB empty) {
    BB g  = bit(s);
    BB a0 = bb_attacks_dir(g, empty, d);
    BB b1 = a0 & occ;
    BB a1 = bb_attacks_dir(b1, empty, d);
    BB b2 = a1 & occ;
    BB a2 = bb_attacks_dir(b2, empty, d);
    BB a3 = bb_attacks_dir(g, ~0ULL, d) & ~(a0 | a1 | a2);

    uint8_t *row = m->c[s];
    int od = OPP8[d];
    BB t;
    t = a0; while (t) { int q = __builtin_ctzll(t); t &= t-1; row[q] = (uint8_t)((d << 4) | 0); m->c[q][s] = (uint8_t)((od << 4) | 0); }
    t = a1; while (t) { int q = __builtin_ctzll(t); t &= t-1; row[q] = (uint8_t)((d << 4) | 1); m->c[q][s] = (uint8_t)((od << 4) | 1); }
    t = a2; while (t) { int q = __builtin_ctzll(t); t &= t-1; row[q] = (uint8_t)((d << 4) | 2); m->c[q][s] = (uint8_t)((od << 4) | 2); }
    t = a3; while (t) { int q = __builtin_ctzll(t); t &= t-1; row[q] = (uint8_t)((d << 4) | 3); m->c[q][s] = (uint8_t)((od << 4) | 3); }
}

void cost64_build(const Pos *pos, Cost64 *m) {
    memset(m->c, C64_NONE, sizeof m->c);
    BB occ = pos->occupied, empty = ~occ;

    static const int POS_DIRS[4] = { 0, 2, 4, 5 };  /* N, E, NE, NW */
    for (int i = 0; i < 4; i++)
        for (int s = 0; s < 64; s++)
            build_ray_sym(m, s, POS_DIRS[i], occ, empty);
    m->valid = ~0ULL;
}

/*
 * Incremental transition. The squares whose occupancy flipped are read
 * back through the matrix itself: column c[r][s] of a flip names the one
 * ray of row r that must be refilled. Maintenance is concentrated on the
 * occupied subspace (the only rows generation reads); rows of squares
 * that became or stay empty are left stale and rebuilt on demand.
 */
void cost64_apply(const Cost64 *old, const Pos *newpos, Move mv,
                  int mover, Cost64 *out) {
    *out = *old;

    BB flips = bit(mv.from);
    if (mv.capture == NO_PIECE || (mv.flags & MOVE_EP))
        flips |= bit(mv.to);              /* plain capture: `to` stays occupied */
    if (mv.flags & MOVE_EP)
        flips |= bit((mover == WHITE) ? mv.to - 8 : mv.to + 8);
    if (mv.flags & MOVE_CASTLE) {
        flips |= (mv.to > mv.from) ? bit(mv.to + 1) | bit(mv.to - 1)
                                   : bit(mv.to - 2) | bit(mv.to + 1);
    }

    BB occ = newpos->occupied, empty = ~occ;

    /* refill dirty rays of still-relevant valid rows; let empty rows decay */
    BB rows = out->valid & ~flips;
    while (rows) {
        int r = __builtin_ctzll(rows); rows &= rows - 1;
        int dm = 0;
        BB f = flips;
        while (f) {
            int s = __builtin_ctzll(f); f &= f - 1;
            uint8_t b = old->c[r][s];
            if (b != C64_NONE) dm |= 1 << c64_dir(b);
        }
        if (!dm) continue;
        if (bit(r) & occ) {
            while (dm) { int d = __builtin_ctzll(dm); dm &= dm - 1;
                         build_ray(out, r, d, occ, empty); }
        } else {
            out->valid &= ~bit(r);        /* stale empty row: decay */
        }
    }

    /* rows of flipped squares: rebuild if now occupied, decay otherwise */
    BB f = flips;
    while (f) {
        int s = __builtin_ctzll(f); f &= f - 1;
        if (bit(s) & occ) {
            for (int d = 0; d < 8; d++) build_ray(out, s, d, occ, empty);
            out->valid |= bit(s);
        } else {
            out->valid &= ~bit(s);
        }
    }

    /* any occupied row still stale (piece landed on a decayed square) */
    BB need = occ & ~out->valid;
    while (need) {
        int s = __builtin_ctzll(need); need &= need - 1;
        for (int d = 0; d < 8; d++) build_ray(out, s, d, occ, empty);
        out->valid |= bit(s);
    }
}

/* ------------------------------------------------------------------ */
/* Everything below is reads and comparisons on the matrix: no fills.  */
/* ------------------------------------------------------------------ */

/* is square t attacked by `them`, with x-rays through our king counted?
   (cost-1 cell whose unique blocker is the king = forbidden retreat) */
static bool dangerous(const Pos *pos, const Cost64 *m, int t,
                      int us, int ksq, BB esl, BB erq, BB ebq) {
    int them = 1 - us;
    BB  tb   = bit(t);

    if (bb_knight_atk(tb) & pos->pieces[them][KNIGHT]) return true;
    if (bb_king_atk(tb)   & pos->pieces[them][KING])   return true;
    if (bb_pawn_atk(us, tb) & pos->pieces[them][PAWN]) return true;

    BB sl = esl;
    while (sl) {
        int e = __builtin_ctzll(sl); sl &= sl - 1;
        uint8_t b = m->c[e][t];
        if (b == C64_NONE) continue;
        int d = c64_dir(b);
        if (!geo_ok(d, bit(e), erq, ebq)) continue;
        if (c64_cost(b) == 0) return true;
        if (c64_cost(b) == 1) {
            uint8_t bk = m->c[e][ksq];   /* king is the unique blocker? */
            if (bk != C64_NONE && c64_dir(bk) == d && c64_cost(bk) == 0)
                return true;
        }
    }
    return false;
}

/*
 * En passant legality by cost arithmetic: two squares empty, one fills.
 * For each enemy slider aimed at the king, adjust the stored cost by the
 * blockers removed/added on the path; the move is illegal iff some
 * adjusted cost reaches zero. Pure comparisons, no refill.
 */
static bool ep_legal_m(const Pos *pos, const Cost64 *m, int from, int to,
                       int csq, int us, int ksq, BB esl, BB erq, BB ebq) {
    int them = 1 - us;
    BB  kbb  = bit(ksq);

    if (bb_knight_atk(kbb) & pos->pieces[them][KNIGHT]) return false;
    if (bb_pawn_atk(us, kbb) & (pos->pieces[them][PAWN] & ~bit(csq))) return false;

    BB sl = esl;
    while (sl) {
        int e = __builtin_ctzll(sl); sl &= sl - 1;
        uint8_t b = m->c[e][ksq];
        if (b == C64_NONE) continue;
        int d = c64_dir(b), cost = c64_cost(b);
        if (!geo_ok(d, bit(e), erq, ebq)) continue;
        if (cost >= 3) continue;            /* still >=1 after removing two */

        /* s lies strictly between e and the king iff it sits on the same
           ray with cost not exceeding the king's (cells beyond the king
           cost at least one more, the king itself is excluded) */
        int nc = cost;
        const int rm[2] = { from, csq };
        for (int i = 0; i < 2; i++) {
            uint8_t bs = m->c[e][rm[i]];
            if (rm[i] != ksq && bs != C64_NONE &&
                c64_dir(bs) == d && c64_cost(bs) <= cost) nc--;
        }
        uint8_t bt = m->c[e][to];
        if (to != ksq && bt != C64_NONE &&
            c64_dir(bt) == d && c64_cost(bt) <= cost) nc++;

        if (nc <= 0) return false;
    }
    return true;
}

/* legality for one colour, read off the matrix; `with_ep` only for the
   side to move (the e.p. target belongs to it alone) */
static void gen_side(const Pos *pos, const Cost64 *m, int us, bool with_ep,
                     MoveList *ml) {
    ml->count = 0;

    int them = 1 - us;
    BB  occ      = pos->occupied;
    BB  friendly = pos->by_color[us];
    BB  enemy    = pos->by_color[them];
    BB  empty    = ~occ;
    BB  kbb      = pos->pieces[us][KING];
    int ksq      = __builtin_ctzll(kbb);

    BB erq = pos->pieces[them][ROOK]   | pos->pieces[them][QUEEN];
    BB ebq = pos->pieces[them][BISHOP] | pos->pieces[them][QUEEN];
    BB esl = erq | ebq;

    /* checks and pins: byte reads on column ksq of the slider rows */
    BB checkers = (bb_knight_atk(kbb) & pos->pieces[them][KNIGHT])
                | (bb_pawn_atk(us, kbb) & pos->pieces[them][PAWN]);
    BB checkray = 0;
    BB pinned   = 0;
    BB pin_line[64];

    BB sl = esl;
    while (sl) {
        int e = __builtin_ctzll(sl); sl &= sl - 1;
        uint8_t b = m->c[e][ksq];
        if (b == C64_NONE) continue;
        int d = c64_dir(b), cost = c64_cost(b);
        if (!geo_ok(d, bit(e), erq, ebq)) continue;

        if (cost == 0) {
            /* check: the ray cells e→king at cost 0 are the checkmask */
            checkers |= bit(e);
            const uint8_t *row = m->c[e];
            for (int t = 0; t < 64; t++)
                if (t != ksq && row[t] != C64_NONE &&
                    c64_dir(row[t]) == d && c64_cost(row[t]) == 0)
                    checkray |= bit(t);
        } else if (cost == 1) {
            /* pin: unique blocker = the occupied cost-0 cell on the ray */
            const uint8_t *row = m->c[e];
            int blocker = -1;
            BB line = bit(e);
            for (int t = 0; t < 64; t++) {
                if (t == ksq || row[t] == C64_NONE || c64_dir(row[t]) != d)
                    continue;
                if (c64_cost(row[t]) == 0 && (bit(t) & occ)) blocker = t;
                else if (c64_cost(row[t]) <= 1)              line |= bit(t);
            }
            if (blocker >= 0 && (bit(blocker) & friendly)) {
                pinned |= bit(blocker);
                pin_line[blocker] = line;
            }
        }
    }

    int ncheck    = __builtin_popcountll(checkers);
    BB  checkmask = ncheck ? (checkers | checkray) : ~0ULL;

    /* king moves: each escape square interrogated against the matrix */
    {
        BB dests = bb_king_atk(kbb) & ~friendly;
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests - 1;
            if (!dangerous(pos, m, to, us, ksq, esl, erq, ebq))
                add(ml, ksq, to, KING, piece_on(pos, them, to), NO_PIECE, 0);
        }
    }

    if (ncheck >= 2) return;

    /* knights */
    BB pieces = pos->pieces[us][KNIGHT];
    while (pieces) {
        int from = __builtin_ctzll(pieces); pieces &= pieces - 1;
        BB mm = checkmask;
        if (pinned & bit(from)) mm &= pin_line[from];
        BB dests = bb_knight_atk(bit(from)) & ~friendly & mm;
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests - 1;
            add(ml, from, to, KNIGHT, piece_on(pos, them, to), NO_PIECE, 0);
        }
    }

    /* sliders: destinations are the cost-0 cells of the piece's row */
    for (int pt = BISHOP; pt <= QUEEN; pt++) {
        pieces = pos->pieces[us][pt];
        while (pieces) {
            int from = __builtin_ctzll(pieces); pieces &= pieces - 1;
            BB mm = checkmask;
            if (pinned & bit(from)) mm &= pin_line[from];
            const uint8_t *row = m->c[from];
            for (int to = 0; to < 64; to++) {
                uint8_t b = row[to];
                if (b == C64_NONE || c64_cost(b) != 0) continue;
                int d = c64_dir(b);
                if (pt == ROOK   && !BB_ORTH(d)) continue;
                if (pt == BISHOP &&  BB_ORTH(d)) continue;
                if (!(bit(to) & ~friendly & mm)) continue;
                add(ml, from, to, pt, piece_on(pos, them, to), NO_PIECE, 0);
            }
        }
    }

    /* pawns */
    {
        int push       = (us == WHITE) ? 8 : -8;
        int start_rank = (us == WHITE) ? 1 : 6;
        int promo_rank = (us == WHITE) ? 7 : 0;
        BB  ep_bb      = (with_ep && pos->ep >= 0) ? bit(pos->ep) : 0;

        pieces = pos->pieces[us][PAWN];
        while (pieces) {
            int from = __builtin_ctzll(pieces); pieces &= pieces - 1;
            BB fb = bit(from);
            BB mm = checkmask;
            if (pinned & fb) mm &= pin_line[from];

            BB p1 = bb_shl(fb, push) & empty;
            BB p2 = (p1 && rank_of(from) == start_rank) ? bb_shl(p1, push) & empty : 0;
            BB atk  = bb_pawn_atk(us, fb);
            BB dests = ((p1 | p2) | (atk & enemy)) & mm;

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
                if (ep_legal_m(pos, m, from, to, csq, us, ksq, esl, erq, ebq))
                    add(ml, from, to, PAWN, PAWN, NO_PIECE, MOVE_EP);
            }
        }
    }

    /* castling */
    if (ncheck == 0) {
        if (us == WHITE) {
            if ((pos->castling & CASTLE_WK) && !(occ & 0x60ULL) &&
                !dangerous(pos, m, 5, us, ksq, esl, erq, ebq) &&
                !dangerous(pos, m, 6, us, ksq, esl, erq, ebq))
                add(ml, 4, 6, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
            if ((pos->castling & CASTLE_WQ) && !(occ & 0x0EULL) &&
                !dangerous(pos, m, 3, us, ksq, esl, erq, ebq) &&
                !dangerous(pos, m, 2, us, ksq, esl, erq, ebq))
                add(ml, 4, 2, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
        } else {
            if ((pos->castling & CASTLE_BK) && !(occ & 0x6000000000000000ULL) &&
                !dangerous(pos, m, 61, us, ksq, esl, erq, ebq) &&
                !dangerous(pos, m, 62, us, ksq, esl, erq, ebq))
                add(ml, 60, 62, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
            if ((pos->castling & CASTLE_BQ) && !(occ & 0x0E00000000000000ULL) &&
                !dangerous(pos, m, 59, us, ksq, esl, erq, ebq) &&
                !dangerous(pos, m, 58, us, ksq, esl, erq, ebq))
                add(ml, 60, 58, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
        }
    }
}

void cost64_gen(const Pos *pos, const Cost64 *m, MoveList *ml) {
    gen_side(pos, m, pos->turn, true, ml);
}

/* the universal-state read: both colours' legal moves from one matrix,
   no per-side state reconstruction (the off-turn side as if it moved) */
void cost64_gen_both(const Pos *pos, const Cost64 *m,
                     MoveList *white, MoveList *black) {
    gen_side(pos, m, WHITE, pos->turn == WHITE, white);
    gen_side(pos, m, BLACK, pos->turn == BLACK, black);
}