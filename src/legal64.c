#include "legal64.h"
#include "position.h"
#include "bb.h"

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

/*
 * En passant is the one move where two squares empty at once; instead of
 * special-casing every discovered-check geometry, re-run the slider fill
 * on the functionally modified occupancy. Pure computation, no state.
 */
static bool ep_legal(const Pos *pos, int from, int to, int csq, int us) {
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

/* ------------------------------------------------------------------ */

void legal64_gen(const Pos *pos, MoveList *ml) {
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

    /* danger map: enemy attacks with our king removed from occupancy,
       so the king cannot hide in his own shadow along a check ray */
    BB empty_nk = empty | kbb;
    BB danger = bb_pawn_atk(them, pos->pieces[them][PAWN])
              | bb_knight_atk(pos->pieces[them][KNIGHT])
              | bb_king_atk(pos->pieces[them][KING]);
    for (int d = 0; d < 4; d++) danger |= bb_attacks_dir(erq, empty_nk, d);
    for (int d = 4; d < 8; d++) danger |= bb_attacks_dir(ebq, empty_nk, d);

    /* king-centric staged fills: cost-0 layer finds slider checks,
       cost-1 layer (refill from the first blocker) finds pins */
    BB checkers = (bb_knight_atk(kbb) & pos->pieces[them][KNIGHT])
                | (bb_pawn_atk(us, kbb) & pos->pieces[them][PAWN]);
    BB checkray = 0;
    BB pinned   = 0;
    BB pin_line[64];

    for (int d = 0; d < 8; d++) {
        BB sliders = (d < 4) ? erq : ebq;
        BB a0 = bb_attacks_dir(kbb, empty, d);
        BB b1 = a0 & occ;
        if (b1 & sliders) {
            checkers |= b1;
            checkray |= a0;
        } else if (b1 & friendly) {
            BB a1 = bb_attacks_dir(b1, empty, d);
            if (a1 & occ & sliders) {
                pinned |= b1;
                pin_line[__builtin_ctzll(b1)] = (a0 | a1) & ~b1;
            }
        }
    }

    int ncheck    = __builtin_popcountll(checkers);
    BB  checkmask = ncheck ? (checkers | checkray) : ~0ULL;

    /* king moves: geometry minus friends minus attacked squares */
    {
        BB dests = bb_king_atk(kbb) & ~friendly & ~danger;
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests - 1;
            add(ml, ksq, to, KING, piece_on(pos, them, to), NO_PIECE, 0);
        }
    }

    if (ncheck >= 2) return;   /* double check: king moves only */

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

    /* sliders: per-piece directional fills */
    for (int pt = BISHOP; pt <= QUEEN; pt++) {
        int d0 = (pt == BISHOP) ? 4 : 0;
        int d1 = (pt == ROOK)   ? 4 : 8;
        pieces = pos->pieces[us][pt];
        while (pieces) {
            int from = __builtin_ctzll(pieces); pieces &= pieces - 1;
            BB m = checkmask;
            if (pinned & bit(from)) m &= pin_line[from];
            BB dests = 0;
            for (int d = d0; d < d1; d++)
                dests |= bb_attacks_dir(bit(from), empty, d);
            dests &= ~friendly & m;
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
            BB caps = atk & enemy;
            BB dests = ((p1 | p2) & m) | (caps & m);

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
                if (ep_legal(pos, from, to, csq, us))
                    add(ml, from, to, PAWN, PAWN, NO_PIECE, MOVE_EP);
            }
        }
    }

    /* castling: path empty, transit squares outside the danger map;
       ncheck == 0 already guaranteed here for the king's own square */
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

long long legal64_perft(const Pos *pos, int depth) {
    MoveList ml;
    legal64_gen(pos, &ml);

    if (depth == 1) return ml.count;   /* every move is legal: just count */

    long long nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        Pos next = pos_after(pos, ml.moves[i]);
        nodes += legal64_perft(&next, depth - 1);
    }
    return nodes;
}