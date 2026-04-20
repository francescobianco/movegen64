# movegen64

A chess move generation experiment in pure C that rethinks the board not as an 8×8 position matrix, but as a **64×64 move space**.

## The Core Idea

Traditional chess engines model the board as an 8×8 grid (or a set of 64-bit bitboards) where each cell holds a piece. Move generation then asks: *given a piece at square X, which squares can it reach?*

movegen64 inverts the abstraction. Instead of starting from the board and enumerating destinations, we define a **64×64 move matrix** `M` where:

```
M[from][to] = 1  ←→  a legal move exists from square `from` to square `to`
```

Every cell in `M` is a **move**, not a position. The board has 64 squares, so there are 4096 possible (from, to) pairs — the full move space of chess.

### Square Indexing

Squares are indexed 0–63 in row-major order:

```
a1=0  b1=1  c1=2  ...  h1=7
a2=8  b2=9  ...        h2=15
...
a8=56 b8=57 ...        h8=63
```

A rook on a1 moving to a2 is the point `M[0][8]`. A queen on d4 moving to h8 is `M[27][63]`.

### From Position to Move Matrix

Given a chess position, move generation becomes:

1. For each piece on the board, activate the cells `M[from][*]` that are geometrically reachable by that piece type.
2. Mask out cells where the destination is occupied by a friendly piece.
3. Mask out cells that leave the king in check.
4. The remaining active cells in `M` are the complete set of legal moves.

The result is a sparse 64×64 boolean matrix — or equivalently, a set of active (from, to) pairs.

### Why This Representation?

- **Move-first thinking**: the fundamental unit is the move, not the square. This aligns naturally with how games are played and recorded.
- **Uniform structure**: all piece types, including special moves (castling, en passant, promotion), map to cells in the same matrix.
- **Parallel evaluation**: the full move space can be computed in passes over the matrix rather than per-piece case analysis.
- **Composability**: two consecutive moves are a path through the matrix; a game is a sequence of activated cells.

## Project Goals

- Implement a legal move generator in pure C using the 64×64 model.
- Validate correctness against known perft counts.
- Explore whether this representation offers structural advantages over traditional 0x88 or bitboard approaches.

## Status

Prototype stage. The project currently has:

- a conventional bitboard `Pos` baseline for move generation and validation,
- a byte-matrix `State64` representation,
- copy-based and incremental perft paths,
- regression checks against known perft positions.

The current experiment treats `State64` as a full 64×64 byte matrix. The
diagonal stores square state and global metadata; off-diagonal cells store move
relations:

```
State64.m[s][s]     = piece on square s + metadata bits
State64.m[from][to] = LEGAL | CAPTURE | PROMO | EP | CASTLE
State64' = apply(State64, semantic move)
State64  = unapply(State64', undo token)
```

A selected matrix cell `(from, to)` may still expand to more than one semantic
move, especially for promotion. `State64` therefore exposes cell-to-move
expansion before applying a transition.

## License

MIT
