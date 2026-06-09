# Legality as Comparison: closed-form legal move generation in a transformed 64×64 move space

*Working notes and experimental results — movegen64 project, June 2026.*
*Status: living document. Disconnected notes are kept on purpose; they may become load-bearing later.*

---

## 1. The vision

Traditional move generators compute pseudo-legal moves and *test* legality by
making the move and checking the king (make/unmake). The project's thesis is
that this test is an artifact of working in the wrong space:

> I see the space of legal moves like the zeros of a complex function on the
> plane — but the same zeros become trivial intersections of that function
> once it is viewed through a transform (think Laplace). I imagine a
> homeomorphic, transduced logical space where pin and check are simple
> comparison operations, and moves that would expose the king are
> **annihilated by their interaction with the state** rather than tested one
> by one. — F.B.

Restated: **legality is not a property to verify, it is a geometry to read —
in the right domain.** The work below identifies that domain, builds it, and
measures it.

The board representation follows the project's core idea (see README): the
state is not an 8×8 grid of pieces but a 64×64 **move space**, where every
cell is a (from, to) relation and the diagonal carries square content.

## 2. The mathematical machinery

### 2.1 Sliding = geometric series of the shift operator

Let `D_d` be the one-step shift operator in direction `d` (a banded 0/1
matrix; on a 64-bit word, a constant shift with a wrap mask) and `E` the
diagonal projector onto empty squares. Slider reachability in direction `d`
is the Kleene closure

```
A_d = D_d (E·D_d)*  =  D_d (I + E·D_d)^7
```

a geometric series in the boolean semiring, computable in closed form by
**3 squarings** because a ray is at most 7 steps. On bitboards this *is* the
Kogge-Stone occluded fill: pure shift arithmetic, zero lookup tables, zero
branches. (`src/bb.h`)

Related but distinct closed form: binary subtraction. `o ^ (o - 2r)` computes
a one-directional ray because the borrow chain propagates through empty bits
and stops at the first occupied one — the ALU's carry chain is a free
transitive closure. Note this requires genuine base-2 carries; over GF(2)
(XOR arithmetic, no carry) the blocker semantics vanish. We use Kogge-Stone
throughout, but the subtraction identity is the cleanest statement that
"sliding" is a primitive arithmetic operation, not an algorithm.

### 2.2 The transform: counting semiring, cost layers

Replace the boolean semiring with a saturating counter: the value of a path
is the number of occupied squares strictly between the endpoints. Re-running
the closure from the first blocker yields the next layer:

```
layer 0:  a0 = A_d(s)              cost 0  — slide / direct attack
layer 1:  a1 = A_d(a0 ∩ occ)       cost 1  — x-ray through one blocker
layer 2:  a2 = A_d(a1 ∩ occ)       cost 2  — x-ray through two blockers
```

The **cost matrix** (`src/cost64.{h,c}`) stores, for every collinear pair,
`c[from][to] = (direction << 4) | cost` with cost saturated at 3, `0xFF` for
non-collinear pairs. This is the transformed space. In it:

| board-domain concept (an algorithm) | matrix-domain concept (a comparison) |
|---|---|
| slider attack / mobility           | `cost == 0` |
| slider check on the king           | `c[e][k] == 0` |
| absolute pin                       | `c[e][k] == 1` and the unique blocker is friendly |
| square forbidden to a retreating king | `cost == 1` **with the king as blocker** — the king's shadow is already the x-ray layer; no second "king-removed" fill is needed |
| en-passant discovered check        | cost arithmetic: `c[e][k] − blockers removed + blockers added == 0` |
| betweenness (checkmask)            | same direction, `cost ≤ cost(e→k)`, cells beyond the king cost at least one more |

Two bits of cost per cell fit inside the off-diagonal bytes of `State64`
without changing its layout.

### 2.3 Direct legality, no make/unmake

The deterministic pipeline (`src/legal64.c`, and read-only from the matrix in
`cost64_gen`):

1. one pass from the king as super-slider → checkers, checkmask, pins
   (layers 0 and 1 of the transform);
2. enemy attack map for king escapes (in the matrix: layer 0 plus layer 1
   with king-as-blocker);
3. every move is then a pure intersection: `pseudo ∩ checkmask ∩ pin-line`,
   king moves `∩ ¬danger`; double check → king only; castling = empty path ∩
   safe path; en passant = the cost-arithmetic test above.

