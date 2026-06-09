/*
 * cost64 simulation harness.
 *
 * 1. correctness: cost64_gen (legality read off the matrix) must produce
 *    exactly the same move sets as legal64 at every node of the test trees;
 * 2. structure: density of the cost layers (how much signal the matrix holds);
 * 3. incrementality: how many of the 64 rows are invalidated by one move
 *    (the update set is itself read from the matrix: columns of the changed
 *    squares) — this measures the potential of incremental maintenance;
 * 4. timing: full rebuild cost, generation cost, perft comparison.
 *
 * Run with argument "quick" for the correctness section only.
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
#include "cost64.h"
#include "relax64.h"

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

/* ---------------- sample collection ---------------- */

#define MAX_SAMPLES 8192
static Pos samples[MAX_SAMPLES];
static int nsamples = 0;

/* ---------------- correctness ---------------- */

static long long nodes_checked = 0;

/* m is the matrix carried incrementally from the root via cost64_apply;
   it must agree with a fresh full rebuild on every valid row, cover all
   occupied rows, and yield exactly the legal64 move set */
static int verify(const Pos *pos, const Cost64 *m, int depth) {
    MoveList ref, mine;
    int rk[256], mk[256];

    Cost64 fresh;
    cost64_build(pos, &fresh);

    if (pos->occupied & ~m->valid) {
        printf("STALE OCCUPIED ROW:\n"); pos_print(pos);
        return 0;
    }
    for (BB v = m->valid; v; v &= v - 1) {
        int r = __builtin_ctzll(v);
        if (memcmp(m->c[r], fresh.c[r], 64) != 0) {
            printf("ROW %d DIVERGED FROM REBUILD:\n", r); pos_print(pos);
            return 0;
        }
    }

    legal64_gen(pos, &ref);
    cost64_gen(pos, m, &mine);

    for (int i = 0; i < ref.count; i++)  rk[i] = move_key(ref.moves[i]);
    for (int i = 0; i < mine.count; i++) mk[i] = move_key(mine.moves[i]);
    qsort(rk, ref.count, sizeof(int), cmp_int);
    qsort(mk, mine.count, sizeof(int), cmp_int);

    if (ref.count != mine.count ||
        memcmp(rk, mk, ref.count * sizeof(int)) != 0) {
        printf("MISMATCH (legal64=%d cost64=%d):\n", ref.count, mine.count);
        pos_print(pos);
        printf("ep=%d castling=%d\n", pos->ep, pos->castling);
        return 0;
    }

    /* plane-based generation must agree too (rk still holds ref keys) */
    {
        Relax64 rx;
        relax64_step(pos->occupied, &rx);
        MoveList rml;
        relax64_gen(pos, &rx, &rml);
        int xk[256];
        for (int i = 0; i < rml.count; i++) xk[i] = move_key(rml.moves[i]);
        qsort(xk, rml.count, sizeof(int), cmp_int);
        if (rml.count != ref.count ||
            memcmp(rk, xk, ref.count * sizeof(int)) != 0) {
            printf("RELAX64_GEN MISMATCH (legal64=%d relax64=%d):\n",
                   ref.count, rml.count);
            pos_print(pos);
            return 0;
        }
    }

    /* duality: both colours from the same matrix; the off-turn side must
       match legal64 run on the turn-flipped position (no e.p. for it) */
    {
        MoveList both[2], oref;
        cost64_gen_both(pos, m, &both[WHITE], &both[BLACK]);
        Pos flipped = *pos;
        flipped.turn = 1 - pos->turn;
        flipped.ep   = -1;
        legal64_gen(&flipped, &oref);
        MoveList *other = &both[flipped.turn];

        for (int i = 0; i < oref.count; i++)   rk[i] = move_key(oref.moves[i]);
        for (int i = 0; i < other->count; i++) mk[i] = move_key(other->moves[i]);
        qsort(rk, oref.count, sizeof(int), cmp_int);
        qsort(mk, other->count, sizeof(int), cmp_int);
        if (oref.count != other->count ||
            memcmp(rk, mk, oref.count * sizeof(int)) != 0) {
            printf("GEN_BOTH OFF-TURN MISMATCH (legal64=%d cost64=%d):\n",
                   oref.count, other->count);
            pos_print(pos);
            return 0;
        }
    }

    nodes_checked++;
    if (nsamples < MAX_SAMPLES) samples[nsamples++] = *pos;

    if (depth > 1)
        for (int i = 0; i < ref.count; i++) {
            Pos next = pos_after(pos, ref.moves[i]);
            Cost64 nm;
            cost64_apply(m, &next, ref.moves[i], pos->turn, &nm);
            if (!verify(&next, &nm, depth - 1)) return 0;
        }
    return 1;
}

