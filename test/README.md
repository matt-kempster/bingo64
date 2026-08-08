# bingo64 tests

Three layers, from fastest to deepest. All of them exist so that bingo
features can be added or changed without hand-testing in an emulator.

| Layer | Where | Time | What it proves |
|---|---|---|---|
| Unit tests | `test/host` | ~2 s | The bingo logic is right |
| Smoke test | `test/emu` (`make test-smoke`) | ~1 min | The ROM boots, plays, renders |
| RAM test | `test/emu` (`make test-ram`) | ~1 min | The running ROM agrees with the unit-tested logic |
| Warp test | `test/emu` (`make test-warp`) | ~1 min | Tests can jump straight into any course |
| Splatoon test | `test/emu` (`make test-splat`) | ~1 min | Walking paints floor triangles |
| Star UI test | `test/emu` (`make test-starui`) | ~1 min | Modifier star row renders complete, evenly spaced, centered |

## Running

```sh
# once: build the ROM (needed by the emulator tests, and for two
# generated headers the unit tests include)
QEMU_IRIX=~/opt/qemu-irix-extract/usr/bin/qemu-irix make GRUCODE=f3dex -j8

cd test/host && make test        # unit tests
cd test/emu && make test         # smoke + RAM tests
```

The emulator tests expect mupen64plus in `~/opt/m64p/install` (override
with `M64P=...`), built with `DEBUGGER=1` for the RAM test.

## How the pieces work

**test/host** compiles the real `src/game/bingo*.c` files with plain gcc.
Stub headers in `test/host/stubs/` stand in for the N64 SDK and the rest
of the game. An MT19937 known-answer test proves the host RNG matches the
console's, so boards generated here are the boards players get.

**test/emu** drives the game from power-on with a scripted controller
recording (`scripts/boot_and_board.txt`), played by a purpose-built input
plugin. Emulation is deterministic: the same script gives the same run
every time, including the "random" seed. There are no savestates on
purpose — a savestate contains the old build's code in RAM, so it would
silently keep testing stale code. Fresh runs from power-on always test
the ROM you just built.

The RAM test looks up global variables in `build/us/sm64.us.map`, reads
them out of emulated memory mid-run, and diffs the live board against the
host generator's output for the same seed.

## Warping into levels

Walking from the castle to a course in an input script is slow and
fragile. Instead the game has one test-only global, `gTestWarpRequest`
(in `level_update.c`). Nothing in the game writes it; when a test
harness pokes a level number into it through the emulator debugger, the
game warps there through the same code as stepping into a painting. The
star select screen appears as normal, so a script can pick any act —
that is how act-specific objectives (click game, timed stars) can be
reached directly.

The recipe, from `warp_test.py` and `scripts/warp_bob.txt`: boot with
the usual script, write the level number at frame 450, star select is up
by ~530, an A press at frame 700 lands Mario in the course by ~720.
Total: about 12 seconds of emulated time to be standing in any level.

Levels are numbered by `levels/level_defines.h` order (BOB=9, CCM=5,
WF=24). Poking works on every fresh build because addresses come from
the linker map, which regenerates with the ROM — this is why RAM pokes
are fine where savestates are not.

Bingo modifiers are picked on the star select screen with Z/R. Z on a
fresh screen wraps the selection backwards from NONE to the *last*
modifier, so a script can select the newest one with a single Z press
(`scripts/star_ui.txt` does this for splatoon). The star UI test also
screenshots the screen and checks the modifier row geometrically: it
finds each star by color, and asserts one star per `enum BingoModifier`
entry, in enum order, evenly spaced, centered — so adding a modifier
without a star (or breaking the row's layout) fails the test.

## Splatoon

Painted floors are tracked as (area, surface index) bits, so paint
survives area transitions inside a course and the painted count never
double-counts. The per-course floor totals hardcoded in
`bingo_const.c` (`course_floors`) come from `test/count_floors.py`,
which applies the same rule `surface_load.c` uses (a triangle is a floor
when its normal's y exceeds 0.01) after resolving `#ifdef VERSION_JP`
blocks to the US branch — RR and LLL carry both variants in one file,
and counting the raw text doubles them. The splatoon test cross-checks
BOB's 621 against the live count in emulated RAM. If a level's collision
ever changes, rerun the script and update the table.

The RAM test's board dump decodes `enum BingoObjectiveType` by number
(`ram_test.py` top constants) — inserting an enum entry shifts them, and
the test fails with a field-name mismatch until the constants are
updated to match.

## Golden files, and when tests "fail" on purpose

Changing board generation at all (new objective type, new weights, RNG
call order) changes every generated board. That is usually intended, and
the tests treat it as a re-blessing, not a fix-up:

```sh
cd test/host && UPDATE_GOLDENS=1 make test && make test
cd test/emu && UPDATE_GOLDENS=1 make test-smoke   # re-shoot board screen
```

Two blessed counts live in `test_bingo.c` and may need updating with a
generation change: `EXPECTED_BOARDS_WITH_DUPLICATES` (dedup leftovers per
10k seeds) and `KNOWN_OVER_BUDGET_BOARDS` (see the bug note there).

If the *input script* changes menu timing, the deterministic seed
changes; update `EXPECTED_SEED` in `ram_test.py` from the failure
message.

## Adding a new objective type: the checklist

After wiring the objective into the game (enum, weights table, init,
`update_objective` case, info/title/description), add:

1. **A simulation test** in `test/host/test_bingo.c`: hand-build the
   objective, feed it `bingo_update()` events, assert the state flips
   (and doesn't flip for wrong-course/wrong-event cases). Copy the shape
   of `test_sim_coin_objective`.
2. **Re-bless the goldens** (see above) — board layouts will shift.
3. Run `cd test/emu && make test`. If the new objective can appear on
   the board's first screen, the smoke golden may need re-blessing too.

The invariant sweep and budget test cover the new type automatically
through the weight tables.

`BOARD_SEED=n ./build/run_tests` (in `test/host`) prints any seed's board
for eyeballing.

## Porting notes

The unit tests are surface-independent: they already run the bingo logic
on x86-64 with plain gcc, which is most of what a PC port needs from this
code. When porting, keep `test/host` green and the logic is intact; the
emulator tests then pin the N64 build as the reference behavior to match.