Every generated move is legal by construction. Illegal moves are never
enumerated and rejected — they are *annihilated* by mask interaction, as the
vision demanded.

## 3. Experimental results

All numbers from `make sim` / `make check` on the 6 standard perft positions
(startpos, kiwipete, pos3–pos6). Correctness gates, all green:

- `legal64` == (pseudo-legal + make + in-check filter) move sets on ~18,300
  nodes; all known perft counts to depth 5/6 (startpos d6 = 119,060,324,
  kiwipete d5 = 193,690,690).
- `cost64_gen` (matrix reads only) == `legal64` on the same trees.
- **Incrementally carried matrices** (`cost64_apply` chains from the root)
  match a full rebuild byte-for-byte on every maintained row at every node.

### 3.1 Speed of direct legality (no matrix, fills on demand)

| position | depth | base (make+check) | legal64 | speedup |
|---|---|---|---|---|
| startpos | 5 | 108 ms | 36 ms | 3.0× |
| kiwipete | 4 | 97 ms  | 23 ms | 4.2× |
| pos4     | 4 | 10 ms  | 2.2 ms | 4.5× |

### 3.2 Structure of the transformed state (8192 positions)

- cells per layer: cost0 ≈ 887, cost1 ≈ 400, cost2 ≈ 93, cost3+ ≈ 75
  (of 4096); 35.5% of the matrix carries ray signal.
- generation reads **only rows of occupied squares** — verified by
  construction of `cost64_gen`: every read is `c[occupied][*]`.

### 3.3 Incrementality (181k simulated moves)

- rows invalidated per move: **34 / 64 (53%)** — row granularity is an
  illusion; central squares sit on too many rays.
- rays invalidated per move: **39 / 512 (7.7%)** — the correct update unit
  is (row, direction).
- restricted to occupied rows (the maintained subspace): **17.7 rays/move**.
- the dirty set is read from the matrix itself: the columns of the flipped
  squares name exactly the rays to refill (**the matrix indexes its own
  updates**). A plain capture flips only one square, halving its update.

### 3.4 Timing of maintenance

| operation | time |
|---|---|
| full 64×64 rebuild | ~4–10 µs |
| `cost64_apply` (ray-granular, occupied-concentrated) | ~0.9 µs (≈10× less than rebuild) |
| `cost64_gen` (reads) | ~0.2–0.5 µs |
| `legal64_gen` (direct fills, no matrix) | ~0.15–0.23 µs |

Perft with the incrementally maintained matrix runs at the speed of the
copy-make baseline (e.g. startpos d5 ≈ 169 ms vs 171 ms) but still ~3–5×
behind plain `legal64`. **Conclusion: as a pure perft engine the full matrix
does not pay; its value is that attackers, pins, x-rays, mobility and check
structure exist as readable data after every move.** `cost64_apply` still
copies 4 KB per node; an in-place apply with an undo token would remove the
dominant constant.

### 3.5 The topology experiment: can we beat the 8×8 embedding?

Question posed: *the adjacency "e4 next to e5" can be challenged — find a new
topology that performs better.*

What the theory says first:

- Any bijective relabeling of squares is a similarity transform: counts of
  active cells, invalidated rays, centrality of the move graph are
  **invariants**. Renaming cannot move work around.
- The freedom that matters computationally is which adjacencies are
  *constant index strides* (and hence shift-computable). In row-major all 8
  ray directions are already constant strides {±1, ±7, ±8, ±9} — the
  theoretical optimum for line directions. The bijections preserving this
  property form essentially the D4 symmetry group: no headroom there.

What the experiment adds (centrality load map, §`make sim`): the load a
square puts on maintenance when it flips varies only between ~8.2 and ~11.4
(1.4×) and **follows piece density, not board geometry** (peaks on the pawn
ranks in our samples). A static deformation chases a target that moves with
the position. Therefore:

> **The performing topology is dynamic.** The deformation that works is not a
> remapping of square labels but a re-weighting of *maintenance effort* onto
> the active subspace: keep rows of occupied squares exact, let rows of empty
> squares decay (rebuild on demand when a piece lands). Implemented in
> `cost64_apply` via the `valid` row mask; measured: 17.7 vs 39.3 maintained
> rays per move (2.2×), apply ≈10× cheaper than rebuild.

