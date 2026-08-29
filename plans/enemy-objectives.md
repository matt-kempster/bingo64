# Enemy/object objectives — roster and course-bias plan

Goal (Matt, 2026-08-29): eventually one objective type per enemy. Weights
should make all 15 main courses equally likely targets of a board, and all
9 special courses equally likely among themselves — no required relation
between the two sets. Course attribution for global counters is
supply-proportional (an enemy that only exists in WF pulls boards toward
WF with its full weight).

Supply counts below come from the sm64.sql database
(~/git/sm64.sql/web/sm64.db, placed objects + macro objects joined to
courses) and cross-check exactly against the bestiary totals in
`src/game/bingo_tracking_collectables.c`. Analysis/simulation tool:
`test/board_gen_bias.py` (replays the generator, attributes cells to
courses). The aggregate fingerprint expect test is
`test/host/golden/weighting.txt` (blessed via `UPDATE_GOLDENS=1 make test`).

Course-bias context (default preset, expected cells per 25-cell board,
before the additions): overweighted BOB 1.69, TTC 1.29 (mostly the
TTC-random-clock objective); starved WF 0.85, JRB 0.83, DDD 0.68,
BBH 0.98; specials near-invisible (SA/TotWC/PSS/CotMC ≈ 0.15).

## Wave 1 — implemented 2026-08-29 (weights provisional, pre-balance-solve)

| Objective | Supply (unique kills) | Why |
|---|---|---|
| KILL_WHOMPS | WF 2 + King + BitS 1 = 4 | WF deficit |
| KILL_BOOS | BBH ~13, castle courtyard ~11 | BBH deficit; big supply |
| KILL_SNUFITS | HMC 4, CotMC 4 | only enemy in a cap course |
| HURT_BY_CLAMS | JRB 5, DDD 4 | only object in both starved water courses |
| KILL_FLY_GUYS | THI 3, SSL 3, TTM 1, SL 1, RR 1 | widest spread |
| KILL_MR_BLIZZARDS | SL 4, CCM 3 | variety |
| KILL_SKEETERS | WDW 4 | low weight (WDW already hot) |
| KILL_KOOPAS | BOB 2, THI 3, minus the 2 Koopa the Quicks ≈ 3 | low weight (BOB/THI hot) |

## Crushers — proof done 2026-08-29, whitelist final (Matt approved)

"Get crushed by N different crushers." Whitelist: bhvThwomp, bhvThwomp2,
bhvGrindel, bhvHorizontalGrindel, bhvSpindel, bhvToxBox (proof found it
as a false negative), bhvSmallWhomp, bhvWhompKingBoss (Matt: in).
Supply: WF 5 (2 thwomps, 2 small whomps, King), TTC 1 (thwomp), SSL 7
(grindel, 2 horizontal grindels, spindel, 3 tox boxes), BitS 1 (small
whomp) -> 14 crushers total, MAX_CRUSHERS 16.

Proof highlights (full report in session 2026-08-29): static geometry
can never squish (INPUT_SQUISHED requires a dynamic surface, mario.c
~1404); the crushing surface's `->object` is the crusher or NULL, no
third state; read the CEILING side only (floor-side check misattributes
elevator-on-thwomp-platform crushes); none of the whitelisted behaviors
has a damage hitbox, so squish entry is the single detection point;
credit at the four ACT_SQUISHED entry sites (airborne/stationary/
object/moving cancels) fires exactly once per squish and before any
death. Known quirk: metal-cap Mario enters ACT_SQUISHED damage-free ->
riskless farming (accepted). Whomp crush credit is disjoint from the
KILL_WHOMPS objective (slam vs ground-pound-kill).

## Tabled (noted for later waves)

- **Sushi** (DDD 2) — hurt-by ("get bitten by Sushi"); DDD still needs it.
- **Bubba** (THI 2) — he swallows you whole; fits the unique-deaths
  death-flag family ("get eaten by Bubba"), not a counter.
- **Haunted furniture** (BBH: 3 chairs, 7 bookends, mad piano,
  bookshelf) — attackable, would deepen BBH further.
- **Piranha Plants** (WF 3 sleeping; fire piranhas THI 6, BitS 2).
- **Bullet Bill** (WF only) — killable by punch; WF-pinned.
- **Monty Moles** (TTM 9 holes, HMC 3 holes — key UIDs on hole
  positions; moles respawn from holes).
