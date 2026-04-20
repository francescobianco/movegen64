#include "state64.h"
#include "position.h"
#include "tables.h"
#include <string.h>

#define S64_PIECE_MASK 0x0F
#define S64_META_BIT   0x80

static State64Static s64_static;
static bool s64_static_ready;
static State64Stats s64_stats;

enum {
    META_TURN = 0,
    META_CASTLE_WK,
    META_CASTLE_WQ,
    META_CASTLE_BK,
    META_CASTLE_BQ,
    META_EP_VALID,
    META_EP_0,
    META_HALFMOVE_0 = META_EP_0 + 6,
    META_FULLMOVE_0 = META_HALFMOVE_0 + 8,
};

static uint8_t piece_code(int color, int piece) {
    return (uint8_t)((color ? 8 : 0) | (piece + 1));
}

static bool code_piece(uint8_t code, int *color, int *piece) {
    code &= S64_PIECE_MASK;
    if (!code) return false;

    *color = (code & 8) ? BLACK : WHITE;
    *piece = (code & 7) - 1;
    return *piece >= PAWN && *piece <= KING;
}

static void set_meta_bit(State64 *s, int idx, bool value) {
    if (value) s->m[idx][idx] |= S64_META_BIT;
    else       s->m[idx][idx] &= (uint8_t)~S64_META_BIT;
}

static bool meta_bit(const State64 *s, int idx) {
    return (s->m[idx][idx] & S64_META_BIT) != 0;
}

static void set_square_code(State64 *s, int sq, uint8_t code) {
    s->m[sq][sq] = (s->m[sq][sq] & (uint8_t)~S64_PIECE_MASK) |
                   (code & S64_PIECE_MASK);
}

static void set_meta_u(State64 *s, int first, int nbits, unsigned value) {
    for (int i = 0; i < nbits; i++)
        set_meta_bit(s, first + i, (value >> i) & 1U);
}

static unsigned meta_u(const State64 *s, int first, int nbits) {
    unsigned value = 0;
    for (int i = 0; i < nbits; i++)
        if (meta_bit(s, first + i)) value |= 1U << i;
    return value;
}

static bool move_same(Move a, Move b) {
    return a.from == b.from &&
           a.to == b.to &&
           a.piece == b.piece &&
           a.capture == b.capture &&
           a.promo == b.promo &&
           a.flags == b.flags;
}

void state64_stats_reset(void) {
    memset(&s64_stats, 0, sizeof(s64_stats));
}

State64Stats state64_stats_get(void) {
    return s64_stats;
}

void state64_static_init(void) {
    memset(&s64_static, 0, sizeof(s64_static));

    for (int piece = 0; piece < NPIECES; piece++) {
        for (int from = 0; from < 64; from++) {
            BB bb = geo_mask[piece][from];
            s64_static.geom_bb[piece][from] = bb;
            while (bb) {
                int to = __builtin_ctzll(bb);
                bb &= bb - 1;
                s64_static.geom[piece][from][to] = 1;
            }
        }
    }

    for (int from = 0; from < 64; from++) {
        for (int to = 0; to < 64; to++) {
            if (from == to) {
                s64_static.path[from][to] = 0;
            } else if (path_mask[from][to]) {
                s64_static.path[from][to] = (uint8_t)__builtin_popcountll(path_mask[from][to]);
            } else {
                int rf = rank_of(from), ff = file_of(from);
                int rt = rank_of(to), ft = file_of(to);
                bool collinear = (rf == rt) || (ff == ft) ||
                                 (abs(rf - rt) == abs(ff - ft));
                s64_static.path[from][to] = collinear ? 0 : 0xFF;
            }
        }
    }

    s64_static_ready = true;
}

static void state64_ensure_static(void) {
    if (!s64_static_ready)
        state64_static_init();
}

static int state64_turn(const State64 *s) {
    return meta_bit(s, META_TURN) ? BLACK : WHITE;
}