The genuinely different static topology worth exploring next is **line
space**: a square is the intersection of its 4 lines (rank, file, diagonal,
antidiagonal); the position is 8+8+15+15 = 46 line-occupancy words of ≤8
bits. In line space "e4 ~ e5" is not an axiom: two squares are close iff they
share a line. The decisive property: **a square flip touches exactly 4
lines — constant update cost by construction** (vs. avg 17.7 ray refills in
square space), and per-line attacks are 8-bit carry/subtraction tricks. The
64×64 cost matrix factors into 4 line-local blocks. This is the most concrete
candidate for "a topology that performs better."

### 3.6 Row/column duality: the matrix as universal two-sided state

Proposal: *exploit the row/column and white/black dualities — white's moves
read by rows, black's by columns, both extracted in parallel from one state.*

The mathematical basis is exact: **cost is symmetric**. The number of
blockers strictly between `a` and `b` does not depend on travel direction;
only the direction nibble flips (`c[a][b]` and `c[b][a]` are transposes with
`dir ↔ opposite`). Consequences, implemented and measured:

- **Build halved.** A full build needs only the 4 positive directions,
  writing each cell and its transpose together (`build_ray_sym`). Measured:
  full-rebuild perft dropped ~2.4× (startpos d5: 1166 → 478 ms); build
  ≈ 3.5 µs.
- **Both colours in one pass.** `cost64_gen_both` extracts white's and
  black's legal moves from the same matrix (the off-turn side "as if it were
  to move"); validated against legal64 on the turn-flipped position at every
  node. The matrix is genuinely colour-blind universal state: checks, pins
  and mobility of *both* kings coexist in it at all times. Raw speed honesty:
  1.16 µs vs 0.50 µs for two legal64 passes — the value is qualitative
  (both perspectives always live, no state reconstruction) plus the
  maintained-matrix context, not raw generation speed.
- **A trade-off discovered.** Transpose-symmetric *refills* (each unordered
  ray segment updated once instead of twice) conflict with the
  occupied-subspace concentration of §3.5: cells pointing from an occupied
  row toward an *empty* square can only be repaired by that row's own
  refill, because the empty partner row is not maintained. Full symmetry
  halving applies to full rebuilds; the concentrated delta path keeps
  row-wise refills. The two symmetries — transpose duality and dynamic
  concentration — trade against each other.
- **Pawn triangularity** (noted, unexplored): white pawn relations live
  strictly in the upper triangle (`to > from`), black pawns strictly in the
  lower — the one piece for which "white by rows, black by columns" is
  literally the storage layout. Colour swap overall is the 180° rotation
  `s ↦ 63−s` of both indices, move reversal is the transpose; composing the
  two is the full symmetry group of the move space.

### 3.7 The autofeedback model (vision, partially realized)

Proposal: *an envelope — feed row output into column input; a self-feedback
model where the matrix flushes the game state forward with no external
memory and no conditional rules, only transport operations.*

How close the current system is:

- **Row→column feedback exists and is load-bearing**: after a move flips
  square s, the *columns* of s (read) name exactly the rays (rows) to
  refill (write) — the matrix already schedules its own next state.
- **The closure is a relaxation to fixed point**: `(I + E·D)^7` is
  idempotent once converged; a move is a perturbation of the diagonal
  (piece transport = a permutation) followed by re-convergence in exactly
  3 squarings. Perturb → relax → read. No search, no cases.
- **"No conditional rules" is the SIMD endgame**: every operation in the
  pipeline (fills, masks, projections) is branchless shift/and/or algebra;
  the only conditionals left are the *selection* of dirty rays. The
  transport-only model would refill all 512 rays unconditionally as 8
  registers × 4 directions × 3 squarings of AVX-512 data movement —
  trading selection (control flow) for bandwidth (transport). At ~3.5 µs
  scalar for the symmetric build, a vectorized unconditional relax step is
  plausibly cheaper than deciding what to skip.
- **What still lives outside the matrix**: piece types and game metadata
  (we read `Pos`). `State64`'s diagonal is designed to hold exactly this;
  folding the cost layers into `State64`'s off-diagonal bytes closes the
  loop — the matrix becomes the whole machine: input = a selected cell,
  transition = transport + relax, output = the next matrix. No external
  memory, as envisioned.

