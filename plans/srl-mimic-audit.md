# SRL mimic: coverage audit of the SM64 bingosync goal list

Audit 2026-09-23. Question: how much of SpeedRunsLive SM64 bingo (bingosync
"Super Mario 64 → Normal") can bingo64 already express, and what a faithful
mode would take. Follows `plans/board-generation.md` §6 and
`plans/synergy-objectives.md` §2.

Sources: bingosync-app `generators/super_mario_64_generator.js` (goal list,
105 goals in 25 tiers, with `types` tags) and `generators/generator_bases/srl_generator_v5.js`
(generator). Line numbers below refer to these two files ("list:N" and "v5:N").
On the bingo64 side: `bingo.h`, `bingo_objective_init.c`,
`bingo_objective_func.c`, `bingo_board_setup.c`, `bingo.c`,
`bingo_tracking_star.c`, `save_file.c` as of `373f1e70b`.

Star indices below are 0-based, as used by `StarObjectiveData.starIndex`.

## Status legend

- **EXACT**: an existing objective with a fixed parameter reproduces the goal.
  The init code may never pick that parameter on its own. A faithful mode sets
  it from the goal table.
- **PARAM**: the detector exists, but the goal needs a parameter the current data
  or init can't hold: an N outside the init range, a course the init never
  picks, or a course subset.
- **PLANNED**: covered by objectives another agent is adding now (Bob-omb Buddy
  cannon ladder, Toad stars, MIPS).
- **NEW**: needs a new detector.
- **AWKWARD**: none of the 105 goals is impossible. The unlock-dependent ones
  are covered in §4.

## 1. Goal-by-goal table

