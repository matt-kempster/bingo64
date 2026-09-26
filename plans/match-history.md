# Match history (relay log) — for datamining tile times later

The relay appends one JSON object per line to `<matchlog>/YYYY-MM.jsonl`
(the deploy points `--matchlog` at the VM's disk). Finished months are
gzipped by the daily loop (`YYYY-MM.jsonl.gz`, ~11x smaller). Every record
has `t` (UTC, second resolution), `ev`, and usually `room`. `frames` are
30 fps race frames since GO — use those, not `t`, for timing.

## Events that matter for tile times (protocol 11+)

| ev | fields | notes |
|---|---|---|
| `start` | seed, mode, unlock, mask, protocol, calls_open, calls_win, players[{id,name,color}] | one per race; `id`s are per-room |
| `board` | id, cells[25] | each racer's board as dealt, once per race — no generator needed to decode |
| `calls_queue` | id, queue[25] | Call and Response call order (cell indices) |
| `where` | id, level, area, frames | on every (level, area) change after GO; cap 1000 per racer per race |
| `claim` | id, cell, frames | accepted claims; exclusive modes log only the owner |
| `finish` / `lockout_win` / `timeout` | … | the verdicts |
| `lobby_reset` | | race over, back to lobby (the next `start` is a new race) |

`level` is the SM64 `LEVEL_*` number (castle inside = 6, grounds = 16,
courtyard = 26, BOB = 9, …); see `levels/level_defines.h` (1-based row order).

## Cell encoding

`type.class[.a[.b[.c]]]`, trailing zeros dropped (bingo_net.c
`board_cell_code`, fields as in test/host `dump_cell`):

- star types: course . starIndex (0-based) . limit (timed: seconds, click game: clicks)
- per-course (coins, 1-ups in level, stars in level, random stars/reds, splatoon): course . toGet
- multi-course (stars in N levels, dangerous wall kicks): total . each
- Bowser: level
- everything else: toGet

`type` is `enum BingoObjectiveType` *of that protocol's build* — the enum
has grown over time, so decode a log with the `bingo.h` from the git tag of
its `start.protocol` (beta.N = protocol N).

## Turning it into per-tile times (the plan, not built)

- Line modes: each racer's claims in frame order; the time "spent" on a
  claim is the gap since that racer's previous claim, split across the
  courses in `where` during the gap. Tiles completed in one visit
  together (synergy) show up as near-zero gaps — fit set costs, not
  single tiles (test/cost_model's model), rather than averaging gaps.
- Call and Response is the clean signal: the calls open at any moment are
  the first calls_open unclaimed queue entries, so each call's time is
  from when it opened to its claim.
- Solo games never reach the relay: no data from them.
