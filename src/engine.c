#include "engine.h"
#include "tables.h"
#include <string.h>
#include <stdlib.h>

static Pos cur;
static BB  lmat[64];   /* lmat[sq] = legal destinations for piece on sq */

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void rebuild_occ(void) {
    cur.by_color[WHITE] = cur.by_color[BLACK] = 0;
    for (int p = 0; p < NPIECES; p++) {
        cur.by_color[WHITE] |= cur.pieces[WHITE][p];
        cur.by_color[BLACK] |= cur.pieces[BLACK][p];
    }
    cur.occupied = cur.by_color[WHITE] | cur.by_color[BLACK];
}

static BB slider_lm(int from, BB geo, BB occ, BB fr) {
    BB r = 0, c = geo & ~fr;
    while (c) {
        int t = __builtin_ctzll(c); c &= c-1;
        if (!(path_mask[from][t] & occ)) r |= bit(t);
    }
    return r;
}

/* Recompute legal move mask for whatever piece sits on sq */
static BB recompute(int sq) {
    int color = -1, piece = -1;
    for (int c = 0; c < 2 && color < 0; c++)
        for (int p = 0; p < NPIECES; p++)
            if (cur.pieces[c][p] & bit(sq)) { color=c; piece=p; break; }
    if (color < 0) return 0;

    BB fr  = cur.by_color[color];
    BB occ = cur.occupied;
    BB en  = cur.by_color[1-color];
    BB ep  = (cur.ep >= 0) ? bit(cur.ep) : 0;

    switch (piece) {
        case PAWN: {
            BB r = pawn_push[color][sq] & ~occ;
            if (r && pawn_dpush[color][sq] && !(pawn_dpush[color][sq] & occ))
                r |= pawn_dpush[color][sq];
            r |= pawn_atk[color][sq] & (en | ep);
            return r;
        }
        case KNIGHT: return geo_mask[KNIGHT][sq] & ~fr;
        case KING:   return geo_mask[KING][sq]   & ~fr;
        case BISHOP: return slider_lm(sq, geo_mask[BISHOP][sq], occ, fr);
        case ROOK:   return slider_lm(sq, geo_mask[ROOK][sq],   occ, fr);
        case QUEEN:  return slider_lm(sq, geo_mask[QUEEN][sq],  occ, fr);
        default:     return 0;
    }
}

/*
 * Influence zone of a square: all squares on the same rank, file,
 * or diagonal. A piece anywhere in this zone may have its legal moves
 * altered when the square changes occupation.
 */