**Experimental verdict (implemented: `src/relax64.{h,c}`).** The
transport-only step exists and was measured. `relax64_step(occ)` recomputes
all three cost layers for all 64 rows as bit-planes (rows are bitboards
again, the byte-scatter disappears) with one identical instruction stream —
no dirty tracking, no copy, no history: a memoryless function `M = F(occ)`.
Verified cell-by-cell against the byte matrix on 256 positions. Timings on
the same machine, same run:

| step                                               | scalar -O2 | vectorized -O3 native |
|----------------------------------------------------|------------|-----------------------|
| `relax64_step` (all 512 rays, unconditional)       | 2.3 µs     | **0.69 µs**           |
| `cost64_apply` (selective delta + 4 KB copy)       | 1.6 µs     | 0.59 µs               |
| `cost64_build` (selective, symmetric, byte output) | 5.7 µs     | 2.6 µs                |

Vectorization buys 3.4× on the transport step (the loops are 64 identical
word operations — exactly what SIMD wants), and the unconditional recompute
lands **even with** the carefully engineered selective delta, while needing
no previous state at all. Conclusion: once transport is vectorized,
*selection buys almost nothing* — the autofeedback intuition is
quantitatively confirmed. On AVX-512 (8 rows per register) the gap should
invert further.

**The plane-based generator (`relax64_gen`) — reading beats computing.**
Generation rewritten against the bit-planes: a slider's reach is one row
ANDed with its arithmetic line mask; the pinned piece is literally
`L0[king] & L0[pinner]` (one AND); the checkmask is `L0[king] & L0[checker]`
on their line; the king's forbidden retreat squares are `L1[attacker]`
beyond the king. Line masks are computed constants (shift arithmetic on the
two diagonal words), so the no-tables invariant holds. Validated identical
to legal64 at every node of the test trees. Measured:

| generator | ns/position |
|---|---|
| `relax64_gen` (plane reads)      | **129–141** |
| `legal64_gen` (direct fills)     | 216–229     |
| `cost64_gen` (byte-matrix scans) | 329–450     |

Once the transformed state is maintained, *reading* legality out of it is
~1.7× faster than recomputing it with the best direct method — the first
hard evidence that the transformed space is not just elegant but cheaper,
provided the transform itself is amortized. The full memoryless pipeline
(relax + plane-gen at every node, whole 64×64 state alive throughout) now
beats the copy-make baseline on tactical positions (kiwipete d4: 80 vs
100 ms; pos6: 74 vs 94 ms) and ties or beats the selective-delta path,
while legal64 (which keeps no matrix) remains ~2–4× ahead overall. The
remaining gap is exactly one relax step per node — i.e. pure transport
bandwidth, the quantity SIMD width directly buys.

### 3.8 The machine: input is the output of the columns

Clarified project thesis (F.B.): detaching from known art is not about
originality for its own sake — the idea to explore is that **we have a
machine whose input is the output of the columns**. Built and validated
(`src/machine64.{h,c}`):

- **Persistent state: the diagonal alone — 64 bytes.** Pieces are 4 bits
  per cell; game metadata travels as transport bits *on* the diagonal:
  castling rights are "virgin" bits living on the king/rook home cells,
  the en-passant target is a one-step **ghost bit** on the skipped square
  (set by a double push, expires at the next step), the side to move is
  one bit of cell 0. No `Pos`, no external memory between steps.
- **One step = R → C → A → T**:
  `R` (emit) — rows broadcast reach from the diagonal (memoryless relax);
  `C` (collect) — columns gather at the kings: checkers, pinners, danger;
  `A` (annihilate) — row output ∧ column feedback kills illegal cells;
  `T` (transport) — the selected cell moves one byte on the diagonal,
  flag bits travel and expire.
  The input of each step is a cell of the legal set the columns just
  produced — the loop the vision asked for.
- **Validated closed.** All six perft counts exact with every node
  regenerated from the diagonal alone (startpos d5 = 4,865,609 etc.);
  200 random self-play games ran entirely inside the machine — 56,058
  plies, every position cross-checked against legal64, terminating in
  genuine checkmates (26) and stalemates (5), the rest hitting the
  300-ply cap. A complete chess game flows through 64 bytes plus pure
  transport.

