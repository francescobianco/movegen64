#include "state64.h"
#include "tables.h"
#include "position.h"
#include <stdio.h>
#include <string.h>

static int same_pos(const Pos *a, const Pos *b) {
    return memcmp(a->pieces, b->pieces, sizeof(a->pieces)) == 0 &&
           a->by_color[WHITE] == b->by_color[WHITE] &&
           a->by_color[BLACK] == b->by_color[BLACK] &&
           a->occupied == b->occupied &&
           a->turn == b->turn &&
           a->ep == b->ep &&
           a->castling == b->castling &&
           a->halfmove == b->halfmove &&
           a->fullmove == b->fullmove;
}

static int popcount64(BB bb) {
    int n = 0;
    while (bb) {
        bb &= bb - 1;
        n++;
    }
    return n;
}

static int legal_count_from_pos(const Pos *pos) {
    MoveList ml;
    gen_moves(pos, &ml);

    int n = 0;
    for (int i = 0; i < ml.count; i++) {
        Pos next = pos_after(pos, ml.moves[i]);
        if (!in_check(&next, pos->turn)) n++;
    }
    return n;
}

static int legal_count_from_state(const State64 *s) {
    int n = 0;
    for (int from = 0; from < 64; from++)
        n += popcount64(state64_legal_from(s, from));
    return n;
}

static int check_fen(const char *fen) {
    Pos pos;
    State64 s;

    pos_from_fen(&pos, fen);
    state64_from_pos(&s, &pos);

    int expected = legal_count_from_pos(&pos);
    int got = legal_count_from_state(&s);
    if (got != expected) {
        fprintf(stderr, "State64 legal count mismatch for FEN: expected %d, got %d\n", expected, got);
        return 1;
    }
    return 0;
}

int main(void) {
    tables_init();

    Pos start, decoded;
    State64 s, next, restored;
    State64Undo undo;

    pos_set_start(&start);
    state64_from_pos(&s, &start);
    state64_to_pos(&s, &decoded);

    if (!same_pos(&start, &decoded)) {
        fprintf(stderr, "State64 round-trip changed the start position\n");
        return 1;
    }

    int legal = 0;
    for (int from = 0; from < 64; from++)
        legal += popcount64(state64_legal_from(&s, from));
    if (legal != 20) {
        fprintf(stderr, "State64 start legal count: expected 20, got %d\n", legal);
        return 1;
    }

    Move moves[4];
    int n = state64_cell_moves(&s, sq(1, 4), sq(3, 4), moves, 4);
    if (n != 1) {
        fprintf(stderr, "State64 e2e4 expansion: expected 1, got %d\n", n);
        return 1;
    }

    if (!state64_apply(&s, moves[0], &next, &undo)) {
        fprintf(stderr, "State64 rejected legal e2e4\n");
        return 1;
    }

    Pos expected = pos_after(&start, moves[0]);
    state64_to_pos(&next, &decoded);
    if (!same_pos(&expected, &decoded)) {
        fprintf(stderr, "State64 apply e2e4 produced the wrong position\n");
        return 1;
    }

    state64_unapply(&undo, &restored);
    state64_to_pos(&restored, &decoded);
    if (!same_pos(&start, &decoded)) {
        fprintf(stderr, "State64 unapply did not restore the start position\n");
        return 1;
    }

    if (check_fen("r3k2r/p1ppqpb1/bn2pnp1/2pP4/1p2P3/2N2N2/PPPBQPPP/R3K2R w KQkq - 0 1"))
        return 1;
    if (check_fen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"))
        return 1;
    if (check_fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"))
        return 1;
    if (check_fen("8/8/8/3pP3/8/8/8/4K2k w - d6 0 1"))
        return 1;

    puts("PASS state64 roundtrip");
    return 0;
}
