# Cost model + board generator prototype (analysis only)

Python 3 stdlib, no game code. Design: `plans/board-generation.md` §3 and §5.
Results: `plans/cost-model-report.md`.

Times are integer **deciseconds** everywhere (600 = 1 min). Every number in
`params.json` is a hand guess until votes come in.

| File | What |
|---|---|
| `catalog.json` | 121 tiles + 10 contexts for the pairwise voting page (keys are stable). |
| `data.py` | Game facts: enum order, per-course supplies, star times, 1-ups, floors, master weight tables, N ranges, ABC/click tables. |
| `params.json` | Hand priors (V, reentry, star work, challenge multipliers, u, f, coins, 1-ups, BLJ/wall kicks/splatoon/purple/roof), each with a `_why`. |
| `model.py` | Tiles (from catalog keys or harness dumps) → demands → cost. `standalone_cost`, `exact_set_cost`, `fast_line_cost`. |
| `measure.py` | Stage 2: the model over master's board dumps (+ 10 example lines). |
| `fit.py` | Votes → Bradley–Terry strengths → fitted knobs (`params.fitted.json`). |
| `gen_v2.py` | Stage 4 prototypes: line mode (magic square + synergy band), lockout, blackout. |
| `compare.py` | Master vs v2 on the stage-2 metrics, plus generator op counts. |
| `out/` | Generated reports (git-ignored). |

## Run

```sh
cd test/cost_model
python3 model.py --catalog                      # standalone cost of every catalog tile
python3 model.py STAR:THI:1 KILL_GOOMBAS:12     # cost a set, show the plan and synergy
python3 model.py --approx <boards dir>          # fast vs exact error over real lines
python3 measure.py                              # stage 2 over all master dumps (~5 min, 8 cores)
python3 measure.py --examples                   # 10 varied real lines with plans
python3 fit.py votes.json                       # "no votes" if empty/missing
python3 fit.py --selftest                       # recover planted knobs from synthetic votes
python3 gen_v2.py --seed 5 --mode line          # also: lockout, blackout; --set line_tol=20
python3 compare.py --seeds 2000                 # master vs v2 (~15 min, 8 cores)
```

Board dumps default to the scratchpad set from the 2026-09-23 session
(`boards/<seed>.txt`, host harness `BOARD_SEED=n`, master's generator).
Pass `--boards DIR` to point elsewhere.

## The model in one paragraph

A tile becomes demands: pinned stars (course, star, work × challenge
multiplier), course-local work (coins in one entry, 1-ups, splatoon, purple
stars), star wildcards (stars in a level, red-coin stars, N stars in M
courses, total stars), global counters with per-course supply (kills, signs,
poles, reds, deaths, hat ways...; same-type tiles take the max), and other
wildcards (BLJ/wall kicks in N courses, total coins, lives — deaths drain
lives). A plan visits a set of courses (V each, the hub is free), collects
stars (a second star in a course pays `reentry`, since stars exit the
course), takes free units per visit first, then the cheapest paid units, and
opens a new course whenever its average cost (V included) beats the next paid
unit. `exact_set_cost` tries every subset of up to 10 candidate courses × both
phase orders; `fast_line_cost` is ≤ 4 fixed greedy passes (what a C port would
do). Synergy = Σ standalone − set cost.

Known blind spots: movement inside a course is one number per course; the
fill inside a subset is greedy, so "exact" is exact over course subsets only;
supplies for cannon stars and unique deaths are rough guesses.

## Votes

`fit.py` expects a JSON array of `{"a","b","winner":"a"|"b"|"tie","ctx",
"voter","t"}` with winner = the tile that takes longer (with `ctx`: the one
that adds more time given you are already doing the ctx star). Votes only
fix ratios; the fit rescales every time-valued knob so the voted tiles keep
the prior's geometric mean. `LAMBDA` (voter noise) is an assumption: a tile
twice as long wins 90% of the time.
