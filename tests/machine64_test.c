/*
 * The closed-loop machine test: a 64-byte diagonal is the only persistent
 * state. Perft must be exact with every node regenerated from the diagonal
 * alone; random self-play games must stay legal end to end, cross-checked
 * against legal64 at every single position.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tables.h"
#include "position.h"
#include "movegen.h"
#include "legal64.h"
#include "machine64.h"

typedef struct { const char *name, *fen; int depth; long long expected; } Case;

static const Case CASES[] = {
    { "startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609LL },
    { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603LL },
    { "pos3",     "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624LL },
    { "pos4",     "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333LL },
    { "pos5",     "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487LL },
    { "pos6",     "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594LL },
};
#define NCASES ((int)(sizeof(CASES)/sizeof(CASES[0])))

static unsigned long long rng = 0x9E3779B97F4A7C15ULL;
static unsigned rnd(unsigned n) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (unsigned)(rng % n);
}

static int key(Move m) {
    return ((int)m.from << 16) | ((int)m.to << 8) | (int)m.promo;
}
static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int main(int argc, char **argv) {
    int games = (argc > 1) ? atoi(argv[1]) : 200;
    tables_init();

    /* 1. perft flowing entirely through the 64-byte diagonal */
    for (int c = 0; c < NCASES; c++) {
        Pos pos;
        pos_from_fen(&pos, CASES[c].fen);
        Diag64 m;
        machine64_from_pos(&pos, &m);
        long long n = machine64_perft(&m, CASES[c].depth);
        if (n != CASES[c].expected) {
            printf("FAIL %s: machine64 perft(%d) = %lld, expected %lld\n",
                   CASES[c].name, CASES[c].depth, n, CASES[c].expected);
            return 1;
        }
        printf("ok  %-9s machine64 perft(%d) = %lld  (state: 64 bytes)\n",
               CASES[c].name, CASES[c].depth, n);
    }

    /* 2. random self-play inside the machine, cross-checked every ply */
    int mates = 0, stalemates = 0, capped = 0;
    long long plies = 0;
    for (int g = 0; g < games; g++) {
        Pos pos;
        pos_set_start(&pos);
        Diag64 m;
        machine64_from_pos(&pos, &m);

        int ply;
        for (ply = 0; ply < 300; ply++) {
            MoveList ml, ref;
            machine64_gen(&m, &ml);

            /* the diagonal must reconstruct a position whose legal moves
               match legal64 exactly */
            Pos rec;
            machine64_to_pos(&m, &rec);
            legal64_gen(&rec, &ref);
            int a[256], b[256];
            for (int i = 0; i < ml.count; i++)  a[i] = key(ml.moves[i]);
            for (int i = 0; i < ref.count; i++) b[i] = key(ref.moves[i]);
            qsort(a, ml.count, sizeof(int), cmp_int);
            qsort(b, ref.count, sizeof(int), cmp_int);
            if (ml.count != ref.count ||
                memcmp(a, b, ml.count * sizeof(int)) != 0) {
                printf("FAIL game %d ply %d: machine=%d legal64=%d moves\n",
                       g, ply, ml.count, ref.count);
                pos_print(&rec);
                return 1;
            }

            if (ml.count == 0) {
                if (in_check(&rec, rec.turn)) mates++; else stalemates++;
                break;
            }
            machine64_apply(&m, ml.moves[rnd((unsigned)ml.count)]);
        }
        if (ply == 300) capped++;
        plies += ply;
    }
    printf("ok  self-play: %d games inside the machine, %lld plies all "
           "cross-validated\n    results: %d checkmates, %d stalemates, "
           "%d hit the 300-ply cap\n", games, plies, mates, stalemates, capped);

    printf("\nall checks passed\n");
    return 0;
}