static int state64_castling(const State64 *s) {
    int castling = 0;
    if (meta_bit(s, META_CASTLE_WK)) castling |= CASTLE_WK;
    if (meta_bit(s, META_CASTLE_WQ)) castling |= CASTLE_WQ;
    if (meta_bit(s, META_CASTLE_BK)) castling |= CASTLE_BK;
    if (meta_bit(s, META_CASTLE_BQ)) castling |= CASTLE_BQ;
    return castling;
}

static int state64_ep(const State64 *s) {
    return meta_bit(s, META_EP_VALID) ? (int)meta_u(s, META_EP_0, 6) : -1;
}

static bool state64_piece_at(const State64 *s, int sq, int *color, int *piece) {
    return code_piece(s->m[sq][sq], color, piece);
}

typedef struct {
    int color[64];
    int piece[64];
    BB by_color[2];
    BB occupied;
    int king_sq[2];
} State64View;

static void state64_view_build(const State64 *s, State64View *v) {
    v->by_color[WHITE] = 0;
    v->by_color[BLACK] = 0;
    v->occupied = 0;
    v->king_sq[WHITE] = -1;
    v->king_sq[BLACK] = -1;

    for (int sq = 0; sq < 64; sq++) {
        int color, piece;
        if (state64_piece_at(s, sq, &color, &piece)) {
            v->color[sq] = color;
            v->piece[sq] = piece;
            v->by_color[color] |= bit(sq);
            v->occupied |= bit(sq);
            if (piece == KING)
                v->king_sq[color] = sq;
        } else {
            v->color[sq] = -1;
            v->piece[sq] = -1;
        }
    }
}

static bool state64_view_square_attacked(const State64View *v, int sq, int attacker) {
    state64_ensure_static();

    for (int from = 0; from < 64; from++) {
        if (v->color[from] != attacker)
            continue;

        switch (v->piece[from]) {
            case PAWN:
                if (pawn_atk[attacker][from] & bit(sq)) return true;
                break;
            case KNIGHT:
                if (s64_static.geom[KNIGHT][from][sq]) return true;
                break;
            case KING:
                if (s64_static.geom[KING][from][sq]) return true;
                break;
            case BISHOP:
                if (s64_static.geom[BISHOP][from][sq] && !(path_mask[from][sq] & v->occupied))
                    return true;
                break;
            case ROOK:
                if (s64_static.geom[ROOK][from][sq] && !(path_mask[from][sq] & v->occupied))
                    return true;
                break;
            case QUEEN:
                if (s64_static.geom[QUEEN][from][sq] && !(path_mask[from][sq] & v->occupied))
                    return true;
                break;
        }
    }
    return false;
}

static bool state64_in_check(const State64 *s, int color) {
    State64View v;
    state64_view_build(s, &v);
    return v.king_sq[color] >= 0 && state64_view_square_attacked(&v, v.king_sq[color], 1 - color);
}

typedef struct {
    int turn;
    int king_sq;
    int check_count;
    BB evasion;
    BB enemy_attack;
    BB occupied;
    BB enemy_rq;
    BB enemy_bq;
    BB pinned[64];
} State64CloseCtx;

static bool state64_slider_attacks_line(int piece, int from, int to) {
    if (piece == QUEEN) return s64_static.geom[QUEEN][from][to] != 0;
    if (piece == ROOK) return s64_static.geom[ROOK][from][to] != 0;
    if (piece == BISHOP) return s64_static.geom[BISHOP][from][to] != 0;
    return false;
}

static BB state64_view_attacks_from(const State64View *v, int from, BB occ) {
    int color = v->color[from];
    int piece = v->piece[from];
    if (color < 0) return 0;

    if (piece == PAWN) return pawn_atk[color][from];
    if (piece == KNIGHT || piece == KING) return s64_static.geom_bb[piece][from];

    BB attacks = 0;
    BB geo = s64_static.geom_bb[piece][from];
    while (geo) {
        int to = __builtin_ctzll(geo);
        geo &= geo - 1;
        if (!(path_mask[from][to] & occ))
            attacks |= bit(to);
    }
    return attacks;
}