- **Heave-Hos** (WDW 3, TTC 1) — hot courses, low weight if added.
- **Pokeys** (SSL 4), **Klepto** (SSL), **Tox Box** (SSL 3, maybe a
  crusher), **Tweesters** (SSL 3) — SSL is already the #3 course; all
  low weight.
- **Enemy Lakitu** (RR 2, THI 1), **Chain Chomp** (BOB 1), **Wiggler**
  (THI 1), **Moneybags** (SL 2 hidden), **Unagi** (JRB — already has
  star/red-coin coverage).
- **Flames / flamethrowers** hurt-by ("get burned"): LLL 8, RR 5,
  HMC 20, BBH 7, Bowser courses 8, Castle 13 — good spread including
  specials; amp-style.
- Specials balance note: SA/TotWC/PSS/WMotR have essentially no enemies —
  their equalization must come from course-pinned objective weights
  (coins, reds, 1-ups, poles, secrets), not the enemy roster.

## Idea mine: SM64 Video Quiz (researched 2026-08-29)

The "Super Mario 64 Video Quiz" (2006-c.2010, sequel after) was
mr_roberts_z's GameFAQs competition: ~163 performed-on-video tasks
(~10 per main course), points by speed/completion, TAS banned.
Brightguy (Curtis Bright, sm64.com) archived every task and ranking:
https://www.sm64.com/videoquiz/ (all tasks:
tasksd52c.xhtml?course=all). Leaderboard names are the early-ABC crowd
(BikdipOnABus's entry videos are still on YouTube).

Objective ideas mined from the task corpus, deduped against our roster
("VQ" = source task; all judged code-hookable unless noted):

New kill classes: Monty Moles (HMC/TTM, VQ HMC 1/9), Swoops (HMC/BBH,
VQ HMC 5), enemy Lakitu (RR/TTC, VQ RR 5/6), Bookends — bounce-kill
variant (BBH, VQ BBH 5), Moneybag chase-down (SL, VQ SL 3), Piranha
Plants (WF/THI, VQ WF 5, THI 8), Eyerok hands (SSL, VQ SSL 8 — grants
a star, check overlap).

Odd interactions (the Quiz's specialty): ground-pound Dorrie's head
(HMC, VQ Misc 6); aggro the Mad Piano and escape unhurt (BBH); catch
and hold a Ukiki (TTM, VQ TTM 2); spawn the butterfly 1-up (TTM, VQ
TTM 5; evil variant: survive the butterfly bomb); carry the cork box
up BoB's mountain (VQ BoB 5); ride the rolling log a full revolution
(LLL/TTM, VQ LLL 5); Crazy Box full-height bounce (SSL/LLL/TTM, VQ SSL
1); kill an enemy by swatting with your own lost cap in hand (SL/SSL/
TTM, VQ SL 5); reactivate WDW downtown's dormant fire spitters and get
burned (VQ WDW 15, their most obscure task); full-heal at a spinning
heart in one pass (WF/TTC/CotMC/BitFS, VQ WF 3).

Places/traversal: under the BBH merry-go-round (VQ BBH 10); swim under
the CCM mother penguin (VQ CCM 9); lava-boost up the volcano lavafall
(LLL, VQ LLL 9); use a hidden warp (warp-trigger id hook, several
courses); stand on both TTC clock hands (VQ TTC 3); star with the
clock stopped at 12 (TTC); ride a flying carpet's full route (RR).

Meta/constraint twists: their "Click Game" formats predate ours —
close-cam-only star or fixed-cam star with N recenters (VQ WF 9 etc.);
capless "cap stars" (Jet Stream / Wings to the Sky / Red Hot Log
Rolling without the cap — star id + no-cap-flag hook, VQ JRB 1, BoB 9,
LLL 2, SSL 9).

Brush-against-existing (harder flavors only): homing-1-up outrun (BoB,
VQ BoB 3), cannon-to-cannon chain (VQ BoB 6), carry Chuckya into the
WDW pipe (VQ WDW 6), all-141-coins WF (VQ WF 1), KtQ 0'00" timer
glitch (VQ BoB 1).

## Next steps

1. Crusher proof verdict -> implement or amend whitelist.
2. Two-tier flatness solve: pick weights so main courses are flat and
   special courses are flat (`test/board_gen_bias.py` has the vectors;
   solve, then transcribe into the C weight tables).
3. Real icons (wave 1 shipped with placeholder textures, marked
   `TODO(matt)` in bingo_objective_info.c).
4. Protocol note: the enum insertions change board generation and the
   objective wire numbering — next netplay release needs the usual
   protocol bump (beta.N = protocol N).