| Tier | Goal | Types | Status | bingo64 mapping / what's missing |
|---|---|---|---|---|
| 1 | Red Coin Star in WF | WF | EXACT | `STAR` (WF, 3) |
| 1 | 100 Coin Star in WF | WF | EXACT | `STAR` (WF, 6) |
| 1 | Peach's Slide x 2 | secrets | PARAM | `STARS_IN_LEVEL` (PSS, 2). The detector (`bingo_get_course_count`) works for any course, but the init only picks main courses. Also covered by the star-set counter (§2). |
| 1 | Reach the Castle Roof | lives | EXACT | `ROOF_WITHOUT_CANNON`. On a fresh file the grounds cannon needs 120 stars, so "without cannon" is implied. |
| 1 | Lose Mario's Hat | losehat | PARAM | `LOSE_MARIO_HAT` with N=1 (the init gives 3–4). |
| 2 | 5 Castle Secret Stars | secrets | NEW | Star-set counter over the 15 castle secret stars (§2) |
| 2 | Beat the King in BOB | BOB | EXACT | `STAR` (BOB, 0). The goal name has a trailing space (list:24). |
| 2 | Two Bowser Stage Red Coin Stars | bowserreds | PARAM | `RED_COIN_STARS` restricted to BitDW/BitFS/BitS, N=2 (needs a course-mask field) |
| 2 | Collect 120 Coins in one stage | WF, hundredcoin | PARAM | `COIN` with course = any and N=120. The per-visit counter already resets on course change. The init caps N at 99 and always pins a course. |
| 2 | 100 Coin Star in CCM | CCM | EXACT | `STAR` (CCM, 6) |
| 3 | Open 3 cannons | cannons | PLANNED | Cannon ladder |
| 3 | Secret Aquarium Star | aquarium | EXACT | `STAR` (SA, 0) |
| 3 | 15 Lives | lives | EXACT | `LIVES` 15. The detector reads the absolute HUD counter, and 15 is in the HARD range 12–15. |
| 3 | Collect 130 Coins in one stage | hundredcoin | PARAM | Same as the 120-coin goal, N=130 |
| 4 | Mario Wings to the Sky BOB | BOB, wingcap | EXACT | `STAR` (BOB, 4) |
| 4 | Red Coin Star in HMC | HMC | EXACT | `STAR` (HMC, 1) |
| 4 | All Stars in Whomps | WF | EXACT | `STARS_IN_LEVEL` (WF, 7) |
| 4 | Three 100 Coin Stars | hundredcoin | NEW | Star-set counter: star 6 of the 15 mains, N=3 |
| 4 | BITS Red Coin Star | bowserreds | EXACT | `STAR` (BITS, 0) |
| 5 | Red Coin Star in TTM | TTM | EXACT | `STAR` (TTM, 2) |
| 5 | Cruiser Crossing the Rainbow RR | RR | EXACT | `STAR` (RR, 0) |
| 5 | Collect 140 Coins in one stage | hundredcoin | PARAM | Same as the 120-coin goal, N=140 |
| 5 | Secrets Star in THI | THI | EXACT | `STAR` (THI, 3) (Five Itty Bitty Secrets) |
| 6 | Secrets Star in WDW | WDW | EXACT | `STAR` (WDW, 2) (Secrets in the Shallows & Sky) |
| 6 | Red Coin Star in BOB | BOB | EXACT | `STAR` (BOB, 3) |
| 6 | 2 Cap Stage Stars | wingcap, vanishcap, metalcap, secrets | PARAM | `RED_COIN_STARS` masked to TotWC/CotMC/VCutM (each cap stage has only its red coin star), N=2 |
| 6 | Red Coin Star in BBH | BBH | EXACT | `STAR` (BBH, 3) |
| 7 | 7 Castle Secret Stars | secrets | NEW | Castle-secrets star-set counter, N=7 |
| 7 | 100 Coin Star in LLL | LLL | EXACT | `STAR` (LLL, 6) |
| 7 | 20 lives | lives | PARAM | `LIVES` 20 (the init caps it at 15) |
| 7 | Red Coin Star in SSL | SSL, wingcap | EXACT | `STAR` (SSL, 4) |
| 8 | All Stars in CCM | CCM | EXACT | `STARS_IN_LEVEL` (CCM, 7) |
| 8 | 100 Coin Star in JRB | JRB | EXACT | `STAR` (JRB, 6) |
| 8 | Red Coin Star in WDW | WDW | EXACT | `STAR` (WDW, 4) |
| 9 | Eye in the Secret Room BBH | BBH, vanishcap | EXACT | `STAR` (BBH, 5) |
| 9 | Three Bowser Stage Red Coin Stars | bowserreds, secrets | PARAM | Masked `RED_COIN_STARS`, N=3 |
| 9 | 100 Coin Star in WDW | WDW | EXACT | `STAR` (WDW, 6) |
| 9 | 100 Coin Star in TTC | TTC | EXACT | `STAR` (TTC, 6) |
| 10 | At least 1 star from 10 stages | starseach | EXACT | `STARS_MULTIPLE_LEVELS` K=1, N=10 (HARD gives 1×7–10) |
| 10 | 100 Coin Star in SSL | SSL | EXACT | `STAR` (SSL, 6) |
| 10 | Into the Igloo SL | SL, vanishcap | EXACT | `STAR` (SL, 5) |
| 10 | 25 Lives | lives | PARAM | `LIVES` 25 |
| 10 | Open 5 Cannons | cannons | PLANNED | Cannon ladder |
| 11 | Red Coin Star in JRB | JRB | EXACT | `STAR` (JRB, 3) |
| 11 | Mystery of the Monkey Cage TTM | TTM | EXACT | `STAR` (TTM, 1) |
| 11 | Top Floor Cloud Stage Star | wingcap | EXACT | `STAR` (WMOTR, 0) |
| 11 | Collect the Caps DDD | DDD, vanishcap | EXACT | `STAR` (DDD, 5) |
| 12 | 3 Cap Stage Stars | wingcap, vanishcap, metalcap, secrets | PARAM | Cap-masked `RED_COIN_STARS`, N=3 |
| 12 | 100 Coin Star in TTM | TTM | EXACT | `STAR` (TTM, 6) |
| 12 | 4 Stars each from SSL and HMC | SSL, HMC | NEW | Two-course stars: `bingo_get_course_count` ≥ K in both courses |
| 12 | All Stars in LLL | LLL | EXACT | `STARS_IN_LEVEL` (LLL, 7) |
| 12 | Four 100 Coin Stars | hundredcoin | NEW | 100-coin star set, N=4 |
| 13 | Race Through Downtown WDW | WDW, vanishcap | EXACT | `STAR` (WDW, 5) |
| 13 | 100 Coin Star in HMC | HMC | EXACT | `STAR` (HMC, 6) |
| 13 | 100 Coin Star in THI | THI | EXACT | `STAR` (THI, 6) |
| 13 | 100 Coin Star in DDD | DDD | EXACT | `STAR` (DDD, 6) |
| 13 | 4 Stars each from BOB and CCM | BOB, CCM | NEW | Two-course stars |
| 14 | 100 Coin Star in RR | RR | EXACT | `STAR` (RR, 6) |
| 14 | 100 Coin Star in BOB | BOB, wingcap | EXACT | `STAR` (BOB, 6) |
| 14 | 30 Lives | lives | PARAM | `LIVES` 30 |
| 14 | 100 Coin Star in BBH | BBH | EXACT | `STAR` (BBH, 6) |
| 14 | Open 7 Cannons | cannons | PLANNED | Cannon ladder |
| 15 | Defeat all 4 Mini-Bosses | SSL, THI | NEW | All-of star set. The list doesn't define the four. The usual reading is King Bob-omb (BOB 0), Whomp King (WF 0), Eyerok (SSL 3) and Wiggler (THI 5). **The set needs a ruling.** |
| 15 | 3 Stars each from JRB and BBH | JRB, BBH | NEW | Two-course stars |
| 15 | 30 Total Stars | manystar | PARAM | `MULTISTAR` 30 (the init caps it at 12). It counts stars from this race, not the save file. |
| 15 | 100 Coin Star in SL | SL, vanishcap | EXACT | `STAR` (SL, 6) |
| 15 | Rematch with Koopa the Quick THI | THI | EXACT | `STAR` (THI, 2) |
| 16 | 10 Castle Secret Stars | secrets, aquarium | NEW | Castle-secrets set, N=10. The 10 non-Toad/MIPS secrets are enough. |
| 16 | At least 3 stars from 6 stages | starseach, manystar | EXACT | `STARS_MULTIPLE_LEVELS` K=3, N=6 (CENTER gives 3×5–6) |
| 16 | At least 2 stars from 10 stages | starseach, manystar | PARAM | `STARS_MULTIPLE_LEVELS` K=2, N=10 (the init caps K=2 at N=8) |
| 16 | All Stars in JRB | JRB | EXACT | `STARS_IN_LEVEL` (JRB, 7) |
| 16 | 5 Stars in SL | SL | EXACT | `STARS_IN_LEVEL` (SL, 5) |
| 17 | 3 Stars each from TTC and RR | TTC, RR | NEW | Two-course stars |
| 17 | All Stars in HMC | HMC | EXACT | `STARS_IN_LEVEL` (HMC, 7) |
| 17 | Five 100 Coin Stars | hundredcoin, starseach, lives | NEW | 100-coin star set, N=5 |
| 17 | Collect 140 Coins in two stages | hundredcoin | NEW | Count distinct courses whose single-visit coin total reached 140 (a per-course high-water mark in race tracking) |
| 17 | 5 Stars in DDD | DDD | EXACT | `STARS_IN_LEVEL` (DDD, 5) |
| 18 | 4 Stars each from JRB and DDD | JRB, DDD | NEW | Two-course stars |
| 18 | 3 Stars each from THI and TTM | THI, TTM | NEW | Two-course stars |
| 18 | All Stars in BOB | BOB | EXACT | `STARS_IN_LEVEL` (BOB, 7) |
| 19 | All Stars in TTC | TTC | EXACT | `STARS_IN_LEVEL` (TTC, 7) |
| 19 | Six 100 Coin Stars | hundredcoin, starseach, lives | NEW | 100-coin star set, N=6 |
| 19 | 5 Stars in BBH | BBH | EXACT | `STARS_IN_LEVEL` (BBH, 5) |
| 19 | Open 9 Cannons | cannons | PLANNED | Cannon ladder |
| 20 | All Stars in SSL | SSL | EXACT | `STARS_IN_LEVEL` (SSL, 7) |
| 20 | 6 Stars in WDW | WDW | EXACT | `STARS_IN_LEVEL` (WDW, 6) |
| 20 | 6 Stars in SSL | SSL | EXACT | `STARS_IN_LEVEL` (SSL, 6) |
| 21 | At least 1 Star from each Stage | starseach, manystar | EXACT | `STARS_MULTIPLE_LEVELS` K=1, N=15 (CENTER gives 1×11–15). The detector counts BOB..RR only, which matches SRL's "stage". |
| 21 | Win All 3 Character Races | BOB, CCM, THI | EXACT | `RACING_STARS`: KTQ BOB, Big Penguin Race, KTQ rematch. The same three as the types. |
| 21 | At least 3 stars from 8 stages | starseach, manystar | PARAM | `STARS_MULTIPLE_LEVELS` K=3, N=8 (the init caps K=3 at N=6) |
| 21 | All Stars in TTM | TTM | EXACT | `STARS_IN_LEVEL` (TTM, 7) |
| 22 | 12 Castle Secret Stars | secrets, aquarium | NEW | Castle-secrets set, N=12. It needs at least 2 Toad/MIPS stars, so it also depends on PLANNED and unlock OFF (§4). |
| 22 | 35 Total Stars | manystar | PARAM | `MULTISTAR` 35 |
| 22 | 6 Stars in RR | RR | EXACT | `STARS_IN_LEVEL` (RR, 6) |
| 23 | Star #1 from each stage | starseach | NEW | All-of star set: star 0 of the 15 mains |
| 23 | Top Floor Toad Star | manystar | PLANNED | Toad stars (third Toad, 35-star gate) |
| 23 | All Stars in Snowmans | SL, vanishcap | EXACT | `STARS_IN_LEVEL` (SL, 7) |
| 23 | 6 Stars in BBH | BBH | EXACT | `STARS_IN_LEVEL` (BBH, 6) |
| 24 | 6 Stars in DDD | DDD | EXACT | `STARS_IN_LEVEL` (DDD, 6) |
| 24 | All Stars in WDW | WDW, vanishcap | EXACT | `STARS_IN_LEVEL` (WDW, 7) |
| 24 | All Stars in THI | THI | EXACT | `STARS_IN_LEVEL` (THI, 7) |
| 25 | All Stars in BBH | BBH, vanishcap | EXACT | `STARS_IN_LEVEL` (BBH, 7) |
| 25 | Open All 11 Cannons | cannons | PLANNED | Cannon ladder: all 11 Buddies (synergy plan §2 lists them) |
| 25 | All Stars in DDD | DDD, vanishcap | EXACT | `STARS_IN_LEVEL` (DDD, 7) |
| 25 | All Stars in RR | RR | EXACT | `STARS_IN_LEVEL` (RR, 7) |

