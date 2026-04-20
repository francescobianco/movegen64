#include "movegen.h"
#include "tables.h"

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

static inline int piece_on(const Pos *pos, int color, int sq) {
    for (int p = 0; p < NPIECES; p++)
        if (pos->pieces[color][p] & bit(sq)) return p;
    return NO_PIECE;
}

/*
 * Core of the project: legal destinations for a sliding piece.
 * For each candidate destination, check path_mask[from][to] against
 * the occupied bitboard — a single AND, no ray walking.
 */
static BB slider_dests(int from, BB geo, BB occupied, BB friendly) {
    BB result = 0;
    BB cands  = geo & ~friendly;
    while (cands) {
        int to = __builtin_ctzll(cands); cands &= cands-1;
        if (!(path_mask[from][to] & occupied))
            result |= bit(to);
    }
    return result;
}

/* Is square `sq` attacked by any piece of color `attacker`? */
bool sq_attacked(const Pos *pos, int sq, int attacker) {
    BB occ = pos->occupied;

    if (geo_mask[KNIGHT][sq] & pos->pieces[attacker][KNIGHT]) return true;
    if (geo_mask[KING][sq]   & pos->pieces[attacker][KING])   return true;

    /* pawn_atk[1-attacker][sq] gives the squares from which
       an attacker-color pawn can reach sq diagonally */
    if (pawn_atk[1-attacker][sq] & pos->pieces[attacker][PAWN]) return true;

    BB rq = pos->pieces[attacker][ROOK]  | pos->pieces[attacker][QUEEN];
    BB bq = pos->pieces[attacker][BISHOP]| pos->pieces[attacker][QUEEN];

    BB tmp = rq;
    while (tmp) {
        int s = __builtin_ctzll(tmp); tmp &= tmp-1;
        if ((geo_mask[ROOK][s] & bit(sq)) && !(path_mask[s][sq] & occ))
            return true;
    }
    tmp = bq;
    while (tmp) {
        int s = __builtin_ctzll(tmp); tmp &= tmp-1;
        if ((geo_mask[BISHOP][s] & bit(sq)) && !(path_mask[s][sq] & occ))
            return true;
    }
    return false;
}

bool in_check(const Pos *pos, int color) {
    int ksq = __builtin_ctzll(pos->pieces[color][KING]);
    return sq_attacked(pos, ksq, 1-color);
}

/* ------------------------------------------------------------------ */

static void gen_pawns(const Pos *pos, MoveList *ml, int c) {
    int e           = 1-c;
    BB  occ         = pos->occupied;
    BB  enemy_occ   = pos->by_color[e];
    BB  ep_bb       = (pos->ep >= 0) ? bit(pos->ep) : 0;
    int promo_rank  = (c == WHITE) ? 7 : 0;
    BB  pawns       = pos->pieces[c][PAWN];

    while (pawns) {
        int from = __builtin_ctzll(pawns); pawns &= pawns-1;

        /* single push */
        BB s1 = pawn_push[c][from] & ~occ;
        if (s1) {
            int to = __builtin_ctzll(s1);
            if (rank_of(to) == promo_rank) {
                for (int p = KNIGHT; p <= QUEEN; p++)
                    add(ml, from, to, PAWN, NO_PIECE, p, 0);
            } else {
                add(ml, from, to, PAWN, NO_PIECE, NO_PIECE, 0);
                /* double push: intermediate (s1) already clear */
                BB s2 = pawn_dpush[c][from] & ~occ;
                if (s2) add(ml, from, __builtin_ctzll(s2), PAWN, NO_PIECE, NO_PIECE, 0);
            }
        }

        /* captures (including en passant) */
        BB atk = pawn_atk[c][from] & (enemy_occ | ep_bb);
        while (atk) {
            int to = __builtin_ctzll(atk); atk &= atk-1;
            if (to == pos->ep) {
                add(ml, from, to, PAWN, PAWN, NO_PIECE, MOVE_EP);
            } else if (rank_of(to) == promo_rank) {
                int cap = piece_on(pos, e, to);
                for (int p = KNIGHT; p <= QUEEN; p++)
                    add(ml, from, to, PAWN, cap, p, 0);
            } else {
                add(ml, from, to, PAWN, piece_on(pos, e, to), NO_PIECE, 0);
            }
        }
    }
}

static void gen_pieces(const Pos *pos, MoveList *ml, int c) {
    int  e        = 1-c;
    BB   friendly = pos->by_color[c];
    BB   occ      = pos->occupied;

    /* knights */
    BB pieces = pos->pieces[c][KNIGHT];
    while (pieces) {
        int from = __builtin_ctzll(pieces); pieces &= pieces-1;
        BB dests = geo_mask[KNIGHT][from] & ~friendly;
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests-1;
            add(ml, from, to, KNIGHT, piece_on(pos, e, to), NO_PIECE, 0);
        }
    }

    /* bishops */
    pieces = pos->pieces[c][BISHOP];
    while (pieces) {
        int from = __builtin_ctzll(pieces); pieces &= pieces-1;
        BB dests = slider_dests(from, geo_mask[BISHOP][from], occ, friendly);
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests-1;
            add(ml, from, to, BISHOP, piece_on(pos, e, to), NO_PIECE, 0);
        }
    }

    /* rooks */
    pieces = pos->pieces[c][ROOK];
    while (pieces) {
        int from = __builtin_ctzll(pieces); pieces &= pieces-1;
        BB dests = slider_dests(from, geo_mask[ROOK][from], occ, friendly);
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests-1;
            add(ml, from, to, ROOK, piece_on(pos, e, to), NO_PIECE, 0);
        }
    }

    /* queens */
    pieces = pos->pieces[c][QUEEN];
    while (pieces) {
        int from = __builtin_ctzll(pieces); pieces &= pieces-1;
        BB dests = slider_dests(from, geo_mask[QUEEN][from], occ, friendly);
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests-1;
            add(ml, from, to, QUEEN, piece_on(pos, e, to), NO_PIECE, 0);
        }
    }

    /* king */
    {
        int from = __builtin_ctzll(pos->pieces[c][KING]);
        BB dests = geo_mask[KING][from] & ~friendly;
        while (dests) {
            int to = __builtin_ctzll(dests); dests &= dests-1;
            add(ml, from, to, KING, piece_on(pos, e, to), NO_PIECE, 0);
        }

        /* castling */
        if (c == WHITE) {
            if ((pos->castling & CASTLE_WK) && !(occ & 0x60ULL) &&
                !sq_attacked(pos,4,e) && !sq_attacked(pos,5,e) && !sq_attacked(pos,6,e))
                add(ml, 4, 6, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
            if ((pos->castling & CASTLE_WQ) && !(occ & 0x0EULL) &&
                !sq_attacked(pos,4,e) && !sq_attacked(pos,3,e) && !sq_attacked(pos,2,e))
                add(ml, 4, 2, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
        } else {
            if ((pos->castling & CASTLE_BK) && !(occ & 0x6000000000000000ULL) &&
                !sq_attacked(pos,60,e) && !sq_attacked(pos,61,e) && !sq_attacked(pos,62,e))
                add(ml, 60, 62, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
            if ((pos->castling & CASTLE_BQ) && !(occ & 0x0E00000000000000ULL) &&
                !sq_attacked(pos,60,e) && !sq_attacked(pos,59,e) && !sq_attacked(pos,58,e))
                add(ml, 60, 58, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
        }
    }
}

void gen_moves(const Pos *pos, MoveList *ml) {
    ml->count = 0;
    gen_pawns(pos, ml, pos->turn);
    gen_pieces(pos, ml, pos->turn);
}
