#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

typedef uint64_t BB;

#define WHITE 0
#define BLACK 1

typedef enum { PAWN=0, KNIGHT=1, BISHOP=2, ROOK=3, QUEEN=4, KING=5 } PieceType;
#define NPIECES 6

#define CASTLE_WK 1
#define CASTLE_WQ 2
#define CASTLE_BK 4
#define CASTLE_BQ 8

#define NO_PIECE 0xFF

#define sq(r,f)   ((r)*8+(f))
#define rank_of(s) ((s)/8)
#define file_of(s) ((s)%8)
#define bit(s)    (1ULL<<(s))

typedef struct {
    BB pieces[2][NPIECES];
    BB by_color[2];
    BB occupied;
    int turn;
    int ep;        /* en passant target square, -1 if none */
    int castling;
    int halfmove, fullmove;
} Pos;

typedef struct {
    uint8_t from, to;
    uint8_t piece;
    uint8_t capture;
    uint8_t promo;
    uint8_t flags;
} Move;

#define MOVE_EP     0x01
#define MOVE_CASTLE 0x02