Observations on the list:
- There are no MIPS, key, cap-switch, star-door or Bowser-fight goals. Toad
  appears once (tier 23). The new warp-pad, Koopa-shell, heart and coinless
  objectives match no SRL goal.
- There is one goal per tier per cell and each tier appears once per board
  (§3), so the same goal can't appear twice. Overlapping goals can
  ("Red Coin Star in BOB" and "All Stars in BOB"), limited only by synergy.
- Some tags are odd: "Five/Six 100 Coin Stars" carry `lives` (list:244, list:267),
  and "Top Floor Toad Star" is `manystar` (list:313). Only "Lose Mario's Hat"
  is tagged `losehat`, so it never has synergy with anything.
- 14 goal names are 30–33 characters, too long for `BingoObjective.title[30]`
  (for example "Rematch with Koopa the Quick THI" at 32). Titles come from
  `get_objective_title`, so the mode either keeps bingo64 wording or needs a
  wider title field.

## 2. Summary and shortest path

| Status | Goals |
|---|---|
| EXACT | 66 |
| PARAM | 16 |
| NEW | 17 |
| PLANNED | 6 |
| IMPOSSIBLE/AWKWARD | 0 (4 goals depend on unlock OFF; see §4) |
| **Total** | **105** |

Simulated on 10,000 bingosync seeds (node, the real generator): **0 boards**
are all-EXACT, **0.1%** are EXACT+PARAM only, and **0.9%** avoid every NEW
goal. **91%** of boards contain a PLANNED goal (cannons fill 5 tiers). The
mode is not worth shipping until nearly everything below exists.