static void state64_build_close_ctx(const State64View *v, int turn, State64CloseCtx *ctx) {
    state64_ensure_static();

    ctx->turn = turn;
    ctx->king_sq = v->king_sq[turn];
    ctx->check_count = 0;
    ctx->evasion = 0;
    ctx->enemy_attack = 0;
    ctx->occupied = v->occupied;
    ctx->enemy_rq = 0;
    ctx->enemy_bq = 0;
    for (int i = 0; i < 64; i++)
        ctx->pinned[i] = ~0ULL;

    int enemy = 1 - ctx->turn;
    BB friendly = v->by_color[ctx->turn];
    BB occ_no_king = v->occupied & ~bit(ctx->king_sq);

    for (int from = 0; from < 64; from++) {
        if (v->color[from] != enemy)
            continue;

        int piece = v->piece[from];
        if (piece == ROOK || piece == QUEEN)
            ctx->enemy_rq |= bit(from);
        if (piece == BISHOP || piece == QUEEN)
            ctx->enemy_bq |= bit(from);

        ctx->enemy_attack |= state64_view_attacks_from(v, from, occ_no_king);

        if (piece == PAWN) {
            if (pawn_atk[enemy][from] & bit(ctx->king_sq)) {
                ctx->check_count++;
                ctx->evasion |= bit(from);
            }
            continue;
        }

        if (piece == KNIGHT || piece == KING) {
            if (s64_static.geom[piece][from][ctx->king_sq]) {
                ctx->check_count++;
                ctx->evasion |= bit(from);
            }
            continue;
        }

        if (!state64_slider_attacks_line(piece, from, ctx->king_sq))
            continue;

        BB blockers = path_mask[from][ctx->king_sq] & v->occupied;
        if (!blockers) {
            ctx->check_count++;
            ctx->evasion |= bit(from) | path_mask[from][ctx->king_sq];
        } else if ((blockers & (blockers - 1)) == 0 && (blockers & friendly)) {
            int pinned_sq = __builtin_ctzll(blockers);
            ctx->pinned[pinned_sq] = bit(from) | path_mask[from][ctx->king_sq];
        }
    }

    if (ctx->check_count > 1)
        ctx->evasion = 0;
}

static void state64_raw_apply_known_legal(const State64 *s, Move m, State64 *next);

static void state64_encode_pos(State64 *s, const Pos *pos) {
    memset(s->m, 0, sizeof(s->m));

    for (int color = 0; color < 2; color++) {
        for (int piece = 0; piece < NPIECES; piece++) {
            BB bb = pos->pieces[color][piece];
            while (bb) {
                int sq = __builtin_ctzll(bb);
                bb &= bb - 1;
                s->m[sq][sq] = (s->m[sq][sq] & (uint8_t)~S64_PIECE_MASK) |
                               piece_code(color, piece);
            }
        }
    }

    set_meta_bit(s, META_TURN, pos->turn == BLACK);
    set_meta_bit(s, META_CASTLE_WK, pos->castling & CASTLE_WK);
    set_meta_bit(s, META_CASTLE_WQ, pos->castling & CASTLE_WQ);
    set_meta_bit(s, META_CASTLE_BK, pos->castling & CASTLE_BK);
    set_meta_bit(s, META_CASTLE_BQ, pos->castling & CASTLE_BQ);
    set_meta_bit(s, META_EP_VALID, pos->ep >= 0);
    set_meta_u(s, META_EP_0, 6, (pos->ep >= 0) ? (unsigned)pos->ep : 0);
    set_meta_u(s, META_HALFMOVE_0, 8, (unsigned)(pos->halfmove & 0xFF));
    set_meta_u(s, META_FULLMOVE_0, 16, (unsigned)(pos->fullmove & 0xFFFF));
}

