# Board generation: what we have, what others do, what to build

Investigation 2026-09-23. Companion to `plans/synergy-objectives.md`.
Research clones (bingosync, oot-bingo-generator, lockout-fabric) were read
from source; URLs inline.

## 1. The current generator (`src/game/bingo_board_setup.c`)

Template (E/H = the two diagonal classes; a coin flip decides which is easy
and which is hard):

```
E M M M H
M H M E M
M M C M M
M E M H M
H M M M E
```

Then: 0–2 random cells get mutated (`switch_to`; the loop re-rolls its bound
every iteration, so P(0/1/2) = 1/3, 4/9, 2/9); cells are filled in shuffled
order by a weighted draw from the class table with per-type budgets; a
dedup pass runs per line (1-line mode) or across the whole board (2/3 lines,
blackout, lockout).

**What's good about it (Matt's point):** it's a crude magic square. Most
lines have the same *shape* (1 easy + 3 medium + 1 hard), while the
diagonals trade medium for extreme (2 easy + 2 hard + center). So it does
make you choose between lines with different mixes.

**Measured on 1998 real boards** (host harness `BOARD_SEED=n`, seeds 1–2000):

| Metric | Value |
|---|---|
| Line mix 1E/3M/1H | 56% of lines |
| Line mix 2E/0M/2H + center (diagonals) | 15% |
| Line mix 0E/4M + center (middle row/col) | 14% |
| Tiles tied to one course | 9.9 of 25 (rest are global counters) |
| Distinct courses among those | 7.8 |
| Lines with 2+ *course-pinned* tiles in the same course | 7% |
| Lines with a **strong** pair (both tiles finishable in one course, using enemy/object supply) | 17.8% |
| Lines with a **weak** pair (both can progress in one course) | 80.3% |
| Lines containing a wildcard (N stars/coins total, lives, deaths, BLJ…) | 66.6% |
| Lines with no overlap of any kind | 2.8% |
| Boards with ≥4 copies of one objective type | 14% (e.g. unlimited STAR) |

**Weaknesses:**
1. **Difficulty = class, not time.** The N in "kill N goombas" is picked by
   class in `bingo_objective_init.c`, and times vary a lot inside a class.
   Line balance is only as good as that coarse bucket.
2. **Overlap is common but uncontrolled.** (Matt caught that the first
   count, 7%, ignored enemies and other global counters.) With supply
   attribution, 18% of lines have a strong pair and almost all have weak
   overlap or a wildcard. But the generator neither guarantees nor caps it:
   it's incidental, so some lines are routes and others are errands.
3. **Predictable structure.** The middle row/column never holds an easy or
   hard tile; the diagonals always hold 2 of each. Players can learn it.
4. **Same algorithm for every mode.** Lockout and blackout inherit a
   line-balancing template although lines don't matter there.
5. Small quirks: `switch_to(2)` has `otherTwo = 2`, so 40% of its mutations
   are no-ops (probably meant 0); the weight-budget leak
   (bingo64-known-bugs); and the host harness's `dump_cell` prints the A-button
   `hint` for B/Z challenges too, reading stale union bytes. It segfaults on
   seeds 1302 and 1320 (test bug only: the game reads `hint` for A only).

## 2. What other generators do

- **SM64 on bingosync (the SRL list)** — `bingosync-app/generators/
  super_mario_64_generator.js` on `generator_bases/srl_generator_v5.js`.
  Magic square: every cell gets difficulty 1..25, every line sums to 65.
  Cells are filled in raster order from that tier (3–5 goals per tier, 105
  goals). Synergy = shared `types` tags (mostly course names plus themes like
  `cannons`, `secrets`, `hundredcoin`, `wingcap`) with the cells already in
  the same lines; it takes the first zero-synergy goal, else the minimum.
  No retries. Blackout variant: synergy is checked against all 24 cells.
  **Lockout on bingosync does not change generation.**
  - Goal-list facts for an SRL mimic: **MIPS is not an SRL goal** (only the
    rando-lockout list has "Both Mips Stars"). Toad appears once: "Top Floor
    Toad Star" (tier 23). No key, cap-switch, star-door or Bowser goals.
    Progression-ish goals: the cannon ladder "Open 3/5/7/9/All 11 cannons"
    (tiers 3/10/14/19/25), "Reach the Castle Roof", "Lose Mario's Hat".
