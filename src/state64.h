#pragma once
#include "types.h"
#include "movegen.h"

typedef struct {
    uint8_t m[64][64];
} State64;

typedef struct {
    uint8_t geom[NPIECES][64][64];
    BB geom_bb[NPIECES][64];
    uint8_t path[64][64];
} State64Static;

typedef struct {
    State64 before;
    Move move;
} State64Undo;

typedef struct {
    unsigned long long candidates;
    unsigned long long certified;
    unsigned long long fallback;
    unsigned long long fallback_rejected;
} State64Stats;

#define S64_GEOM    0x01
#define S64_CLEAR   0x02
#define S64_ATTACK  0x04
#define S64_LEGAL   0x08
#define S64_CAPTURE 0x10
#define S64_PROMO   0x20
#define S64_EP      0x40
#define S64_CASTLE  0x80

void state64_static_init(void);
void state64_from_pos(State64 *s, const Pos *pos);
void state64_to_pos(const State64 *s, Pos *pos);
void state64_close(State64 *s);
void state64_rebuild(State64 *s);
BB   state64_legal_from(const State64 *s, int from);
int  state64_cell_moves(const State64 *s, int from, int to, Move *out, int max);
void state64_gen_moves(const State64 *s, MoveList *ml);
void state64_apply_known_legal(const State64 *s, Move m, State64 *next);
void state64_apply_matrix_known_legal(const State64 *s, Move m, State64 *next);
bool state64_apply(const State64 *s, Move m, State64 *next, State64Undo *undo);
void state64_unapply(const State64Undo *undo, State64 *s);
void state64_stats_reset(void);
State64Stats state64_stats_get(void);