static void state64_rebuild_from_pos(State64 *s, const Pos *pos) {
    (void)pos;
    state64_close(s);
}

void state64_from_pos(State64 *s, const Pos *pos) {
    state64_encode_pos(s, pos);
    state64_rebuild_from_pos(s, pos);
}

void state64_to_pos(const State64 *s, Pos *pos) {
    memset(pos, 0, sizeof(*pos));

    for (int sq = 0; sq < 64; sq++) {
        int color, piece;
        if (code_piece(s->m[sq][sq], &color, &piece))
            pos->pieces[color][piece] |= bit(sq);
    }

    for (int piece = 0; piece < NPIECES; piece++) {
        pos->by_color[WHITE] |= pos->pieces[WHITE][piece];
        pos->by_color[BLACK] |= pos->pieces[BLACK][piece];
    }
    pos->occupied = pos->by_color[WHITE] | pos->by_color[BLACK];

    pos->turn = meta_bit(s, META_TURN) ? BLACK : WHITE;
    pos->castling = 0;
    if (meta_bit(s, META_CASTLE_WK)) pos->castling |= CASTLE_WK;
    if (meta_bit(s, META_CASTLE_WQ)) pos->castling |= CASTLE_WQ;
    if (meta_bit(s, META_CASTLE_BK)) pos->castling |= CASTLE_BK;
    if (meta_bit(s, META_CASTLE_BQ)) pos->castling |= CASTLE_BQ;
    pos->ep = meta_bit(s, META_EP_VALID) ? (int)meta_u(s, META_EP_0, 6) : -1;
    pos->halfmove = (int)meta_u(s, META_HALFMOVE_0, 8);
    pos->fullmove = (int)meta_u(s, META_FULLMOVE_0, 16);
}

static bool state64_ctx_allows(const State64CloseCtx *ctx, Move m) {
    if (m.flags & MOVE_EP) {
        int cap_sq = (ctx->turn == WHITE) ? m.to - 8 : m.to + 8;

        if (ctx->check_count > 1)
            return false;
        if (ctx->check_count == 1 &&
            !(ctx->evasion & (bit(m.to) | bit(cap_sq))))
            return false;

        BB occ_after = ctx->occupied;
        occ_after &= ~bit(m.from);
        occ_after &= ~bit(cap_sq);
        occ_after |= bit(m.to);

        BB sliders = ctx->enemy_rq;
        while (sliders) {
            int from = __builtin_ctzll(sliders);
            sliders &= sliders - 1;
            if (s64_static.geom[ROOK][from][ctx->king_sq] &&
                !(path_mask[from][ctx->king_sq] & occ_after))
                return false;
        }

        sliders = ctx->enemy_bq;
        while (sliders) {
            int from = __builtin_ctzll(sliders);
            sliders &= sliders - 1;
            if (s64_static.geom[BISHOP][from][ctx->king_sq] &&
                !(path_mask[from][ctx->king_sq] & occ_after))
                return false;
        }

        return true;
    }

    if (m.piece == KING)
        return !(ctx->enemy_attack & bit(m.to));

    if (ctx->check_count > 0) {
        if (ctx->check_count > 1)
            return false;
        if (!(ctx->evasion & bit(m.to)))
            return false;
    }

    if (!(ctx->pinned[m.from] & bit(m.to)))
        return false;

    return true;
}

static void state64_mark_if_legal(State64 *s, const State64CloseCtx *ctx, Move m) {
    s64_stats.candidates++;

    if (state64_ctx_allows(ctx, m)) {
        s64_stats.certified++;
    } else {
        s64_stats.fallback++;
        State64 next;
        state64_raw_apply_known_legal(s, m, &next);
        if (state64_in_check(&next, state64_turn(s))) {
            s64_stats.fallback_rejected++;
            return;
        }
    }

    uint8_t cell = s->m[m.from][m.to] | S64_LEGAL;
    if (m.capture != NO_PIECE)        cell |= S64_CAPTURE;
    if (m.promo != NO_PIECE)          cell |= S64_PROMO;
    if (m.flags & MOVE_EP)            cell |= S64_EP;
    if (m.flags & MOVE_CASTLE)        cell |= S64_CASTLE;
    s->m[m.from][m.to] = cell;
}

