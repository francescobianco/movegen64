#include "machine64.h"
#include "relax64.h"
#include <string.h>

#define M_PIECE  0x07   /* piece+1, 0 = empty */
#define M_COLOR  0x08
#define M_VIRGIN 0x10
#define M_GHOST  0x20
#define M_TURN   0x80   /* on cell 0 only */

void machine64_from_pos(const Pos *pos, Diag64 *m) {
    memset(m->d, 0, sizeof m->d);
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < NPIECES; p++)
            for (BB b = pos->pieces[c][p]; b; b &= b - 1) {
                int s = __builtin_ctzll(b);
                m->d[s] = (uint8_t)((p + 1) | (c << 3));
            }
    if (pos->ep >= 0) m->d[pos->ep] |= M_GHOST;
    if (pos->castling & (CASTLE_WK | CASTLE_WQ)) m->d[4]  |= M_VIRGIN;
    if (pos->castling & CASTLE_WK)               m->d[7]  |= M_VIRGIN;
    if (pos->castling & CASTLE_WQ)               m->d[0]  |= M_VIRGIN;
    if (pos->castling & (CASTLE_BK | CASTLE_BQ)) m->d[60] |= M_VIRGIN;
    if (pos->castling & CASTLE_BK)               m->d[63] |= M_VIRGIN;
    if (pos->castling & CASTLE_BQ)               m->d[56] |= M_VIRGIN;
    if (pos->turn == BLACK) m->d[0] |= M_TURN;
}

void machine64_to_pos(const Diag64 *m, Pos *pos) {
    memset(pos, 0, sizeof *pos);
    pos->ep = -1;
    for (int s = 0; s < 64; s++) {
        uint8_t b = m->d[s];
        if (b & M_PIECE) {
            int c = (b & M_COLOR) ? BLACK : WHITE;
            int p = (b & M_PIECE) - 1;
            pos->pieces[c][p] |= bit(s);
            pos->by_color[c]  |= bit(s);
        }
        if (b & M_GHOST) pos->ep = s;
    }
    pos->occupied = pos->by_color[WHITE] | pos->by_color[BLACK];
    if ((m->d[4]  & M_VIRGIN) && (m->d[7]  & M_VIRGIN)) pos->castling |= CASTLE_WK;
    if ((m->d[4]  & M_VIRGIN) && (m->d[0]  & M_VIRGIN)) pos->castling |= CASTLE_WQ;
    if ((m->d[60] & M_VIRGIN) && (m->d[63] & M_VIRGIN)) pos->castling |= CASTLE_BK;
    if ((m->d[60] & M_VIRGIN) && (m->d[56] & M_VIRGIN)) pos->castling |= CASTLE_BQ;
    pos->turn = (m->d[0] & M_TURN) ? BLACK : WHITE;
    pos->fullmove = 1;
}

/* C + A: rows emitted from the diagonal, columns collected at the king,
   illegal cells annihilated — all inside the plane generator */
void machine64_gen(const Diag64 *m, MoveList *ml) {
    Pos p;                      /* transient register, not persistent state */
    machine64_to_pos(m, &p);
    Relax64 r;
    relax64_step(p.occupied, &r);
    relax64_gen(&p, &r, ml);
}

/* T: pure transport — bytes move on the diagonal, flag bits travel/expire */
void machine64_apply(Diag64 *m, Move mv) {
    int them = (m->d[0] & M_TURN) ? WHITE : BLACK;

    /* en-passant ghosts expire after one step */
    for (int s = 0; s < 64; s++) m->d[s] &= (uint8_t)~M_GHOST;

    uint8_t b = m->d[mv.from] & (M_PIECE | M_COLOR);   /* virgin stays behind */
    m->d[mv.from] &= (uint8_t)~(M_PIECE | M_COLOR | M_VIRGIN);

    if (mv.flags & MOVE_EP) {
        int csq = (them == BLACK) ? mv.to - 8 : mv.to + 8;
        m->d[csq] &= (uint8_t)~(M_PIECE | M_COLOR);
    }
    if (mv.flags & MOVE_CASTLE) {
        int rf = (mv.to > mv.from) ? mv.to + 1 : mv.to - 2;
        int rt = (mv.to > mv.from) ? mv.to - 1 : mv.to + 1;
        uint8_t rb = m->d[rf] & (M_PIECE | M_COLOR);
        m->d[rf] &= (uint8_t)~(M_PIECE | M_COLOR | M_VIRGIN);
        m->d[rt]  = (uint8_t)((m->d[rt] & ~(M_PIECE | M_COLOR | M_VIRGIN)) | rb);
    }
    if (mv.promo != NO_PIECE)
        b = (uint8_t)((b & M_COLOR) | (mv.promo + 1));

    m->d[mv.to] = (uint8_t)((m->d[mv.to] & ~(M_PIECE | M_COLOR | M_VIRGIN)) | b);

    if (mv.piece == PAWN && (mv.to - mv.from == 16 || mv.from - mv.to == 16))
        m->d[(mv.from + mv.to) / 2] |= M_GHOST;

    m->d[0] = (uint8_t)((m->d[0] & ~M_TURN) | (them == BLACK ? M_TURN : 0));
}

long long machine64_perft(const Diag64 *m, int depth) {
    MoveList ml;
    machine64_gen(m, &ml);
    if (depth == 1) return ml.count;
    long long nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        Diag64 next = *m;
        machine64_apply(&next, ml.moves[i]);
        nodes += machine64_perft(&next, depth - 1);
    }
    return nodes;
}
