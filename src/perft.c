#include "perft.h"
#include "movegen.h"
#include "position.h"
#include <stdio.h>

long long perft(const Pos *pos, int depth) {
    MoveList ml;
    gen_moves(pos, &ml);

    if (depth == 1) {
        /* count only legal moves — avoids one extra recursive call */
        long long count = 0;
        for (int i = 0; i < ml.count; i++) {
            Pos next = pos_after(pos, ml.moves[i]);
            if (!in_check(&next, pos->turn)) count++;
        }
        return count;
    }

    long long nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        Pos next = pos_after(pos, ml.moves[i]);
        if (in_check(&next, pos->turn)) continue;
        nodes += perft(&next, depth - 1);
    }
    return nodes;
}

long long state64_perft(const State64 *s, int depth) {
    Move null_move = {0, 0, NO_PIECE, NO_PIECE, NO_PIECE, 0};
    return state64_perft_depth(s, depth, null_move);
}

long long state64_perft_depth(const State64 *s, int depth, Move prev_move) {
    MoveList ml;
    state64_gen_moves(s, &ml);

    long long nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        if (depth == 1) {
            nodes++;
            continue;
        }

        State64 next;
        state64_apply_delta(s, ml.moves[i], &next);
        nodes += state64_perft_depth(&next, depth - 1, ml.moves[i]);
    }
    return nodes;
}

static void move_str(Move m, char *buf) {
    static const char files[] = "abcdefgh";
    static const char promos[] = "  nbrq";
    buf[0] = files[file_of(m.from)];
    buf[1] = '1' + rank_of(m.from);
    buf[2] = files[file_of(m.to)];
    buf[3] = '1' + rank_of(m.to);
    buf[4] = (m.promo != NO_PIECE) ? promos[m.promo] : '\0';
    buf[5] = '\0';
}

void perft_divide(const Pos *pos, int depth) {
    MoveList ml;
    gen_moves(pos, &ml);
    long long total = 0;
    char buf[8];
    for (int i = 0; i < ml.count; i++) {
        Pos next = pos_after(pos, ml.moves[i]);
        if (in_check(&next, pos->turn)) continue;
        long long n = (depth > 1) ? perft(&next, depth-1) : 1;
        move_str(ml.moves[i], buf);
        printf("  %s: %lld\n", buf, n);
        total += n;
    }
    printf("total: %lld\n", total);
}
