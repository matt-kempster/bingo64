# Synergy objectives — live tiles, progression tiles, and parked ideas

Brainstorm with Matt, 2026-09-23. Starting point: "there is some sort of
missing 'oomph' here … something synergistic."

## Diagnosis

Every bingo64 tile is an independent errand. The formats with the most
energy get it from three things we lack:

1. **Overlap between tiles.** OoT bingo goals share work (one trip advances
   several goals), so choosing a line is route planning. The OoT generator
   models this explicitly: per-goal synergy values, min/max synergy per line
   (ootbingo/oot-bingo-generator `doc/BALANCING.md`). Our generator
   (`bingo_board_setup.c`) draws each cell independently from the weight
   tables; the only cross-tile check is duplicate rejection. The
   course-flatness work spreads tiles further apart.
2. **Progression.** With "Unlock game" ON (the default) nothing a player does
   early opens anything up later.
3. **Player interaction.** A lockout claim is permanent, and no tile refers to
   an opponent. The only contact is racing for the same square.

## 1. Live tiles — ACCEPTED DIRECTION (online only)

Idea (from Minecraft Lockout Bingo's "more X than the opponent" goals): a
square owned by whoever currently leads a running stat. It can change hands
until the game ends.

Candidate stats (all fed by normal play, so they overlap every other tile):
- Most stars collected
- Most coins collected (cumulative, not the HUD count — HUD resets per course)
- Most unique kills (reuses the bingo_tracking_collectables UID tables)
- Most unique deaths / most deaths (the comedy tile)
- Most red coins, most 1-ups, most cannons shot… anything we already count

Rules to settle:
- **Ownership:** held by the strict leader, unowned on a tie (in particular at
  0–0 at the start). Ownership freezes at game end.
- **Online only.** A live tile needs an opponent. This **forks board
  generation between online and offline for the first time**. The generator
  needs an `online` input (and maybe the mode): offline boards stay
  byte-identical, so the existing goldens are untouched, and online boards
  get their own goldens. Seeds are already shared per room, so both sides
  still derive the same board.
- **Which modes:** most interesting in lockout. In line modes a live tile
  counts toward the holder's lines, which works but is a weaker fit. Blackout
  (co-op) has no opponent, so no live tiles. Open question.
- **Lockout end condition:** the relay's uncatchable-lead check must treat
  every live square as winnable by anyone, and a claimed live square can be
  lost. So a game with live tiles may never become mathematically decided.
  This pairs naturally with the **FFA time limit** that is already on the
  wishlist (bingo-mode research): time runs out → live tiles freeze → count.
  Probably ship the two together.
- **Authority:** the relay owns live-tile ownership, just as it adjudicates
  lockout claims today. Clients report their counters (new message type);
  the relay recomputes the leader and broadcasts changes of ownership. The
  trust model is the same as for claims today.
- **Protocol:** new message + mode/generation change → protocol bump (bundle
  with the next one; beta.N = protocol N).
- **UI:** the owner-tile rendering from presence already exists. A live tile
  also needs to *look* live (e.g. a pulsing border or a small "crown" badge)
  and show the leader's value vs yours in the L-screen description. Icon and
  mini-text budget is tight: one icon per stat plus a shared live marker.

Related, not yet discussed: **opponent tiles** ("an opponent dies", "an
opponent loses their hat"). They don't require any action from you, so they
fail the objective design bar as stated. Live tiles capture the interaction
without that problem.

## 2. Progression objectives — ACCEPTED DIRECTION (tied to "Unlock game")

A group of objectives that is only eligible when **Unlock game is OFF**
(`gBingoFullGameUnlocked == 0`; the SRL preset already uses OFF). With unlock
ON they are disabled from generation, since they would be free or
meaningless.

What unlock ON currently opens (grep for `gBingoFullGameUnlocked`): cannons
(`save_file_is_cannon_unlocked`), all acts' objects (`level_script.c:610`),
castle boos, the DDD sub/pole, Yoshi's star count, star-select display.

Candidates (supply and feasibility to verify on a fresh unlock-OFF file):
- **Get MIPS**: 2 stars, gated at 15 / 50 stars. Probably only the first.
- **Toad stars**: 3 Toads, gated at 12 / 25 / 35 stars. The random-star
  picker already excludes Toad and MIPS (`random_star_except_mips_toad`).
- **Open N cannons via Bob-omb Buddies**: 11 cannon-opening Buddies (BoB,
  WF, JRB, CCM, SSL, WDW, TTM, THI, SL, RR, WMotR; sm64.sql). Only
  meaningful when cannons start closed, i.e. unlock OFF.
- **Press a cap switch / all three cap switches**, **get a Bowser key**,
  **drain the moat**, **move the DDD sub**: check which of these a fresh
  unlock-OFF file actually needs before listing them.

Implementation notes:
- The generator does not read the unlock flag today. This group makes unlock
  a generation input (it is already in the online options message, so both
  sides agree).
- Star-count gates interact with board length: a "Toad star at 35" tile is
  a long-game tile. Weight and class accordingly, or restrict to the lowest
  gate.
- Audit the reverse direction too: with unlock OFF, can every *existing*
  objective still be completed (act-gated stars, cannon objectives)?

Matt: this group also fits a future **faithful SRL mimic** mode. MIPS and
Toad stars are SRL goals, so progression tiles are part of that goal list.

## 3. Hat trick — PARKED

"Complete 3 other squares in a single course visit." It rewards reading the
board for co-located tiles (the overlap from the diagnosis).

Why parked: feasibility is board-dependent and can be **lost during play**.
Some tiles are one-shot (e.g. the 400th coin of a coin tile, a Bowser fight,
racing stars). If you complete one outside the visit, the chance to count it
is gone for good. **Principle (Matt): no square may become impossible.**

Possible salvage (not decided): count only repeatable progress, or have the
generator guarantee enough same-course repeatable tiles. Both are fragile.
Related generator-side idea that doesn't need this tile: cluster 2–3 hub
courses per board so co-located tiles exist, with an OoT-style per-line
synergy cap. Expected course flatness across many boards can still hold.

## 4. Combo tiles — PARKED

Cross-products of existing verbs: "kill an enemy while wearing the metal
cap", "collect a star while riding a shell", etc. They create overlap
automatically when the component tiles share a board.

Why parked: the icon (16x16) and mini-text budget can't obviously express
"A while B". One idea for later: the base verb's icon plus a small corner
badge for the condition (cap, shell). Needs a design pass before any code.

## Principles recorded this session

- **No impossible squares.** A square must never become impossible because of
  choices made earlier in the game. This applies to every new objective.
  (Existing star challenges are fine: "Failed!" only lasts for the current
  course visit.)
- **Icon / mini-text budget is a real constraint.** An idea that can't be
  drawn in 16x16 plus a few characters needs a design answer first.

## Backlog from the same session (plain new tiles, supply from sm64.sql)

- **Use N warp pads**: 24 fading-warp pads = 12 pairs over BoB 4, SSL 4,
  WF/CCM/LLL/WDW/TTM/THI/SL/RR 2 each. Count unique pairs (N 3–5).
- **Ride N Koopa shells**: 5 total. Land shells from ! boxes in LLL, SL, SSL;
  underwater in JRB and DDD. Stomped Koopa Troopas do NOT drop rideable
  shells. Hook: `interaction.c:1559`.
- **Touch N spinning hearts**: 13 total. BitFS 2, BitS 2, RR 2, TTC 2, and
  1 each in BoB, CCM, HMC, LLL, SSL.
- **Coinless star**: "collect star X without touching a coin", in the
  A/B/Z-challenge family. Exclude red-coin and 100-coin stars.
- Modifier candidate: **4-direction input** (pure input filter, like
  reverse joystick).