Missing detectors, ranked by the goals they unlock:

1. **Star-set counter**: "N of the stars in list L". This generalizes
   `objective_red_coin_stars` (`sRedCoinStars` + recount from
   `bingo_get_course_flags`) to a list id plus N, with "all" as N = the list
   length. Lists:
   - Castle secret stars: PSS×2, SA, TotWC, CotMC, VCutM, WMotR, BitDW/FS/S
     reds from `gbCourseStars`, plus Toad×3 and MIPS×2 from `gbSecretStarFlags`
     (`bingo_set_star(-1, …)`)
   - 100-coin stars (star 6 of the 15 mains)
   - Star 0 of the 15 mains
   - Mini-bosses
   - Bowser-stage reds and cap-stage stars
   - PSS

   It unlocks **10 NEW goals** (castle secrets ×4, 100-coin counts ×4,
   Star #1, mini-bosses) and absorbs **5 PARAM goals** (Bowser reds ×2, cap
   stages ×2, PSS ×2). Hook: `BINGO_UPDATE_STAR`, the same hook the red coin
   star tile uses. No new game hooks are needed.
2. **Two-course stars**: K stars in both of courses A and B. It unlocks
   **6 NEW goals**. It needs a two-course data variant and reuses
   `bingo_get_course_count`.