typedef struct { const char *name, *fen; int cmp_depth, perft_depth; } Case;

static const Case CASES[] = {
    { "startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 5 },
    { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 4 },
    { "pos3",     "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 5 },
    { "pos4",     "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 4 },
    { "pos5",     "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 4 },
    { "pos6",     "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 3, 4 },
};
#define NCASES ((int)(sizeof(CASES)/sizeof(CASES[0])))

/* ---------------- structure & incrementality ---------------- */

static void analyze(void) {
    long long layer[4] = {0,0,0,0};
    long long moves_seen = 0, dirty_sum = 0, dirty_full_sum = 0;
    long long ray_sum = 0, ray_occ_sum = 0;
    long long load[64] = {0};
    int dirty_max = 0;

    for (int i = 0; i < nsamples; i++) {
        const Pos *pos = &samples[i];
        Cost64 m;
        cost64_build(pos, &m);

        for (int f = 0; f < 64; f++)
            for (int t = 0; t < 64; t++)
                if (m.c[f][t] != C64_NONE)
                    layer[c64_cost(m.c[f][t])]++;

        /* centrality load map: rays a square invalidates when it flips,
           counted over the rows that actually get maintained (occupied) */
        for (int s = 0; s < 64; s++)
            for (BB o = pos->occupied & ~bit(s); o; o &= o - 1) {
                int r = __builtin_ctzll(o);
                uint8_t b = m.c[r][s];
                if (b != C64_NONE && c64_cost(b) <= 2) load[s]++;
            }

        MoveList ml;
        legal64_gen(pos, &ml);
        for (int k = 0; k < ml.count; k++) {
            Move mv = ml.moves[k];
            int chg[4], nchg = 0;
            chg[nchg++] = mv.from;
            chg[nchg++] = mv.to;
            if (mv.flags & MOVE_EP)
                chg[nchg++] = (pos->turn == WHITE) ? mv.to - 8 : mv.to + 8;
            if (mv.flags & MOVE_CASTLE) {
                chg[nchg++] = (mv.to > mv.from) ? mv.to + 1 : mv.to - 2;
                chg[nchg++] = (mv.to > mv.from) ? mv.to - 1 : mv.to + 1;
            }

            /* rows invalidated by the occupancy change: read straight from
               the columns of the changed squares */
            int dirty = 0, dirty_full = 0, rays = 0, rays_occ = 0;
            for (int r = 0; r < 64; r++) {
                int hit = 0, hit_full = 0, dirs_seen = 0;
                for (int j = 0; j < nchg; j++) {
                    if (r == chg[j]) continue;
                    uint8_t b = m.c[r][chg[j]];
                    if (b == C64_NONE) continue;
                    hit_full = 1;
                    if (c64_cost(b) <= 2) {
                        hit = 1;
                        /* only this ray of row r needs a refill */
                        if (!(dirs_seen & (1 << c64_dir(b)))) {
                            dirs_seen |= 1 << c64_dir(b);
                            rays++;
                            if (bit(r) & pos->occupied) rays_occ++;
                        }
                    }
                }
                dirty      += hit;
                dirty_full += hit_full | hit;
            }
            ray_sum     += rays;
            ray_occ_sum += rays_occ;
            dirty_sum      += dirty;
            dirty_full_sum += dirty_full;
            if (dirty > dirty_max) dirty_max = dirty;
            moves_seen++;
        }
    }

    double cells = 64.0 * 64.0;
    printf("\n--- matrix structure (%d positions) ---\n", nsamples);
    printf("avg cells  cost0: %6.1f   cost1: %6.1f   cost2: %6.1f   cost3+: %6.1f   (of %.0f)\n",
           (double)layer[0]/nsamples, (double)layer[1]/nsamples,
           (double)layer[2]/nsamples, (double)layer[3]/nsamples, cells);
    printf("ray-related cells: %4.1f%%   legality signal (cost<=2): %4.1f%%\n",
           100.0*(layer[0]+layer[1]+layer[2]+layer[3])/(cells*nsamples),
           100.0*(layer[0]+layer[1]+layer[2])/(cells*nsamples));

    printf("\n--- incrementality (%lld moves simulated) ---\n", moves_seen);
    printf("rows invalidated per move:  avg %.1f / 64  (%.0f%%),  max %d\n",
           (double)dirty_sum/moves_seen, 100.0*dirty_sum/(64.0*moves_seen), dirty_max);
    printf("with cost-3 tracking too:   avg %.1f / 64  (%.0f%%)\n",
           (double)dirty_full_sum/moves_seen, 100.0*dirty_full_sum/(64.0*moves_seen));
    printf("rays invalidated per move:  avg %.1f / 512 (%.1f%%)  — ray-granular update unit\n",
           (double)ray_sum/moves_seen, 100.0*ray_sum/(512.0*moves_seen));
    printf("...restricted to occupied rows (the maintained subspace): avg %.1f\n",
           (double)ray_occ_sum/moves_seen);

    printf("\n--- centrality load map (avg maintained rays invalidated by a flip of each square) ---\n");
    for (int r = 7; r >= 0; r--) {
        printf("  ");
        for (int f = 0; f < 8; f++)
            printf("%5.1f", (double)load[sq(r,f)]/nsamples);
        printf("\n");
    }
}

/* ---------------- timing ---------------- */

static long long cost64_perft(const Pos *pos, int depth) {
    Cost64 m;
    cost64_build(pos, &m);
    MoveList ml;
    cost64_gen(pos, &m, &ml);
    if (depth == 1) return ml.count;
    long long nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        Pos next = pos_after(pos, ml.moves[i]);
        nodes += cost64_perft(&next, depth - 1);
    }
    return nodes;
}

static long long perft_inc_rec(const Pos *pos, const Cost64 *m, int depth) {
    MoveList ml;
    cost64_gen(pos, m, &ml);
    if (depth == 1) return ml.count;
    long long nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        Pos next = pos_after(pos, ml.moves[i]);
        Cost64 nm;
        cost64_apply(m, &next, ml.moves[i], pos->turn, &nm);
        nodes += perft_inc_rec(&next, &nm, depth - 1);
    }
    return nodes;
}

static long long cost64_perft_inc(const Pos *pos, int depth) {
    Cost64 m;
    cost64_build(pos, &m);
    return perft_inc_rec(pos, &m, depth);
}

/* memoryless: relax + plane generation at every node, nothing carried */
static long long relax_perft(const Pos *pos, int depth) {
    Relax64 r;
    relax64_step(pos->occupied, &r);
    MoveList ml;
    relax64_gen(pos, &r, &ml);
    if (depth == 1) return ml.count;
    long long nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        Pos next = pos_after(pos, ml.moves[i]);
        nodes += relax_perft(&next, depth - 1);
    }
    return nodes;
}

static void bench(void) {
    /* micro: rebuild and generation over the sample set */
    int n = nsamples < 2000 ? nsamples : 2000;
    static Cost64 mats[2000];
    volatile long long sink = 0;

    double t0 = now_ms();
    for (int r = 0; r < 5; r++)
        for (int i = 0; i < n; i++) cost64_build(&samples[i], &mats[i]);
    double build_ns = (now_ms() - t0) * 1e6 / (5.0 * n);

    MoveList ml;
    double t1 = now_ms();
    for (int r = 0; r < 5; r++)
        for (int i = 0; i < n; i++) { cost64_gen(&samples[i], &mats[i], &ml); sink += ml.count; }
    double genm_ns = (now_ms() - t1) * 1e6 / (5.0 * n);

    double t2 = now_ms();
    for (int r = 0; r < 5; r++)
        for (int i = 0; i < n; i++) { legal64_gen(&samples[i], &ml); sink += ml.count; }
    double genl_ns = (now_ms() - t2) * 1e6 / (5.0 * n);

    /* incremental apply: every legal move of every sampled position */
    long long applies = 0;
    double t3 = now_ms();
    for (int i = 0; i < n; i++) {
        legal64_gen(&samples[i], &ml);
        for (int k = 0; k < ml.count; k++) {
            Pos next = pos_after(&samples[i], ml.moves[k]);
            Cost64 nm;
            cost64_apply(&mats[i], &next, ml.moves[k], samples[i].turn, &nm);
            sink += nm.valid;
            applies++;
        }
    }
    double apply_ns = (now_ms() - t3) * 1e6 / (double)applies;

    /* transport-only relax: memoryless full recompute, bit-plane output.
       First exactness against the byte matrix layers, then speed. */
    {
        static Relax64 rs;
        for (int i = 0; i < n && i < 256; i++) {
            relax64_step(samples[i].occupied, &rs);
            for (int s = 0; s < 64; s++)
                for (int t = 0; t < 64; t++) {
                    uint8_t b = mats[i].c[s][t];
                    int want = (b != C64_NONE && c64_cost(b) <= 2) ? c64_cost(b) : -1;
                    int got  = (rs.L0[s] & bit(t)) ? 0 :
                               (rs.L1[s] & bit(t)) ? 1 :
                               (rs.L2[s] & bit(t)) ? 2 : -1;
                    if (want != got) {
                        printf("RELAX64 LAYER MISMATCH at sample %d, c[%d][%d]: "
                               "byte=%d planes=%d\n", i, s, t, want, got);
                        exit(1);
                    }
                }
        }
        double t6 = now_ms();
        for (int rep = 0; rep < 5; rep++)
            for (int i = 0; i < n; i++) {
                relax64_step(samples[i].occupied, &rs);
                sink += (long long)rs.L0[0];
            }
        double relax_ns = (now_ms() - t6) * 1e6 / (5.0 * n);
        printf("\nrelax64_step (memoryless transport-only, all 512 rays): %8.0f ns"
               "  — exact on %d positions\n",
               relax_ns, n < 256 ? n : 256);

        /* plane-based generation alone, planes precomputed */
        static Relax64 rmats[2000];
        for (int i = 0; i < n; i++) relax64_step(samples[i].occupied, &rmats[i]);
        MoveList rml;
        double t7 = now_ms();
        for (int rep = 0; rep < 5; rep++)
            for (int i = 0; i < n; i++) {
                relax64_gen(&samples[i], &rmats[i], &rml);
                sink += rml.count;
            }
        double rgen_ns = (now_ms() - t7) * 1e6 / (5.0 * n);
        printf("relax64_gen  (plane reads): %8.0f ns\n", rgen_ns);
    }

    /* duality: both colours from one matrix vs two legal64 passes */
    MoveList mw, mb;
    double t4 = now_ms();
    for (int r = 0; r < 5; r++)
        for (int i = 0; i < n; i++) {
            cost64_gen_both(&samples[i], &mats[i], &mw, &mb);
            sink += mw.count + mb.count;
        }
    double both_ns = (now_ms() - t4) * 1e6 / (5.0 * n);

    double t5 = now_ms();
    for (int r = 0; r < 5; r++)
        for (int i = 0; i < n; i++) {
            Pos flipped = samples[i];
            flipped.turn = 1 - flipped.turn;
            flipped.ep = -1;
            legal64_gen(&samples[i], &mw);
            legal64_gen(&flipped, &mb);
            sink += mw.count + mb.count;
        }
    double both_l_ns = (now_ms() - t5) * 1e6 / (5.0 * n);

    printf("\n--- timing (avg over %d positions) ---\n", n);
    printf("cost64_build (sym, 4 directions):  %8.0f ns\n", build_ns);
    printf("cost64_apply (ray-granular delta): %8.0f ns  (%lld applies)\n", apply_ns, applies);
    printf("cost64_gen   (reads on matrix):    %8.0f ns\n", genm_ns);
    printf("legal64_gen  (direct fills):       %8.0f ns\n", genl_ns);
    printf("both colours: cost64_gen_both      %8.0f ns   vs 2x legal64 %8.0f ns\n",
           both_ns, both_l_ns);

    printf("\n--- perft: rebuild vs incremental vs alternatives (ms) ---\n");
    printf("%-9s %5s %15s %10s %10s %10s %10s %10s\n",
           "position", "depth", "nodes", "base", "legal64", "rebuild", "increm", "relax");
    for (int c = 0; c < NCASES; c++) {
        Pos pos;
        pos_from_fen(&pos, CASES[c].fen);
        int d = CASES[c].perft_depth;

        double ta = now_ms();
        long long n1 = perft(&pos, d);
        double msa = now_ms() - ta;

        double tb = now_ms();
        long long n2 = legal64_perft(&pos, d);
        double msb = now_ms() - tb;

        double tc = now_ms();
        long long n3 = cost64_perft(&pos, d);
        double msc = now_ms() - tc;

        double td = now_ms();
        long long n4 = cost64_perft_inc(&pos, d);
        double msd = now_ms() - td;

        double te = now_ms();
        long long n5 = relax_perft(&pos, d);
        double mse = now_ms() - te;

        printf("%-9s %5d %15lld %9.1f %9.1f %9.1f %9.1f %9.1f%s\n",
               CASES[c].name, d, n5, msa, msb, msc, msd, mse,
               (n1 != n2 || n1 != n3 || n1 != n4 || n1 != n5)
                   ? "  *** MISMATCH ***" : "");
    }
    (void)sink;
}

int main(int argc, char **argv) {
    int quick = (argc > 1 && strcmp(argv[1], "quick") == 0);
    tables_init();

    for (int c = 0; c < NCASES; c++) {
        Pos pos;
        pos_from_fen(&pos, CASES[c].fen);
        nodes_checked = 0;
        Cost64 m;
        cost64_build(&pos, &m);
        if (!verify(&pos, &m, CASES[c].cmp_depth)) {
            printf("FAIL: %s\n", CASES[c].name);
            return 1;
        }
        printf("ok  %-9s incremental cost64 == rebuild == legal64 on %lld nodes (depth %d)\n",
               CASES[c].name, nodes_checked, CASES[c].cmp_depth);
    }

    if (!quick) {
        analyze();
        bench();
    }
    printf("\nall checks passed\n");
    return 0;
}