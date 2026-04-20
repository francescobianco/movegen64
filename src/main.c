#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "tables.h"
#include "position.h"
#include "movegen.h"
#include "perft.h"
#include "engine.h"
#include "state64.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void run_comparison(const Pos *pos, int max_depth) {
    pos_print(pos);

    printf("\n%-6s  %15s  %12s %12s  %12s %12s  %12s %12s  %9s\n",
           "depth", "nodes", "copy ms", "copy Mn/s", "delta ms", "delta Mn/s",
           "s64 ms", "s64 Mn/s", "s64 fb%");
    printf("%-6s  %15s  %12s %12s  %12s %12s  %12s %12s  %9s\n",
           "-----", "---------------",
           "-------", "---------", "--------", "----------", "------", "--------", "-------");

    engine_init(pos);
    State64 s64;
    state64_from_pos(&s64, pos);

    for (int d = 1; d <= max_depth; d++) {
        /* copy-based perft */
        double t0 = now_ms();
        long long n1 = perft(pos, d);
        double ms1 = now_ms() - t0;

        /* delta / make-unmake perft */
        engine_init(pos);
        double t1 = now_ms();
        long long n2 = engine_perft(d);
        double ms2 = now_ms() - t1;

        state64_stats_reset();
        double t2 = now_ms();
        long long n3 = state64_perft(&s64, d);
        double ms3 = now_ms() - t2;
        State64Stats st = state64_stats_get();

        double mn1 = (ms1 > 0) ? (n1/1e6)/(ms1/1000.0) : 0;
        double mn2 = (ms2 > 0) ? (n2/1e6)/(ms2/1000.0) : 0;
        double mn3 = (ms3 > 0) ? (n3/1e6)/(ms3/1000.0) : 0;
        double fb_pct = st.candidates ? (100.0 * (double)st.fallback / (double)st.candidates) : 0;

        printf("%-6d  %15lld  %12.1f %12.2f  %12.1f %12.2f  %12.1f %12.2f  %8.2f%s\n",
               d, n1, ms1, mn1, ms2, mn2, ms3, mn3, fb_pct,
               (n1 != n2 || n1 != n3) ? "  *** MISMATCH ***" : "");
    }
}

int main(int argc, char *argv[]) {
    tables_init();

    Pos pos;
    pos_set_start(&pos);

    int  depth = 5;
    int  mode  = 0;
    const char *fen = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "fen") == 0 && i+1 < argc)   { fen = argv[++i]; }
        else if (strcmp(argv[i], "divide") == 0)          { mode = 1; }
        else if (argv[i][0] >= '1' && argv[i][0] <= '9') { depth = atoi(argv[i]); }
    }

    if (fen) pos_from_fen(&pos, fen);

    if (mode == 1) {
        pos_print(&pos);
        printf("\nperft_divide(%d):\n", depth);
        perft_divide(&pos, depth);
    } else {
        run_comparison(&pos, depth);
    }

    return 0;
}