- **OoT bingo** (ootbingo/oot-bingo-generator): the same magic square, but
  each goal has a real **time in minutes** (plus a skill bonus). Cell target
  = difficulty × 0.75 min; candidates within ±1..2 min; weighted shuffle
  favors rarely generated goals. Each line's synergy (minutes saved by doing
  its goals together, with per-category filters) must stay in [−3, 7], and
  no single synergy may exceed 3.75. **A dead end throws the whole board away
  and restarts** (≈100 tries). Blackout adds a pairwise conflict check across
  the board. No lockout profile.
- **Lockout-first generators ignore position entirely** and balance the
  *set*:
  - Hollow Knight (SHO): per-type caps (max 1 "tiebreaker" goal), exclusion
    pairs, a board variance budget (goal scores 0.2–1.3, max 9.5).
  - Minecraft Lockout (lockout-fabric): shuffle + 45 group caps (e.g.
    OPPONENT_GOALS ≤ 3), world-feasibility checks, and a **ladder rule**: a
    top rung ("kill 15 unique hostiles") only appears if a lower rung of the
    same group is on the board.
  - Lockout.Live: category caps per board *and* per line, then MRV
    backtracking, then least-violations relaxation.

## 3. Proposal: one engine, one objective function per mode

The modes differ in which rearrangements of a board leave the game
unchanged, and each generator should balance only what the mode can tell
apart.

**Shared engine.**
- Each objective instance gets a **time estimate** `t` (frames) and **tags**
  (course(s), theme: cannons/caps/reds/kills…).
- Priors come from class + N scaling.
- Then refine with **pairwise votes + a supply model** (§5); relay logs are only a later check.
- Search: seeded MT19937 as today, integer math, draw-and-check with
  whole-board restart like OoT. 25 cells × a few hundred tries is trivial even
  on N64.

**Line modes (1/2/3 lines).** Lines matter. The eight rotations and
reflections of the square are the only symmetries.
- Replace the class template with a **real magic square over time targets**
  (SRL/OoT, seeded). This keeps Matt's "different mixes, equal totals"
  property but makes it exact, and it removes the predictable
  middle-row/diagonal structure.
- Add a **per-line synergy band** on the tags. Unlike SRL/OoT, which only cap
  synergy, we also want a *floor*: e.g. every line has at least one same-course
  or same-theme pair, and none has more than two. Today ~18% of lines have a
  strong pair, by accident. That dial is the "oomph" knob. Synergy should shrink the line's time budget
  (OoT does this), so lines stay fair.

**Lockout.** Every one of the 25 cells is interchangeable, and so are the
players: only the *set* matters.
- Generate a multiset:
  - total time band;
  - category caps (Minecraft/HK style);
  - at most one very long tile.
- **Snowball cap:** the race is decided by the ~13 fastest tiles. No course
  may hold more than k of them, or whoever gets there first wins the game.
- **Contention dial:** how many tiles share courses. More sharing means players
  meet more often (interaction); less means parallel solitaire.
- Layout is free: random placement.
- Live tiles (synergy plan §1) slot in here.

**Blackout (co-op).** All cells are interchangeable, and everyone needs all 25.
- Total time band.
- **Cap the longest tile.** When a team splits up, the longest tile decides
  the finish time.
- OoT-style pairwise conflict check.
- Course clustering is *good* here: it lets a team divide work by course.

**Call and Response.** A sequence, not a board (§4).

Board generation for online and offline already forks for live tiles
(synergy plan). With per-mode generators the input is simply
`(seed, mode, unlock, mask, online)`. Every one of these changes board
generation → protocol bump and goldens re-bless. Offline line modes could keep
the old generator behind a version switch if golden stability matters.

## 4. Call and Response

**Closest documented format:** Lockout.Live **Rush**
(wiki.lockout.live/lockout/game-modes/rush/). All players see the same 3
goals; completing any one deals a fresh set of 3; you can't skip twice in a
row. 90 goals in 50 sets; sets 31–50 recycle skipped goals; timer-bound.
No documented "one goal at a time, first to N" format was found.

**Proposal: an open window of K calls.**
- K=1 is pure call-and-response.
- **K=2 is Matt's "pairs".**
- K=25 with no refill is just lockout.

Recommended variant: **K=2, sliding.** Claiming a call replaces only that
slot; the other call stays up.
- Every call has a real choice (which of two?).
- A stale call becomes a catch-up opportunity for the player who was
  elsewhere.
- Stalls mostly vanish (you can take the other call), so the per-call void
  timer becomes optional.

