# bingo64 tests

Three layers, from fastest to deepest. All of them exist so that bingo
features can be added or changed without hand-testing in an emulator.

| Layer | Where | Time | What it proves |
|---|---|---|---|
| Unit tests | `test/host` | ~2 s | The bingo logic is right |
| Smoke test | `test/emu` (`make test-smoke`) | ~1 min | The ROM boots, plays, renders |
| RAM test | `test/emu` (`make test-ram`) | ~1 min | The running ROM agrees with the unit-tested logic |

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