3. **Cannon ladder (PLANNED)**: 5 goals. Toad stars (PLANNED) add 1 more.
4. **Fixed-parameter init path**: covers the remaining **11 PARAM goals**:
   - `LIVES` 20/25/30
   - `MULTISTAR` 30/35
   - `STARS_MULTIPLE_LEVELS` 2×10 and 3×8
   - `LOSE_MARIO_HAT` 1
   - `COIN` any-course at 120/130/140

   The detectors already exist; only `COIN` needs a "course = any" branch in
   `objective_obtain_coins`. This is data work, not detector work.
5. **Coins in M stages**: count courses where a single visit reached N coins.
   It unlocks **1 goal** (140 coins in two stages, on 24% of boards).

Steps 1, 2 and 4 plus the planned work cover 104/105. Step 5 covers the last one.

## 3. Porting the generator

**The SRL v5 algorithm (verified by running it on seeds 1–2000):**
- **Magic square (v5:129–169):** `difficulty(i)` is pure integer arithmetic on
  the seed. It uses `SEED % 1000` and `floor(SEED/1000) % 1000`, so only the
  low 6 digits matter, and it draws no random numbers. It builds two
  permutations of 0..4 (`Table5`, `Table1`) and a row offset `RemT`. Each
  cell gets `5*Table5[(x+3y)%5] + Table1[(3x+y)%5] + 1`. This is an
  orthogonal Latin-square pair, so tiers 1..25 each appear exactly once, and
  all 12 lines (rows, columns, both diagonals) sum to 65. The script found 0
  exceptions in 2000 seeds. (`x`, `y`, `value` are implicit globals and
  `mirror()` is dead code. The `short`/`long` modes, v5:162–166, aren't used
  by SM64 Normal.)
- **Raster fill (v5:199–223):** cells 1..25 in order. One `Math.random()`
  per cell picks a start index in that tier's list (v5:201). The generator
  walks the list cyclically and takes the first goal with synergy 0, or the
  lowest-synergy goal if none has 0 (v5:209–219). There are no retries or
  restarts. The generator makes exactly 25 random draws.
- **Synergy (v5:171–192):** the goal's `types` are compared with every cell
  sharing a row, column or diagonal (`lineCheckList`, v5:89–113, 0-based
  cells). Only cells already filled count, because unfilled cells have
  `types` undefined (v5:175). So early cells never see later ones. Each
  shared tag scores +1, plus +1 if it is the candidate's first tag and +1 if
  it is the other cell's first tag (v5:180–185). **Tag order matters**, so
  store tags as an ordered list, not a bitmask. (`lineCheckList[13]` includes
  the center cell itself (v5:101); this is harmless because that cell is
  still empty.) In practice 1.35% of cells end with non-zero synergy.
- **Lockout on bingosync doesn't change generation.** The blackout variant
  (`srl_generator_v5_blackout.js`) checks all cells.

**How it plugs into `bingo_board_setup.c`:**
- Add a mode switch, for example a new `BINGO_PRESET_SRL_FAITHFUL` or a
  generator flag. At the top of `setup_bingo_objectives`, after the tracking
  reset, branch to `setup_srl_board(seed)` and skip the class template,
  weight tables, `switch_to` mutation and `deduplicate()`. Dedup has to be
  skipped for faithfulness. SRL relies on tier uniqueness plus synergy, and
  bingo64's dedup would replace legitimate SRL overlaps like "Red Coin Star
  in BOB" with "All Stars in BOB".
- Goal table as `const` data, 105 rows:
  `{ tier, name, u8 tags[4] (ordered, 27 distinct tags → u8 ids), objective type, params }`.
  Params: course, star index, N, K, and a list/mask id. Add
  `bingo_objective_init_fixed(obj, type, params)` beside
  `bingo_objective_init`, so the class-random init functions are bypassed.
  Then compute the title and icon as usual.
- The existing preset toggles (`gBingoObjectivesDisabled`) don't apply. The
  goal table replaces them.