static void state64_add_move(State64 *s, const State64CloseCtx *ctx,
                             int from, int to, int piece,
                             int capture, int promo, int flags) {
    s->m[from][to] |= S64_GEOM | S64_CLEAR;
    if (capture != NO_PIECE || (flags & MOVE_EP))
        s->m[from][to] |= S64_ATTACK;

    Move m;
    m.from = (uint8_t)from;
    m.to = (uint8_t)to;
    m.piece = (uint8_t)piece;
    m.capture = (uint8_t)capture;
    m.promo = (uint8_t)promo;
    m.flags = (uint8_t)flags;
    state64_mark_if_legal(s, ctx, m);
}

static void state64_mark_causal_layers(State64 *s, const State64View *v) {
    state64_ensure_static();

    for (int from = 0; from < 64; from++) {
        int color = v->color[from];
        int piece = v->piece[from];
        if (color < 0) continue;

        if (piece == PAWN) {
            BB pushes = pawn_push[color][from] | pawn_dpush[color][from];
            while (pushes) {
                int to = __builtin_ctzll(pushes);
                pushes &= pushes - 1;
                s->m[from][to] |= S64_GEOM;
                if (!(bit(to) & v->occupied))
                    s->m[from][to] |= S64_CLEAR;
            }

            BB attacks = pawn_atk[color][from];
            while (attacks) {
                int to = __builtin_ctzll(attacks);
                attacks &= attacks - 1;
                s->m[from][to] |= S64_GEOM | S64_ATTACK | S64_CLEAR;
            }
            continue;
        }

        BB geo = s64_static.geom_bb[piece][from];
        while (geo) {
            int to = __builtin_ctzll(geo);
            geo &= geo - 1;
            s->m[from][to] |= S64_GEOM;

            if (piece == KNIGHT || piece == KING || !(path_mask[from][to] & v->occupied))
                s->m[from][to] |= S64_CLEAR | S64_ATTACK;
        }
    }
}