**The cost of legality in the machine** (measured, 4096 positions,
-O3 -march=native; scalar -O2 in parentheses):

| phase                                      | ns              |
|--------------------------------------------|-----------------|
| R — relax, the whole field, 512 rays       | 1196 (2322)     |
| C+A — reading the legal set off the field  | 208 (235)       |
| diagonal → transient register              | 126 (136)       |
| **full step**                              | **1621 (2772)** |
| reference: legal64, no field               | 286 (329)       |

Three readings. (1) Per legal move: ~39 ns vs ~7 ns for legal64 (~42 legal
moves/position) — but the machine buys the *entire* transformed field for
both colours, and legality itself is then a 208 ns read, cheaper than
legal64. The field is the product; the move list is a projection of it.
(2) The cost is **constant, not average**: no conditionals, identical
instruction stream in any position — fixed latency, the property that maps
to hardware pipelines (FPGA/tensor units), where branchy average-case
cleverness does not. (3) The cost is pure bandwidth (~20k identical word
ops): AVX2 4 lanes → 1.2 µs, AVX-512 8 lanes → ~0.6 µs projected, GPU →
a few micro-kernels. Selection does not scale with vector width; transport
does. The 5.7× gap is today's price of unconditionality, shrinking with
every hardware generation.

### 3.9 Honest positioning: is this disguised bitboard/attack-table work?

Asked directly, answered directly: **largely yes, at the substrate level.**

- `bb_occl_fill` is textbook Kogge-Stone (Westcott, early 2000s).
- The legal64 pipeline (checkmask, pin lines, king-removed danger map) is
  how modern fully-legal generators work; only the zero-tables constraint
  is unusual.
- The matrix columns ("who attacks this square") are Ed Schröder's
  incrementally-maintained attack tables (Rebel) rediscovered.
- Pin-as-x-ray is on the chessprogramming wiki verbatim.
- Most telling: every time something here got fast, it got fast by drifting
  *toward* bitboards (bytes → bit-planes, scans → ANDs). The 64-bit word
  *is* a board row; on this hardware every 8×8 representation falls into
  that gravity well. That is physics, not failure.

What survives the audit as genuinely distinctive:

1. **The unifying formalization.** The state of the art is a collection of
   separate tricks; here check, pin, e.p. legality and the king's shadow
   all fall out of *one* formula — the staged closure `D(E·D)*` in a
   counting semiring. Components known, systematization not (to our
   knowledge). The difference between a list of trig identities and the
   complex exponential.
2. **The measured negative results** are research output, not
   reformulation: row granularity is useless (53%); static topology has no
   headroom (1.4×, follows piece density); unconditional recompute ties
   selective delta once transport is vectorized — this last one
   contradicts standard engine wisdom ("incremental is sacred"), which was
   formed before SIMD bandwidth existed.
3. **The product differs even where the substrate doesn't.** Rebel
   maintained attack tables to generate moves; here the matrix is a
   queryable state where pins, x-rays, batteries and pressure are readable
   data — for evaluation, NN input, column queries. Same hardware,
   different artifact.

Escape routes from the bitboard gravity well, if we want one: line-space
topology (the representation genuinely changes there) and weighted-semiring
evaluation — the two fronts attack tables never covered.

## 4. Disconnected notes (kept on purpose)

- **Column reads are attack queries.** `M[*][t]` = who attacks/defends t.
  Defender counts, SEE-like exchange info and king-zone pressure are column
  sums — already maintained by the same updates.
- **M² in the boolean semiring** = squares reachable in two consecutive
  moves; a game is a path through activated cells (README's composability
  goal). Never explored quantitatively.
- **Weighted semirings for evaluation**: give squares weights (centrality,
  king-zone) and run the same closure in a (min,+) or (sum) semiring → the
  matrix cells become *positional* quantities (weighted mobility, pressure)
  instead of legality bits. Deforming the metric, not the labels.
- **SIMD full-matrix fill**: 64 rows = 64 bitboards; AVX-512 holds 8 rows per
  register → the whole cost-0 layer for all squares in 8 registers × 8
  directions × 3 squaring steps. The build cost could drop near the
  per-piece cost. Unexplored.