- Online: the seed is already shared, so both clients generate the same board.
  A new generator or mode is a generation change, so it **requires a
  protocol bump**, and the mode flag must travel in the options message.

**RNG:** bingosync calls `Math.seedrandom(SEED)` (v5:84; an ARC4-based
generator, v5:1–67) with the seed as a **string**
(`bingosync/generators/bingo_generator.py:65–66`, "the generator *actually*
treats the seed as a string"; a non-string would get `"\0"` appended, v5:35).
bingo64 uses MT19937 (`init_genrand` in `src/engine/rand.c`), with seeds up
to 999,999,998.
- Under MT19937, the **magic square matches bingosync** for any seed with
  the same low 6 digits, because it uses no RNG. The **goal picks won't
  match.**
- Does it matter? Not for play: both bingo64 clients agree, and nobody plays
  against a bingosync board. It matters only if someone wants to check a
  bingo64 board against bingosync's for the same seed, or use node as a
  golden-test oracle.
- A byte-exact port is feasible and small: about 60 lines of ARC4 key
  schedule, plus 25 draws built from doubles, which are cheap on N64 at
  that count. Seeds would have to be decimal strings, ideally in bingosync's
  1..1,000,000 default range. It would make the node generator a free golden
  test.
- **Recommendation:** port seedrandom for this mode only. Other modes keep
  MT19937.

## 4. Interactions with "Unlock game" (`gBingoFullGameUnlocked`)

SRL races **assume a fresh file**: 0 stars, closed doors, closed cannons,
vanilla acts, 4 lives. The difficulty tiers price in door progression. For
example, "Top Floor Toad Star" needs 35 stars and sits at tier 23. A
faithful mode should therefore **force unlock OFF**, as the SRL preset
already does. What unlock ON (`unlock_full_game`, `save_file.c:49`) breaks:

- **Cannon ladder (5 goals):** `save_file_is_cannon_unlocked` returns TRUE
  for every course, so Buddies have nothing to open. These goals are
  meaningless with unlock ON.
- **Toad and MIPS stars:** the stamp writes `flags = 0x1F10FFCF`, which
  **sets `SAVE_FLAG_COLLECTED_TOAD_STAR_1..3` and `MIPS_STAR_1..2`** (bits
  24–28). With unlock ON, Toads give the "already gave it" dialog and MIPS
  stays empty-handed. So "Top Floor Toad Star" is **impossible** with unlock
  ON, and "12 Castle Secret Stars" is too, because only 10 castle secrets
  remain obtainable. The planned Toad/MIPS objectives must either require
  unlock OFF or clear those bits.
- **Star-count gates:** the Toad gates (12/25/35) and MIPS gates (15/50) read
  the save-file total. With unlock ON that total is the 120 stamped stars.
  bingo64 already works around this for Yoshi (`yoshi.inc.c`) by gating on
  `bingo_get_star_count()`. `MULTISTAR`, `STARS_MULTIPLE_LEVELS` and
  `STARS_IN_LEVEL` count **race** stars (`gbCourseStars`, cleared in
  `setup_bingo_objectives`), so "30/35 Total Stars" works under either
  setting. Only the gates themselves differ.
- **Roof:** with unlock ON, `ROOF_WITHOUT_CANNON` needs its "without cannon"
  clause. With unlock OFF on a fresh file, SRL's plain "Reach the Castle
  Roof" is the same goal.
- **Acts and objects:** with unlock OFF, vanilla act gating applies
  (`level_script.c:610`, DDD sub/pole, castle boos, star select). That's
  faithful to SRL. The synergy plan §2 asks for an audit that every
  existing objective stays completable with unlock OFF. For the 66 EXACT
  goals that holds by construction, because SRL itself plays them on
  vanilla rules.

**Fresh-file gap:** unlock OFF does **not** reset the save today.
`lvl_update_obj_and_load_file_selected` only runs `unlock_full_game()` when
unlock is ON, and saves persist to EEPROM or text saves. So an unlock-OFF race
inherits whatever the chosen slot holds. Worse, `unlock_full_game` writes
`gSaveBuffer.files[0][0]` (slot 1), so a slot-1 race after an unlock-ON race
starts with the 120-star stamp: in the same session because the RAM buffer
keeps it, and across sessions once it has been saved. A faithful mode (and
arguably any unlock-OFF race) should erase the selected slot in memory at
race start (`save_file_erase`-equivalent, without an EEPROM write). Race tracking (`gbCourseStars`) is already reset.