void state64_close(State64 *s) {
    state64_ensure_static();
    State64View v;
    state64_view_build(s, &v);

    for (int from = 0; from < 64; from++)
        for (int to = 0; to < 64; to++)
            if (from != to) s->m[from][to] = 0;

    int turn = state64_turn(s);
    int enemy = 1 - turn;
    int ep = state64_ep(s);
    int castling = state64_castling(s);
    BB occ = v.occupied;
    BB friendly = v.by_color[turn];
    BB enemy_bb = v.by_color[enemy];
    int promo_rank = (turn == WHITE) ? 7 : 0;
    State64CloseCtx ctx;

    state64_mark_causal_layers(s, &v);
    state64_build_close_ctx(&v, turn, &ctx);

    for (int from = 0; from < 64; from++) {
        if (v.color[from] != turn)
            continue;
        int piece = v.piece[from];

        if (piece == PAWN) {
            BB s1 = pawn_push[turn][from] & ~occ;
            if (s1) {
                int to = __builtin_ctzll(s1);
                if (rank_of(to) == promo_rank) {
                    for (int promo = KNIGHT; promo <= QUEEN; promo++)
                        state64_add_move(s, &ctx, from, to, PAWN, NO_PIECE, promo, 0);
                } else {
                    state64_add_move(s, &ctx, from, to, PAWN, NO_PIECE, NO_PIECE, 0);
                    BB s2 = pawn_dpush[turn][from] & ~occ;
                    if (s2)
                        state64_add_move(s, &ctx, from, __builtin_ctzll(s2), PAWN, NO_PIECE, NO_PIECE, 0);
                }
            }

            BB ep_bb = (ep >= 0) ? bit(ep) : 0;
            BB atk = pawn_atk[turn][from] & (enemy_bb | ep_bb);
            while (atk) {
                int to = __builtin_ctzll(atk);
                atk &= atk - 1;
                if (to == ep) {
                    state64_add_move(s, &ctx, from, to, PAWN, PAWN, NO_PIECE, MOVE_EP);
                } else {
                    int capture = v.piece[to] >= 0 ? v.piece[to] : NO_PIECE;
                    if (rank_of(to) == promo_rank) {
                        for (int promo = KNIGHT; promo <= QUEEN; promo++)
                            state64_add_move(s, &ctx, from, to, PAWN, capture, promo, 0);
                    } else {
                        state64_add_move(s, &ctx, from, to, PAWN, capture, NO_PIECE, 0);
                    }
                }
            }
            continue;
        }

        BB dests = 0;
        BB cands = s64_static.geom_bb[piece][from];
        while (cands) {
            int to = __builtin_ctzll(cands);
            cands &= cands - 1;
            if (friendly & bit(to)) continue;
            if ((piece == KNIGHT || piece == KING) || !(path_mask[from][to] & occ))
                dests |= bit(to);
        }

        while (dests) {
            int to = __builtin_ctzll(dests);
            dests &= dests - 1;
            int capture = v.piece[to] >= 0 ? v.piece[to] : NO_PIECE;
            state64_add_move(s, &ctx, from, to, piece, capture, NO_PIECE, 0);
        }
    }

    if (turn == WHITE) {
        if ((castling & CASTLE_WK) && !(occ & 0x60ULL) &&
            !state64_view_square_attacked(&v, 4, enemy) &&
            !state64_view_square_attacked(&v, 5, enemy) &&
            !state64_view_square_attacked(&v, 6, enemy))
            state64_add_move(s, &ctx, 4, 6, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
        if ((castling & CASTLE_WQ) && !(occ & 0x0EULL) &&
            !state64_view_square_attacked(&v, 4, enemy) &&
            !state64_view_square_attacked(&v, 3, enemy) &&
            !state64_view_square_attacked(&v, 2, enemy))
            state64_add_move(s, &ctx, 4, 2, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
    } else {
        if ((castling & CASTLE_BK) && !(occ & 0x6000000000000000ULL) &&
            !state64_view_square_attacked(&v, 60, enemy) &&
            !state64_view_square_attacked(&v, 61, enemy) &&
            !state64_view_square_attacked(&v, 62, enemy))
            state64_add_move(s, &ctx, 60, 62, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
        if ((castling & CASTLE_BQ) && !(occ & 0x0E00000000000000ULL) &&
            !state64_view_square_attacked(&v, 60, enemy) &&
            !state64_view_square_attacked(&v, 59, enemy) &&
            !state64_view_square_attacked(&v, 58, enemy))
            state64_add_move(s, &ctx, 60, 58, KING, NO_PIECE, NO_PIECE, MOVE_CASTLE);
    }
}

void state64_rebuild(State64 *s) {
    state64_close(s);
}

BB state64_legal_from(const State64 *s, int from) {
    BB result = 0;
    if (from < 0 || from >= 64) return 0;

    for (int to = 0; to < 64; to++) {
        if (to == from) continue;
        if (s->m[from][to] & S64_LEGAL) result |= bit(to);
    }
    return result;
}

int state64_cell_moves(const State64 *s, int from, int to, Move *out, int max) {
    if (from < 0 || from >= 64 || to < 0 || to >= 64) return 0;
    if (from == to) return 0;
    if (!(s->m[from][to] & S64_LEGAL)) return 0;

    Pos pos;
    state64_to_pos(s, &pos);

    MoveList ml;
    gen_moves(&pos, &ml);

    int n = 0;
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (m.from != from || m.to != to) continue;

        Pos next = pos_after(&pos, m);
        if (in_check(&next, pos.turn)) continue;

        if (n < max) out[n] = m;
        n++;
    }
    return n;
}

void state64_gen_moves(const State64 *s, MoveList *ml) {
    ml->count = 0;

    for (int from = 0; from < 64; from++) {
        int color, piece;
        if (!code_piece(s->m[from][from], &color, &piece)) continue;

        for (int base = 0; base < 64; base += 8) {
            uint64_t chunk;
            memcpy(&chunk, &s->m[from][base], sizeof(chunk));
            uint64_t legal_bytes = chunk & 0x0808080808080808ULL;

            while (legal_bytes) {
                int lane = __builtin_ctzll(legal_bytes) / 8;
                int to = base + lane;
                legal_bytes &= legal_bytes - 1;
                if (from == to) continue;

            uint8_t cell = s->m[from][to];

            int cap_color, cap_piece;
            uint8_t capture = NO_PIECE;
            if (cell & S64_EP) {
                capture = PAWN;
            } else if (cell & S64_CAPTURE) {
                capture = code_piece(s->m[to][to], &cap_color, &cap_piece) ?
                          (uint8_t)cap_piece : NO_PIECE;
            }

            uint8_t flags = 0;
            if (cell & S64_EP)     flags |= MOVE_EP;
            if (cell & S64_CASTLE) flags |= MOVE_CASTLE;

            if (cell & S64_PROMO) {
                for (int promo = KNIGHT; promo <= QUEEN; promo++) {
                    Move *m = &ml->moves[ml->count++];
                    m->from = (uint8_t)from;
                    m->to = (uint8_t)to;
                    m->piece = (uint8_t)piece;
                    m->capture = capture;
                    m->promo = (uint8_t)promo;
                    m->flags = flags;
                }
            } else {
                Move *m = &ml->moves[ml->count++];
                m->from = (uint8_t)from;
                m->to = (uint8_t)to;
                m->piece = (uint8_t)piece;
                m->capture = capture;
                m->promo = NO_PIECE;
                m->flags = flags;
            }
            }
        }
    }
}

void state64_apply_known_legal(const State64 *s, Move m, State64 *next) {
    Pos pos;
    state64_to_pos(s, &pos);
    Pos p = pos_after(&pos, m);
    state64_from_pos(next, &p);
}

static void state64_raw_apply_known_legal(const State64 *s, Move m, State64 *next) {
    *next = *s;

    int c = meta_bit(s, META_TURN) ? BLACK : WHITE;
    int e = 1 - c;
    int old_halfmove = (int)meta_u(s, META_HALFMOVE_0, 8);
    int old_fullmove = (int)meta_u(s, META_FULLMOVE_0, 16);
    int castling = 0;

    if (meta_bit(s, META_CASTLE_WK)) castling |= CASTLE_WK;
    if (meta_bit(s, META_CASTLE_WQ)) castling |= CASTLE_WQ;
    if (meta_bit(s, META_CASTLE_BK)) castling |= CASTLE_BK;
    if (meta_bit(s, META_CASTLE_BQ)) castling |= CASTLE_BQ;

    int halfmove = old_halfmove + 1;
    int fullmove = old_fullmove + (c == BLACK);
    int ep = -1;

    if (m.capture != NO_PIECE && !(m.flags & MOVE_EP))
        set_square_code(next, m.to, 0);

    if (m.flags & MOVE_EP) {
        int cap_sq = (c == WHITE) ? m.to - 8 : m.to + 8;
        set_square_code(next, cap_sq, 0);
        halfmove = 0;
    }

    set_square_code(next, m.from, 0);
    set_square_code(next, m.to, piece_code(c, m.promo != NO_PIECE ? m.promo : m.piece));

    if (m.flags & MOVE_CASTLE) {
        if      (m.to == 6)  { set_square_code(next, 7, 0);  set_square_code(next, 5,  piece_code(WHITE, ROOK)); }
        else if (m.to == 2)  { set_square_code(next, 0, 0);  set_square_code(next, 3,  piece_code(WHITE, ROOK)); }
        else if (m.to == 62) { set_square_code(next, 63, 0); set_square_code(next, 61, piece_code(BLACK, ROOK)); }
        else if (m.to == 58) { set_square_code(next, 56, 0); set_square_code(next, 59, piece_code(BLACK, ROOK)); }
    }

    if (m.piece == KING) {
        if (c == WHITE) castling &= ~(CASTLE_WK | CASTLE_WQ);
        else            castling &= ~(CASTLE_BK | CASTLE_BQ);
    }
    if (m.piece == ROOK) {
        if (c == WHITE && m.from == 0)  castling &= ~CASTLE_WQ;
        if (c == WHITE && m.from == 7)  castling &= ~CASTLE_WK;
        if (c == BLACK && m.from == 56) castling &= ~CASTLE_BQ;
        if (c == BLACK && m.from == 63) castling &= ~CASTLE_BK;
    }
    if (m.capture == ROOK) {
        if (e == WHITE && m.to == 0)  castling &= ~CASTLE_WQ;
        if (e == WHITE && m.to == 7)  castling &= ~CASTLE_WK;
        if (e == BLACK && m.to == 56) castling &= ~CASTLE_BQ;
        if (e == BLACK && m.to == 63) castling &= ~CASTLE_BK;
    }

    if (m.piece == PAWN && abs((int)m.to - (int)m.from) == 16) {
        ep = ((int)m.from + (int)m.to) / 2;
        halfmove = 0;
    }
    if (m.piece == PAWN || m.capture != NO_PIECE) halfmove = 0;

    set_meta_bit(next, META_TURN, e == BLACK);
    set_meta_bit(next, META_CASTLE_WK, castling & CASTLE_WK);
    set_meta_bit(next, META_CASTLE_WQ, castling & CASTLE_WQ);
    set_meta_bit(next, META_CASTLE_BK, castling & CASTLE_BK);
    set_meta_bit(next, META_CASTLE_BQ, castling & CASTLE_BQ);
    set_meta_bit(next, META_EP_VALID, ep >= 0);
    set_meta_u(next, META_EP_0, 6, (ep >= 0) ? (unsigned)ep : 0);
    set_meta_u(next, META_HALFMOVE_0, 8, (unsigned)(halfmove & 0xFF));
    set_meta_u(next, META_FULLMOVE_0, 16, (unsigned)(fullmove & 0xFFFF));

}

void state64_apply_matrix_known_legal(const State64 *s, Move m, State64 *next) {
    state64_raw_apply_known_legal(s, m, next);
    state64_close(next);
}

bool state64_apply(const State64 *s, Move m, State64 *next, State64Undo *undo) {
    Move candidates[4];
    int n = state64_cell_moves(s, m.from, m.to, candidates, 4);

    bool found = false;
    for (int i = 0; i < n && i < 4; i++) {
        if (move_same(candidates[i], m)) {
            found = true;
            break;
        }
    }
    if (!found) return false;

    if (undo) {
        undo->before = *s;
        undo->move = m;
    }

    state64_apply_matrix_known_legal(s, m, next);
    return true;
}

void state64_unapply(const State64Undo *undo, State64 *s) {
    *s = undo->before;
}

static BB influence_zone(int sq) {
    return geo_mask[ROOK][sq] | geo_mask[BISHOP][sq] | bit(sq);
}

void state64_close_delta(State64 *s, Move prev_move) {
    (void)prev_move;
    state64_close(s);
}

void state64_apply_delta(const State64 *s, Move m, State64 *next) {
    state64_raw_apply_known_legal(s, m, next);
    state64_close_delta(next, m);
}
