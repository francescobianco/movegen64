#include "position.h"
#include <string.h>
#include <stdio.h>

static void rebuild(Pos *pos) {
    pos->by_color[WHITE] = 0;
    pos->by_color[BLACK] = 0;
    for (int p = 0; p < NPIECES; p++) {
        pos->by_color[WHITE] |= pos->pieces[WHITE][p];
        pos->by_color[BLACK] |= pos->pieces[BLACK][p];
    }
    pos->occupied = pos->by_color[WHITE] | pos->by_color[BLACK];
}

void pos_set_start(Pos *pos) {
    memset(pos, 0, sizeof(*pos));
    pos->pieces[WHITE][ROOK]   = bit(0)  | bit(7);
    pos->pieces[WHITE][KNIGHT] = bit(1)  | bit(6);
    pos->pieces[WHITE][BISHOP] = bit(2)  | bit(5);
    pos->pieces[WHITE][QUEEN]  = bit(3);
    pos->pieces[WHITE][KING]   = bit(4);
    pos->pieces[WHITE][PAWN]   = 0x000000000000FF00ULL;
    pos->pieces[BLACK][ROOK]   = bit(56) | bit(63);
    pos->pieces[BLACK][KNIGHT] = bit(57) | bit(62);
    pos->pieces[BLACK][BISHOP] = bit(58) | bit(61);
    pos->pieces[BLACK][QUEEN]  = bit(59);
    pos->pieces[BLACK][KING]   = bit(60);
    pos->pieces[BLACK][PAWN]   = 0x00FF000000000000ULL;
    pos->turn     = WHITE;
    pos->ep       = -1;
    pos->castling = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ;
    pos->halfmove = 0;
    pos->fullmove = 1;
    rebuild(pos);
}

Pos pos_after(const Pos *pos, Move m) {
    Pos next  = *pos;
    int c     = pos->turn;
    int e     = 1 - c;

    next.ep       = -1;
    next.halfmove++;
    if (c == BLACK) next.fullmove++;

    /* remove captured piece */
    if (m.capture != NO_PIECE && !(m.flags & MOVE_EP))
        next.pieces[e][m.capture] &= ~bit(m.to);

    /* en passant: remove the captured pawn on the rank behind */
    if (m.flags & MOVE_EP) {
        int cap_sq = (c == WHITE) ? m.to - 8 : m.to + 8;
        next.pieces[e][PAWN] &= ~bit(cap_sq);
        next.halfmove = 0;
    }

    /* move piece */
    next.pieces[c][m.piece] &= ~bit(m.from);
    if (m.promo != NO_PIECE)
        next.pieces[c][m.promo] |= bit(m.to);
    else
        next.pieces[c][m.piece] |= bit(m.to);

    /* castling: move the rook */
    if (m.flags & MOVE_CASTLE) {
        if      (m.to == 6)  { next.pieces[WHITE][ROOK] &= ~bit(7);  next.pieces[WHITE][ROOK] |= bit(5);  }
        else if (m.to == 2)  { next.pieces[WHITE][ROOK] &= ~bit(0);  next.pieces[WHITE][ROOK] |= bit(3);  }
        else if (m.to == 62) { next.pieces[BLACK][ROOK] &= ~bit(63); next.pieces[BLACK][ROOK] |= bit(61); }
        else if (m.to == 58) { next.pieces[BLACK][ROOK] &= ~bit(56); next.pieces[BLACK][ROOK] |= bit(59); }
    }

    /* update castling rights */
    if (m.piece == KING) {
        if (c == WHITE) next.castling &= ~(CASTLE_WK | CASTLE_WQ);
        else            next.castling &= ~(CASTLE_BK | CASTLE_BQ);
    }
    if (m.piece == ROOK) {
        if (c == WHITE && m.from == 0)  next.castling &= ~CASTLE_WQ;
        if (c == WHITE && m.from == 7)  next.castling &= ~CASTLE_WK;
        if (c == BLACK && m.from == 56) next.castling &= ~CASTLE_BQ;
        if (c == BLACK && m.from == 63) next.castling &= ~CASTLE_BK;
    }
    /* rook captured */
    if (m.capture == ROOK) {
        if (e == WHITE && m.to == 0)  next.castling &= ~CASTLE_WQ;
        if (e == WHITE && m.to == 7)  next.castling &= ~CASTLE_WK;
        if (e == BLACK && m.to == 56) next.castling &= ~CASTLE_BQ;
        if (e == BLACK && m.to == 63) next.castling &= ~CASTLE_BK;
    }

    /* en passant square for double pawn push */
    if (m.piece == PAWN && abs((int)m.to - (int)m.from) == 16) {
        next.ep = ((int)m.from + (int)m.to) / 2;
        next.halfmove = 0;
    }

    if (m.piece == PAWN || m.capture != NO_PIECE) next.halfmove = 0;

    next.turn = e;
    rebuild(&next);
    return next;
}

void pos_from_fen(Pos *pos, const char *fen) {
    static const char *piece_str = "PNBRQKpnbrqk";
    memset(pos, 0, sizeof(*pos));
    pos->ep = -1;

    int r = 7, f = 0;
    const char *p = fen;

    /* board */
    while (*p && *p != ' ') {
        if (*p == '/') { r--; f = 0; }
        else if (*p >= '1' && *p <= '8') { f += *p - '0'; }
        else {
            const char *idx = strchr(piece_str, *p);
            if (idx) {
                int piece_idx = (int)(idx - piece_str);
                int color = piece_idx >= 6 ? BLACK : WHITE;
                int type  = piece_idx % 6;
                pos->pieces[color][type] |= bit(sq(r, f));
                f++;
            }
        }
        p++;
    }

    /* turn */
    if (*p) p++;
    pos->turn = (*p == 'b') ? BLACK : WHITE;
    if (*p) p++;

    /* castling */
    if (*p) p++;
    while (*p && *p != ' ') {
        if (*p == 'K') pos->castling |= CASTLE_WK;
        if (*p == 'Q') pos->castling |= CASTLE_WQ;
        if (*p == 'k') pos->castling |= CASTLE_BK;
        if (*p == 'q') pos->castling |= CASTLE_BQ;
        p++;
    }

    /* en passant */
    if (*p) p++;
    if (*p && *p != '-') {
        int ep_f = *p - 'a'; p++;
        int ep_r = *p - '1';
        pos->ep = sq(ep_r, ep_f);
    }

    rebuild(pos);
}

void pos_print(const Pos *pos) {
    static const char pieces_w[] = "PNBRQK";
    static const char pieces_b[] = "pnbrqk";
    printf("  +--------+\n");
    for (int r = 7; r >= 0; r--) {
        printf("%d |", r+1);
        for (int f = 0; f < 8; f++) {
            int s = sq(r, f);
            char c = '.';
            for (int pt = 0; pt < NPIECES; pt++) {
                if (pos->pieces[WHITE][pt] & bit(s)) { c = pieces_w[pt]; break; }
                if (pos->pieces[BLACK][pt] & bit(s)) { c = pieces_b[pt]; break; }
            }
            printf("%c", c);
        }
        printf("|\n");
    }
    printf("  +--------+\n");
    printf("  abcdefgh\n");
    printf("  turn=%s  ep=%d  castling=%d\n",
           pos->turn==WHITE?"white":"black", pos->ep, pos->castling);
}
