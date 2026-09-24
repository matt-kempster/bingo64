# Cost model + v2 generator: first results

2026-09-24. Implements stages 1, 2 and 4 of `plans/board-generation.md` §5
(Python only, in `test/cost_model/`; see its README). There are no votes yet,
so **every time below comes from hand priors** (`params.json`, each one marked
as a guess). Treat the absolute minutes as rough. The comparisons (master vs
v2, line vs line) hold up better than the absolute numbers, since both sides
use the same guesses.

## How the model works (short)

- Each tile becomes demands: pinned stars, course-local work (coins in one
  entry, 1-ups, splatoon, purple stars), star wildcards, global counters with
  per-course supply, and other wildcards (BLJ, wall kicks, coins total,
  lives).
- A plan pays **V** once per course it visits. Star work comes from
  `starTimes` × 1.3. A second star in the same course pays a 12 s re-entry,
  because stars kick you out.
- Counters take free units first (30% of a course's supply, capped), then the
  cheapest paid units. Same-type tiles take the max. Deaths drain lives.
- The exact solver tries every subset of up to 10 candidate courses. The fast
  solver (the one to port to C) is ≤ 4 fixed greedy passes. Over 1,200 real
  lines the fast solver is on average **1.9% over** exact (p90 6%, max 20%).
  A pure one-pass solver is 3.4% over (p90 10%).
- Synergy = Σ standalone − set cost.
- Blind spot: movement inside a course is one number per course.

## 1. Ten real lines (master dumps) for a gut check

Standalone times are in parentheses. "Exact" is the cheapest joint plan.
Full plans are in `measure.py --examples`.

| Pick | Line | Tiles (standalone) | Σ alone | Exact | Synergy |
|---|---|---|---|---|---|
| largest synergy | seed 90 row0 | BLJ:4 (3:19), STARS_MULTIPLE_LEVELS:1x15 (19:45), WALL_KICKS:5x4 (5:51), BLUE_COIN:20 (4:08), MULTICOIN:321 (7:19) | 40:22 | 24:41 | 15:41 |
| deaths vs lives | seed 198 col2 | LIVES:9 (1:32), STARS_IN_LEVEL:DDD:4 (5:14), BOWSER:3 (4:05), UNIQUE_DEATHS:4 (2:55), RANDOM_RED_COINS:WF (1:58) | 15:44 | 16:02 | **−0:18** |
| cheapest | seed 43 row1 | ! BOX:7 (1:45), COIN:WF:38 (0:49), 1UPS:HMC:3 (1:45), STAR:WF:4 (1:27), KILL_WHOMPS:2 (1:17) | 7:04 | 5:39 | 1:24 |
| most expensive | seed 153 row3 | SHOOT_CANNONS:9 (5:27), KILL_CHUCKYAS:5 (11:29), Z-BUTTON TTM 2 (2:41), STARS_IN_LEVEL:DDD:7 (12:13), DAREDEVIL THI 5 (2:21) | 34:12 | 31:48 | 2:24 |
| median, no synergy | seed 275 col0 | COIN:CotMC:38 (0:59), BOWSER:1 (1:55), TTC_RANDOM TTC 3 (1:52), KILL_MR_IS:4 (2:15), STARS_IN_LEVEL:WF:7 (6:22) | 13:24 | 13:24 | 0:00 |
| median, some synergy | seed 40 row4 | COIN:TTC:58 (1:39), TTC_RANDOM TTC 3 (1:52), COIN:HMC:56 (1:25), RACING_STARS (8:21), STAR:WDW:2 (1:33) | 14:51 | 13:28 | 1:23 |
| star wildcard fed | seed 279 col2 | BLUE_COIN:18 (3:44), DAREDEVIL HMC 1 (1:41), STARS_MULTIPLE_LEVELS:1x15 (19:45), RACING_STARS (8:21), TTC_RANDOM TTC 1 (1:52) | 35:23 | 25:10 | 10:13 |
| counters share courses | seed 159 row2 | RED_COIN:17 (3:18), SECRETS_STARS (11:37), AMPS:15 (7:18), SIGNPOST:11 (1:41), KILL_FLY_GUYS:5 (3:16) | 27:10 | 19:41 | 7:29 |
| middle column | seed 131 col2 | KILL_MR_IS:5 (3:17), AMPS:8 (3:30), MULTICOIN:344 (7:52), 1UPS:TTC:3 (1:58), CRUSHED:5 (2:57) | 19:34 | 13:27 | 6:06 |
| diagonal | seed 7 diag\ | SPLATOON:RR:17% (1:55), MULTISTAR:8 (5:35), KILL_BOBOMBS:19 (5:15), DAREDEVIL WDW 100c (3:44), STAR:WDW:6 (2:25) | 18:55 | 13:28 | 5:27 |

Things to check against your gut:

- **1x15 "stars in 15 courses" at ~20 min** is the dominant tile of any line
  it is in.
- **KILL_CHUCKYAS:5 at 11:29** needs all 5 Chuckyas (one each in WDW, TTM,
  THI, RR, BitS).
- **LIVES:9 at 1:32** is probably too cheap: it assumes free 1-ups while
  passing through LLL.
- **BLJ in 1 course = 45 s** (PSS).
- In the deaths-vs-lives line, the 4 deaths add 4 lives to the LIVES target.
  The line costs more together than its tiles do alone (synergy −0:18).

## 2. Stage 2: today's generator, measured (1,998 boards, 23,976 lines)

| Metric | Master |
|---|---|
| Line cost, median (p10–p90) | 13:31 (9:23–20:15) |
| Most/least expensive line in a board, median (p90) | **2.38× (3.14×)** |
| Line-cost coefficient of variation, median | 0.25 |
| Synergy per line, mean / median | 3:27 / 3:04; 88% of lines ≥ 1 min |
| Lines with 0 / 1 / 2 / 3+ strong pairs (≥ 45 s saved) | 14% / 20% / 16% / **50%** |
| Lines within 10% of the cheapest, mean | 1.72 (53% of boards have a single clear best) |
| Cheapest line is a diagonal | **7.2%** (fair share: 16.7%) |
| Cheapest line is the middle row/col | 16.3% (fair: 16.7%) |
| Boards with ≥ 4 copies of one type | 14.4% |

Standalone time by master class (median, p10–p90):

| Class | Median | p10–p90 |
|---|---|---|
| EASY | 2:45 | 1:13–6:29 |
| MEDIUM | 2:33 | 1:24–5:14 |
| HARD | 3:32 | 1:48–7:23 |
| CENTER | 5:15 | 1:52–12:28 |

Findings:

1. **Balance is poor.** In a typical board the most expensive line costs
   2.4× the cheapest. About half of boards have one clearly cheapest line, so
   the "choose between different mixes" property is mostly lost to noise.
2. **The class barely predicts time.** EASY tiles take about as long as
   MEDIUM ones (their median is actually higher). The cause is the N ranges
   and type mix: EASY holds STARS_MULTIPLE_LEVELS, MULTISTAR, UNIQUE_DEATHS
   and LOSE_MARIO_HAT at 3–4 ways, which are not easy. That is weakness #1 of
   the plan, now in numbers.
3. **Diagonals are rarely the pick** (7% vs 17%). They hold 2 HARD tiles plus
   CENTER, and the swap of "3 medium" for "easy + hard" does not even out in
   time. The middle row/col, which never holds E or H, gets its fair share.
4. **Synergy is common and uncontrolled.** Under this model almost every line
   saves minutes:
   - Wildcards (BLJ, wall kicks, coins total, stars total) ride on any course
     you visit anyway.
   - Two flexible tiles share a V.
   - This contradicts the earlier estimate that only 18% of lines have a
     strong pair. That estimate only counted same-course pinned or supply
     pairs; this model also counts shared visits and wildcard credit. What
     varies is *how much* synergy a line has (p90 6:35 vs median 3:04), and
     the generator doesn't control it.
5. **Type repetition:** 14% of boards have 4+ copies of one type.

## 3. v2 vs master (2,000 seeds each)

### Line mode

| Metric | Master | v2 line |
|---|---|---|
| Most/least expensive line, median (p90) | 2.38× (3.14×) | **1.23× (1.31×)** |
| Line-cost CV, median | 0.254 | **0.064** |
| Line cost, median (p10–p90) | 13:31 (9:23–20:15) | 15:11 (13:38–16:24) |
| Synergy per line, mean | 3:27 | 2:16 |
| Lines with 0 / 1 / 2 / 3+ strong pairs | 14 / 20 / 16 / 50% | **0.7 / 52 / 34 / 13%** |
| Lines within 10% of the cheapest | 1.72 | **4.69** |
| Cheapest line: diagonal / middle | 7.2% / 16.3% | 9.3% / 18.2% |
| Boards with ≥ 4 copies of one type | 14.4% | 0% |
| Boards meeting every constraint | — | 99.8% |
| Restarts per board, mean (max) | — | 6 (50) |

v2 makes all 12 lines worth about the same, with 1–2 overlaps each.
- **4.7 lines are within 10% of the best**, so the choice between lines
  becomes a real choice.
- Diagonals are still a bit expensive (9% vs the fair 17%). Not yet
  diagnosed. The likely cause is that their cells are filled first, before
  any partner tiles exist, so they collect less synergy.

### Lockout and blackout

Master has one generator for every mode, so its dumps stand in for its
lockout and blackout boards.

| Metric | Master (as a set) | v2 lockout | v2 blackout |
|---|---|---|---|
| Snowball (most of the 13 cheapest tiles in one home course), mean; boards > 3 | 2.54; **8.1%** | 2.40; **0.5%** | 2.62; 10.2% |
| Contention (share of tiles sharing a home course), median (p10–p90) | 60% (44–72) | 56% (48–60) | 64% (52–76) |
| Longest tile, median (max) | **9:36 (21:08)** | 5:03 (5:28) | 5:03 (5:28) |
| Σ standalone, median (p10–p90) | 84:29 (73–99) | 74:20 (73–75) | 74:19 (73–75) |
| Whole-board synergy (fast solver) | 39% | 29% | 30% |

- Blackout's "encourage clustering" (hub_p 50) raised contention only a
  little (64% vs 56%). A stronger dial is needed if Matt wants co-op boards
  to be course-clustered.
- The 5:28 cap is simply the top of the cost ladder (`hi` = 5:00). Master's
  boards regularly hold a 10–20 min tile, which decides a blackout finish on
  its own.

### Feasibility on N64

Per board, the line generator does:

| Operation | Mean | p90 | Max |
|---|---|---|---|
| Candidate checks | 7.4k | 17k | 56k |
| Dial lookups (from ~900 unique tiles) | 40k | 93k | 299k |
| Pair-synergy lookups (from ~2.6k unique) | 8.8k | 20k | 66k |
| Full line costings | 90 | 204 | 600 |

- The expensive part is the ~3.5k unique fast-solver calls (standalone and
  pair). Each is a few hundred integer ops, so the mean board costs a few
  million integer ops.
- On the 93 MHz VR4300 that is roughly **0.1 s for the mean board and up to
  ~1 s at the max**.
- Caching standalone costs per skeleton makes the dial lookups cheap.
- The practical risk is the 50-restart tail. Cap restarts at ~16 (p90) and
  take the best attempt.
- Lockout and blackout are 10× cheaper (no pairs, no line checks).
- Python takes 1.3 s per line board on average, 9.7 s max.

## 4. Example v2 boards

Standalone costs are under each tile.

**Line mode, seed 7** (hubs JRB, LLL; 14 attempts). Exact line costs run
14:18–16:28 (1.15×).

```
SPLATOON:DDD:53     | DAREDEVIL:LLL:4    | POLES:17            | CANNON_STARS:1      | STARS_IN_LEVEL:THI:5
  2:56              |   1:17             |   4:11              |   2:37              |   5:55
STAR:SL:7           | BLUE_COIN:27       | RED_COIN_STARS:3    | COIN:JRB:35         | STAR:SSL:4
  2:17              |   5:52             |   3:51              |   1:00              |   4:25
KILL_MR_BLIZZARDS:4 | AMPS:10            | STAR_TIMED:JRB:1:98 | KILL_BOBOMBS:19     | KILL_SPINDRIFTS:16
  1:55              |   4:12             |   2:28              |   5:15              |   3:15
SPLATOON:BBH:55     | STAR_TIMED:TTC:6:40| STAR:CCM:1          | SHOOT_CANNONS:8     | CLICK_GAME:SSL:1:0
  5:36              |   3:17             |   1:38              |   4:47              |   1:58
1UPS:BitS:6         | KILL_BOOS:12       | STARS_IN_LEVEL:BBH:4| DAREDEVIL:LLL:7     | REVERSE_JOY:WF:5
  4:30              |   2:48             |   4:45              |   3:44              |   1:21
rows 14:33 15:14 15:19 16:28 16:09 | cols 16:20 14:18 15:31 16:00 16:10 | diags 15:03 16:11
```

**Lockout, seed 7** (hubs HMC, JRB, LLL; snowball 2, contention 48%).

```
COIN:BitS:76        | LIVES:13           | STARS_IN_LEVEL:RR:4 | STAR_TIMED:CCM:2:28 | STAR:BBH:1
  4:02              |   2:52             |   4:01              |   2:17              |   2:41
GREEN_DEMON:SL:2    | B_BUTTON:DDD:?     | 1UPS:TTM:10         | KILL_BULLIES:10     | STAR_TIMED:CCM:4:91
  1:39              |   3:38             |   3:52              |   2:17              |   2:17
POLES:11            | ! BOX:6            | KILL_BOBOMBS:9      | SIGNPOST:7          | ROOF_WITHOUT_CANNON
  2:41              |   1:37             |   1:26              |   1:09              |   1:30
REVERSE_JOY:HMC:?   | STAR:SSL:4         | REVERSE_JOY:LLL:?   | SPLATOON:HMC:44     | RED_COIN:22
  1:59              |   4:25             |   3:28              |   4:54              |   4:19
BLUE_COIN:22        | KILL_GOOMBAS:13    | STARS_IN_LEVEL:JRB:4| SPLATOON:RR:44      | STAR:BitFS:1
  4:32              |   1:42             |   4:42              |   3:23              |   2:55
```

(`?` = star number cut off in the render. `python3 gen_v2.py --seed 7 --mode
lockout` prints the full keys.)

Problems these boards show:

- **The dial saturates on the hardest cells.** Several tiles sit at the top
  of their sane range: SPLATOON 53–55%, BOBOMBS 19, STAR_TIMED with 0 s
  slack (TTC 6 at 40 s). So the cost ladder asks more than the tiles
  comfortably give. Lower `hi`, or allow more very-long types.
- **A few oddities slip through:**
  - two STAR_TIMED in CCM;
  - LLL DAREDEVIL twice (star 4 and the 100-coin star), which is legal under
    master's dedup;
  - `CLICK_GAME:SSL:1:0`, which reads oddly as a key.

## 5. Recommended dial settings (starting point)

- **Line mode:**

  | Setting | Value |
  |---|---|
  | `lo`–`hi` | 1:00–6:00 |
  | `line_tol` | 25% |
  | Strong pairs per line | 1–2 (`strong` = 45 s) |
  | `hubs` | 2 |
  | `hub_p` | 30% |
  | `type_cap` | 2 (STAR 3) |
  | Category caps | star 9, course 6, counter 8, wild 5 |
  | `restarts` | 16 in C |

  Lower `hi` to ~5:00 if the dial saturation bothers you.
- **Lockout:** snowball k = 3, contention 30–60%, ladder 1:00–5:00, wild cap
  4.
- **Blackout:** longest-tile cap 6:00, total band ±8%. Raise `hub_p` to
  60–70% if clustering matters.

## 6. Open questions for Matt

1. **Should synergy count shared visits and wildcards** (a BLJ done in a
   course you're in anyway), or only same-course pinned or supply overlap?
   The strong-pair counts depend on this definition.
2. **Is ~15 min per line the right size for a 1-line race?** (Master's median
   is 13:31, but its spread is large.)
3. **Long tiles:** today's boards have 10–20 min tiles (1x15 stars, 4–5
   Chuckyas, all 7 stars in DDD, 15+ cannons). Should v2 allow a bounded
   number of them (for example one per board in lockout), or keep them out?
4. **Diagonals:** accept them being slightly expensive, or give diagonal
   targets a ~5% discount?
5. **Should offline 1-line keep the old generator behind a version switch**
   (golden stability), with v2 for online and the new modes only?

## 7. Which votes matter most

`measure.py --sensitivity` multiplies one knob by 1.5 and counts how often the
cheapest line of a master board changes (300 boards):

| Knob | Flips | Catalog tiles it moves |
|---|---|---|
| `star.scale_pct` (star work vs everything else) | **17%** | 42 |
| `f.pct` (free units per visit) | **11%** | 31 |
| `coins.u` | 9% | 10 |
| `V.BOB` | 8% | 25 |
| `oneups.u` | 6% | 3 |
| V for TTC / THI / SL / HMC | 5–6% each | |
| `u.SIGNPOST`, `splatoon.per_tile`, `blj.per_course`, reverse-joystick multiplier | 4–6% each | |

Most needed votes:

- **Stars vs counters/visits** (standalone pairs such as STAR:X vs
  KILL_Y:N): they fix `star.scale_pct`.
- **Context votes**: they are the only thing that separates V from star work
  and measures `f` (freebies).
- **Coin tiles against other tiles.**
- **1-up and lives tiles**: few catalog tiles touch `oneups.u`, and LIVES
  looks too cheap. Consider adding a LIVES:14 vs STAR pair or two.

Knobs no catalog tile touches (so no vote can fix them), all in `params.json`:

- the BitS pieces (`BitS:1`, the BitS Bowser fight, `V.BitS`);
- `purple.per_star` beyond the one WF tile;
- the counter tail multiplier;
- `reentry`, which only context votes see.

`fit.py --selftest` recovers 6 of 7 planted knob changes from 5,200 synthetic
votes and cuts the log-cost error by 30%. `coins.u` is not recovered: it
trades off against `coins.free_per_visit`.
