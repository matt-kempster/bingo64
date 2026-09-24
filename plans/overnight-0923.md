# Overnight 2026-09-23 → 24: what changed

Branch `overnight-0923` (NOT merged to master). Test build:
`C:\Users\Matt\AppData\Local\bingo64-test\overnight-0923\bingo64-10.0-dev-overnight.exe`
(own `res\base.zip` next to it). Smoke-tested: launches and responds on
Windows. **Nothing below has been played in-game yet.**

## Try these in the exe

**New tiles (any settings).** Options → objective grid:
- STAR CHALLENGES: **Coinless star**: collect star X without touching a
  coin. Any coin fails the visit; re-entering the course resets it. The
  pool skips 100-coin, red-coin, BoB Wings to the Sky, and the CCM/TTM
  slides.
- COLLECTING:
  - **Warp pads**: use N different pad pairs (12 exist). Back and forth
    on one pair counts once.
  - **Koopa shells**: ride N different shells. There are 5 box/underwater
    shells, plus Koopa Troopas, which *do* drop rideable shells (I was wrong
    about that earlier). A respawned shell from the same box doesn't count
    again.
  - **Spinning hearts**: spin N different hearts (13 exist).
- Presets: SRL keeps all four off, Vanilla has them on, and Casual drops
  the coinless star.

**Progression tiles (need Unlock full game OFF, e.g. the SRL preset).**
There is a new PROGRESSION band. Its icons are dimmed while unlock is ON,
and the footer says "Unlock OFF only".
- **Open N cannons** (Bob-omb Buddies): N 3/4/5. Only 4 Buddies are on the
  first floor of a fresh file, so N is lower than SRL's 3–11 ladder.
- **Toad star** (hard, N=1): basement Toad at 12 stars.
- **MIPS** (hard, N=1): 15 stars + the basement key.
- SRL preset: cannons + Toad ON, MIPS off (MIPS isn't an SRL goal).
- With unlock ON these are never dealt, and unlock-ON boards are
  byte-identical to before this change.

**Behavior change to check:** an unlock-OFF race now starts from a genuinely
fresh file in memory. Before, it inherited whatever the slot held, including
120 stars left over from an earlier unlock-ON race (found by the SRL audit).
It writes nothing to EEPROM until the race saves. This mirrors how unlock-ON
stamps the file. Checked by reading the code only.

**Online:** protocol is now **10** (board generation changed). It won't talk
to the deployed relay. Run `server/relay.py` from this branch locally, or
redeploy before any real online use.

## Analysis (no game changes)

- **Voting page:** https://claude.ai/artifact/MHAdPpkZPaWEsHsbeP8pxu
  - It's private; share it from the page's Share menu if friends should vote.
  - Keys: ← → pick the longer tile, ↓ = about the same, S = skip.
  - "Already in a course" mode is the most valuable one: it pins the free
    units per visit and separates visit cost from star work.
  - "Stars vs counters" pairs set the star scale.
- **Cost model + v2 generator prototype:** `plans/cost-model-report.md` (read
  §1 first: 10 real lines with times for a gut check) and `test/cost_model/`.
  All times are hand-guessed priors until votes come in. Headlines:
  - Today, the priciest line on a board is a median 2.38× the cheapest. v2
    gets that to 1.23×, and near-tie lines go from 1.7 to 4.7 per board.
  - Class barely predicts time today (easy median 2:45 vs medium 2:33).
  - v2 lockout: boards where one course hosts more than 3 of the 13 cheapest
    tiles drop from 8.1% to 0.5%, and the longest tile is capped (was 9–21
    min, now ≤ 5:28).
  - Open problems: the N dial maxes out on the hardest cells; diagonals are a
    bit pricey; blackout clustering is weak; LIVES looks too cheap.
- **SRL mimic audit:** `plans/srl-mimic-audit.md`. Of 105 goals: 66 exact,
  16 need a parameter, 17 need a detector, 6 planned. The best next detector
  is a generic "N stars from list L" (covers 10 goals).
- Design docs: `plans/synergy-objectives.md` (live tiles, progression,
  parked ideas) and `plans/board-generation.md`.

## Fixes
- Host harness board dump no longer segfaults (B/Z challenges printed a stale
  hint pointer). The same bug is fixed in `web/gen/wasm_api.c` and
  `ram_test.py`.

## Not done / known
- No N64 ROM build and no emu tests (`make test-rando` was already broken on
  master). Host tests (36/36), relay tests, `web make check` and the Linux
  build pass.
- The sticky PRESET header on the options page lets the icons behind it show
  through (from the sticky-header commit, not tonight's work).
- `test/board_gen_bias.py` is out of date with the new objectives.
- Live tiles and call-and-response are designed but not started.

## Commits (oldest last)
```
e632cd5c4 [cost-model] README, knob sensitivity, results report
32e05b51b [bingo] Progression objectives (unlock OFF only) + fresh-file race start
7f2101d5c [cost-model] Prototype v2 generators (line/lockout/blackout) + master comparison
54f9b52c4 [cost-model] Stage 2 measurement + vote fitting (Bradley-Terry -> knobs)
cd2fd8a4f [docs] Koopas do drop rideable shells
9c0b6bbbb [bingo] Protocol 10: board generation changed
7f2aac7e6 [bingo] Four new objectives: coinless star, warp pads, Koopa shells, spinning hearts
e8b9faddd [cost-model] Demand model + set solver (exact over course subsets, fast greedy passes)
95d3a0e01 [bingo] Board dump: print the A-button hint only for A challenges
1d1701b4e [docs] SRL mimic coverage audit
373f1e70b [cost-model] Tile catalog for pairwise time voting
93dd0c3a7 [docs] Plans: synergy objectives + board generation investigation
```