static BB influence(int sq) {
    return geo_mask[ROOK][sq] | geo_mask[BISHOP][sq] | bit(sq);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void engine_init(const Pos *pos) {
    cur = *pos;
    for (int sq = 0; sq < 64; sq++)
        lmat[sq] = recompute(sq);
}

const Pos *engine_cur(void) { return &cur; }

void engine_make(Move m, Undo *u) {
    int c = cur.turn, e = 1-c;

    /* --- save position --- */
    memcpy(u->pieces_snap, cur.pieces, sizeof(cur.pieces));
    u->old_ep       = cur.ep;
    u->old_castling = cur.castling;
    u->old_halfmove = cur.halfmove;
    u->old_fullmove = cur.fullmove;
    u->old_turn     = cur.turn;
    u->ep_cap_sq    = -1;

    /* --- build influence zone --- */
    BB zone = influence(m.from) | influence(m.to);

    if (m.flags & MOVE_EP) {
        u->ep_cap_sq = (c == WHITE) ? m.to - 8 : m.to + 8;
        zone |= influence(u->ep_cap_sq);
    }
    if (m.flags & MOVE_CASTLE) {
        int rf, rt;
        if      (m.to == 6)  { rf=7;  rt=5;  }
        else if (m.to == 2)  { rf=0;  rt=3;  }
        else if (m.to == 62) { rf=63; rt=61; }
        else                 { rf=56; rt=59; }
        zone |= influence(rf) | influence(rt);
    }

    /* --- save legal rows for every square in zone --- */
    u->n = 0;
    BB tmp = zone;
    while (tmp) {
        int sq = __builtin_ctzll(tmp); tmp &= tmp-1;
        u->sq[u->n]     = sq;
        u->saved[u->n]  = lmat[sq];
        u->n++;
    }

    /* --- apply move to position --- */
    cur.ep = -1; cur.halfmove++;
    if (c == BLACK) cur.fullmove++;

    if (m.capture != NO_PIECE && !(m.flags & MOVE_EP))
        cur.pieces[e][m.capture] &= ~bit(m.to);
    if (m.flags & MOVE_EP)
        cur.pieces[e][PAWN] &= ~bit(u->ep_cap_sq);

    cur.pieces[c][m.piece] &= ~bit(m.from);
    if (m.promo != NO_PIECE) cur.pieces[c][m.promo] |= bit(m.to);
    else                     cur.pieces[c][m.piece] |= bit(m.to);

    if (m.flags & MOVE_CASTLE) {
        if      (m.to == 6)  { cur.pieces[WHITE][ROOK] &= ~bit(7);  cur.pieces[WHITE][ROOK] |= bit(5);  }
        else if (m.to == 2)  { cur.pieces[WHITE][ROOK] &= ~bit(0);  cur.pieces[WHITE][ROOK] |= bit(3);  }
        else if (m.to == 62) { cur.pieces[BLACK][ROOK] &= ~bit(63); cur.pieces[BLACK][ROOK] |= bit(61); }
        else if (m.to == 58) { cur.pieces[BLACK][ROOK] &= ~bit(56); cur.pieces[BLACK][ROOK] |= bit(59); }
    }

    if (m.piece == KING) {
        if (c == WHITE) cur.castling &= ~(CASTLE_WK|CASTLE_WQ);
        else            cur.castling &= ~(CASTLE_BK|CASTLE_BQ);
    }
    if (m.piece == ROOK) {
        if (c == WHITE && m.from==0)  cur.castling &= ~CASTLE_WQ;
        if (c == WHITE && m.from==7)  cur.castling &= ~CASTLE_WK;
        if (c == BLACK && m.from==56) cur.castling &= ~CASTLE_BQ;
        if (c == BLACK && m.from==63) cur.castling &= ~CASTLE_BK;
    }
    if (m.capture == ROOK) {
        if (e == WHITE && m.to==0)    cur.castling &= ~CASTLE_WQ;
        if (e == WHITE && m.to==7)    cur.castling &= ~CASTLE_WK;
        if (e == BLACK && m.to==56)   cur.castling &= ~CASTLE_BQ;
        if (e == BLACK && m.to==63)   cur.castling &= ~CASTLE_BK;
    }

    if (m.piece == PAWN && abs((int)m.to-(int)m.from) == 16) {
        cur.ep = ((int)m.from + (int)m.to) / 2;
        cur.halfmove = 0;
    }
    if (m.piece == PAWN || m.capture != NO_PIECE) cur.halfmove = 0;

    cur.turn = e;
    rebuild_occ();

    /* --- recompute legal rows in zone --- */
    for (int i = 0; i < u->n; i++)
        lmat[u->sq[i]] = recompute(u->sq[i]);
}

void engine_unmake(const Undo *u) {
    /* restore position */
    memcpy(cur.pieces, u->pieces_snap, sizeof(cur.pieces));
    cur.ep       = u->old_ep;
    cur.castling = u->old_castling;
    cur.halfmove = u->old_halfmove;
    cur.fullmove = u->old_fullmove;
    cur.turn     = u->old_turn;
    rebuild_occ();
    /* restore legal rows */
    for (int i = 0; i < u->n; i++)
        lmat[u->sq[i]] = u->saved[i];
}

bool engine_in_check(void) {
    /* check the side that just moved (before turn swap) */
    int just_moved = 1 - cur.turn;
    int ksq = __builtin_ctzll(cur.pieces[just_moved][KING]);
    /* use lmat of enemy pieces to ask: does any enemy piece reach ksq? */
    BB attackers = cur.by_color[cur.turn];
    while (attackers) {
        int sq = __builtin_ctzll(attackers); attackers &= attackers-1;
        if (lmat[sq] & bit(ksq)) return true;
    }
    return false;
}

void engine_gen_moves(MoveList *ml) {
    /* Delegate to the existing gen_moves which reads from cur */
    extern void gen_moves(const Pos *, MoveList *);
    gen_moves(&cur, ml);
}

long long engine_perft(int depth) {
    MoveList ml;
    engine_gen_moves(&ml);

    if (depth == 1) {
        long long count = 0;
        for (int i = 0; i < ml.count; i++) {
            Undo u;
            engine_make(ml.moves[i], &u);
            if (!engine_in_check()) count++;
            engine_unmake(&u);
        }
        return count;
    }

    long long nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        Undo u;
        engine_make(ml.moves[i], &u);
        if (!engine_in_check()) nodes += engine_perft(depth - 1);
        engine_unmake(&u);
    }
    return nodes;
}