- **In-place apply + undo token**: the 4 KB copy dominates `cost64_apply`.
  An undo log of (cell, old byte) pairs — bounded by ~39 rays × ~7 cells —
  would make the matrix a true make/unmake-free *persistent* state.
- **State64 integration**: 2 cost bits + 3 direction bits fit the existing
  off-diagonal byte scheme (`LEGAL|CAPTURE|...` flags can coexist or be
  derived). The diagonal keeps piece + metadata. Then `State64` *is* the
  transformed state and `apply/unapply` maintain it.
- **Rank attacks by multiplication**: byte-reversal via
  `((b * 0x0202020202ULL) & 0x010884422010ULL) % 1023` gives the
  west-direction subtraction trick without tables, if the hyperbola route is
  ever preferred over Kogge-Stone.
- **Cache layout is a non-issue at this size**: the whole matrix is 4 KB
  (one row = one cache line = one bitboard-sized byte row). Hilbert/Z-order
  remappings solve a problem we don't have.
- **Saturation pitfall (found the hard way)**: cost must distinguish 2 from
  ≥3, because the en-passant test needs exact 2, and a cost-3 cell can decay
  to 2 when two pawns leave a rank. Saturate at 3, never at 2.
- **Capture asymmetry**: a capture flips one square, a quiet move flips two,
  castling four, en passant three. Update cost follows tactical density —
  quiet maneuvering is the expensive regime for maintenance, not tactics.
- **The matrix indexes its own updates**: dirty rays after flipping square s
  are read from column `c[*][s]` (the direction nibble names the ray). No
  scheduling structure needed beyond the state itself — closest concrete
  realization of "lo stato si autoalimenta".
- **NN/eval angle**: the 64×64 byte matrix is an image where tactical
  structure (pins, x-rays, batteries, overload) is explicit rather than
  latent. As network input it removes the need for the net to rediscover ray
  geometry. Untested.
- **The attention parallel**: the machine's step — rows emit, columns
  collect, a 64×64 interaction matrix gates the update — is structurally
  the self-attention pattern (queries × keys → attention matrix → value
  transport), with exact ray algebra in place of learned weights. Chess as
  a fixed-weight attention layer over 64 tokens; possibly the cleanest
  bridge between this representation and learned models.
- **Ghost bit / virgin bit pattern**: ephemeral rule state (e.p. rights,
  castling rights) can live *on the board cells themselves* as transport
  bits that travel and expire, instead of in side variables. This made the
  64-byte closed loop possible and generalizes (any "right" is a bit that
  decays under transport).

## 5. Reproduction

```
make check     # full gate: baseline perft, State64 roundtrip,
               # legal64 vs baseline, incremental cost64 vs rebuild vs legal64
make sim       # the full analysis: structure, incrementality,
               # centrality load map, timings, perft comparison
```

Code map: `src/bb.h` (shift/fill primitives) · `src/legal64.{h,c}` (direct
legal generation) · `src/cost64.{h,c}` (cost matrix: symmetric build,
read-only generation for one or both colours, incremental apply) ·
`src/relax64.{h,c}` (transport-only memoryless relax step, bit-plane
layers, plane-based generator) · `src/machine64.{h,c}` (the closed-loop
machine: 64-byte diagonal state, R→C→A→T step) · `tests/legal64_compare.c`,
`tests/cost64_sim.c`, `tests/machine64_test.c` (validation + simulations +
self-play inside the machine).

## 6. Open front

1. ~~Plane-based generator~~ — done (`relax64_gen`, §3.7): 129 ns, faster
   than legal64; reading the transform beats recomputing.
2. Line-space topology (46 line words, constant-4 updates).
3. ~~The closed-loop machine~~ — done (`machine64`, §3.8): persistent state
   is the 64-byte diagonal; `Pos` survives only as a transient register
   inside one step. Remaining refinement: make the C/A phases consume the
   diagonal directly and retire the register entirely.
4. Weighted-semiring evaluation signals from the same closure.
5. Triangle (half-matrix) storage exploiting cost symmetry: 2 KB state,
   pawn moves naturally split by triangle.
6. ~~SIMD unconditional relax step~~ — done (`src/relax64.{h,c}`, §3.7).
7. ~~In-place apply with undo token~~ — superseded: the memoryless relax
   makes the copy/undo question moot.
