/*
 * Validates legal64 (direct legal generation, no make/unmake) against the
 * baseline (pseudo-legal + pos_after + in_check filter) at every node of
 * the perft trees of the standard test positions, then cross-checks perft
 * counts and reports a timing comparison.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tables.h"
#include "position.h"
#include "movegen.h"
#include "perft.h"
#include "legal64.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int move_key(Move m) {
    return ((int)m.from << 16) | ((int)m.to << 8) | (int)m.promo;
}

static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

static void print_set(const char *tag, int *keys, int n) {
    static const char files[] = "abcdefgh";
    static const char promos[] = "  nbrq";
    printf("%s (%d):", tag, n);
    for (int i = 0; i < n; i++) {
        int from = keys[i] >> 16, to = (keys[i] >> 8) & 0xFF, pr = keys[i] & 0xFF;
        printf(" %c%d%c%d", files[file_of(from)], 1 + rank_of(from),
                            files[file_of(to)],   1 + rank_of(to));
        if (pr != NO_PIECE) putchar(promos[pr]);
    }
    printf("\n");
}

static long long nodes_checked = 0;

/* compare move sets at this node, then recurse through the legal moves */
static int verify(const Pos *pos, int depth) {
    MoveList base, mine;
    int bk[256], mk[256];

    gen_moves(pos, &base);
    int nb = 0;
    for (int i = 0; i < base.count; i++) {
        Pos next = pos_after(pos, base.moves[i]);
        if (!in_check(&next, pos->turn))
            bk[nb++] = move_key(base.moves[i]);
    }

    legal64_gen(pos, &mine);
    int nm = 0;
    for (int i = 0; i < mine.count; i++)
        mk[nm++] = move_key(mine.moves[i]);

    qsort(bk, nb, sizeof(int), cmp_int);
    qsort(mk, nm, sizeof(int), cmp_int);

    if (nb != nm || memcmp(bk, mk, nb * sizeof(int)) != 0) {
        printf("MISMATCH at this position:\n");
        pos_print(pos);
        printf("ep=%d castling=%d turn=%d\n", pos->ep, pos->castling, pos->turn);
        print_set("baseline", bk, nb);
        print_set("legal64 ", mk, nm);
        return 0;
    }

    nodes_checked++;
    if (depth > 1) {
        for (int i = 0; i < mine.count; i++) {
            Pos next = pos_after(pos, mine.moves[i]);
            if (!verify(&next, depth - 1)) return 0;
        }
    }
    return 1;
}

typedef struct {
    const char *name, *fen;
    int cmp_depth;          /* node-by-node set comparison depth */
    int perft_depth;
    long long expected;
} Case;

static const Case CASES[] = {
    { "startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      4, 5, 4865609LL },
    { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
      3, 4, 4085603LL },
    { "pos3",     "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
      4, 5, 674624LL },
    { "pos4",     "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
      3, 4, 422333LL },
    { "pos5",     "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
      3, 4, 2103487LL },
    { "pos6",     "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
      3, 4, 3894594LL },
};

int main(void) {
    tables_init();
    int ncases = (int)(sizeof(CASES) / sizeof(CASES[0]));

    /* 1. node-by-node move-set equality */
    for (int c = 0; c < ncases; c++) {
        Pos pos;
        pos_from_fen(&pos, CASES[c].fen);
        nodes_checked = 0;
        if (!verify(&pos, CASES[c].cmp_depth)) {
            printf("FAIL: %s\n", CASES[c].name);
            return 1;
        }
        printf("ok  %-9s move sets identical on %lld nodes (depth %d)\n",
               CASES[c].name, nodes_checked, CASES[c].cmp_depth);
    }

    /* 2. perft counts via legal64 alone (no filtering anywhere) */
    for (int c = 0; c < ncases; c++) {
        Pos pos;
        pos_from_fen(&pos, CASES[c].fen);
        long long n = legal64_perft(&pos, CASES[c].perft_depth);
        if (n != CASES[c].expected) {
            printf("FAIL: %s perft(%d) = %lld, expected %lld\n",
                   CASES[c].name, CASES[c].perft_depth, n, CASES[c].expected);
            return 1;
        }
        printf("ok  %-9s perft(%d) = %lld\n",
               CASES[c].name, CASES[c].perft_depth, n);
    }

    /* 3. timing: baseline (pseudo + make + in_check) vs legal64 */
    printf("\n%-9s %5s %15s %12s %12s %8s\n",
           "position", "depth", "nodes", "base ms", "legal64 ms", "speedup");
    for (int c = 0; c < ncases; c++) {
        Pos pos;
        pos_from_fen(&pos, CASES[c].fen);
        int d = CASES[c].perft_depth;

        double t0 = now_ms();
        long long n1 = perft(&pos, d);
        double ms1 = now_ms() - t0;

        double t1 = now_ms();
        long long n2 = legal64_perft(&pos, d);
        double ms2 = now_ms() - t1;

        printf("%-9s %5d %15lld %12.1f %12.1f %7.2fx%s\n",
               CASES[c].name, d, n2, ms1, ms2,
               ms2 > 0 ? ms1 / ms2 : 0.0,
               n1 != n2 ? "  *** MISMATCH ***" : "");
    }

    printf("\nall checks passed\n");
    return 0;
}