**Generator = a sequence (queue) derived from the seed.** No new wire data;
the lazy-init and adjudication notes in the bingo-mode research memory still
apply.
- **Pair shape:** deal each pair as one fast + one slow objective, so taking
  the easy one has a cost.
- **Positional fairness:** the winner of a call is standing where it happened.
  A new call must not be in that call's course (and ideally not in the other
  open call's course either).
- **Ramp:** early calls short, later calls longer. Cap any single call's time
  estimate.
- **Win:** first to N (Matt floated 5), or a timer with a count.

## 5. Cost model: time and synergy without wall-clock data

Matt (2026-09-23): the relay has only a few matches, and "how long do 133
coins take" can't be measured. Also, tiles are never done alone: if you
already have 7 goombas for another tile, the 8th is one second. So the unit of
cost is a **set of tiles**, and synergy = sum of standalone costs − cost of
the set (OoT's definition: time saved by doing them together).

### The model
Each tile becomes **demands** instead of a cost:
- *visit* demands: "be in THI and do star 3" (pinned tiles);
- *counter* demands: "have 8 goomba kills", met from the per-course supply
  tables (`test/board_gen_bias.py` SUPPLY);
- *wildcards* ("N stars total", lives, deaths) are counters fed by the other
  tiles' work.

cost(set) = the cheapest plan that meets every demand:
- **V[course]**: cost of a visit (entry + getting around), paid once and shared;
- **s[course][star]**: work for a specific star once there;
- **u[type]**: cost per unit (the next goomba once you're in THI);
- **f[type][course]**: units you get free per visit (goombas killed on the way
  anyway);
- counters take the max across tiles, not the sum (7 and 8 goombas → 8).

Solver: a line is ≤5 tiles over ≤~8 candidate courses, so enumerate course
subsets (≤256), fill counters freebies-first then cheapest paid units, and take
the minimum. Tiny, deterministic, and portable to C (or precomputed).

Known blind spot: movement inside a course isn't modelled (a THI visit is
one number). The same level of detail as SRL/OoT.

### Stages (each ends with a Matt decision)
1. **Demand schema + solver in Python** (`test/cost_model.py`). Reads boards
   from the host harness (`BOARD_SEED=n`). Hand-set priors for V, s, u, f
   (visit ≈ 1 min, freebies ≈ 30% of supply, classes as a rough guide for
   stars).
   → *Decision: show Matt ~10 real lines with their computed cost and synergy.
   Does it match his gut? Adjust priors until it roughly does.*
2. **Measure today's generator** with the model: spread of line costs within a
   board, synergy per line, which lines are always the fastest pick.
   → *Decision: is today's balance actually bad, and where?*
3. **Voting page** to fit the knobs: standalone pairs ("which is longer?") plus
   a few in-context pairs ("you're in THI for star 3: which adds more?").
   Fit V/u/f by Bradley–Terry / least squares. Parameters live in a JSON file
   in the repo, and a script turns it into a C table.
   → *Decision: accept the fitted numbers (review any surprises).*
4. **Prototype generators in Python** against the model: line modes (magic
   square on line cost + synergy band), lockout (set balance, snowball cap,
   contention dial), blackout (longest-tile cap), call and response (sequence).
   Compare against today's generator on the stage-2 metrics.
   → *Decision: pick the algorithms and dial settings.*
5. **Port to C**, goldens re-blessed, protocol bump (bundle with live tiles
   etc.). Only this stage touches the game.

Stages 1–2 are pure analysis, needing no game changes and no votes, and
answer "is it worth it" cheaply.

## 5b. Relay logs (later, as a check)

`server/relay.py` MatchLog writes JSONL per month:
- `start` with `seed, mode, unlock, mask`;
- every `claim` with `cell` and `frames`.

The host harness regenerates any board from those. So from the relay logs we
can compute **per-objective real completion times** (per player: gaps between
consecutive claims, fitted per type + N + course across many matches).

Needed:
- log the protocol/build version in `start` (the generator changes between
  protocols);
- Matt pulls the logs from the VM (this machine has no ssh/gcloud);
- write a small fit script.

This turns the time estimates from guesses into measurements, for every mode.

## 6. SRL mimic (Matt's idea)

A faithful mode = port `super_mario_64_generator.js` + SRL v5 as-is (goal
list, tiers, types, raster fill, synergy rule), selected as its own preset.
It needs a goal-by-goal coverage audit against our objective roster (100-coin
star counts, "Star #1 from each stage", mini-bosses, the cannon ladder, Toad
top-floor…) to see which goals need new detectors. Progression tiles (synergy
plan §2) are part of this list.
