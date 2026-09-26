# Horizontal objectives + new-aspect tiles (2026-09-26)

Everything here rests on **well-definedness**: a player must be able to tell,
without a ruling, whether a thing counts. Supply numbers: sm64.sql
(~/git/sm64.sql/web/sm64.db: placed objects + macro + special objects).

## The grid

Vertical = N of one thing. Horizontal = breadth: N places or N kinds.

| Resource | Total | One course | N courses | N kinds |
|---|---|---|---|---|
| Stars | N stars total | N stars in X | K stars in each of N courses | REJECTED (see below) |
| Coins | N coins total | N coins in X | **gap: K coins in each of N courses** | too thin (3 colours) |
| Lives / 1-ups | life counter to N | N 1-ups in X | **gap: a 1-up in each of N courses** | 1-up sources: fuzzy |
| Enemies | one tile per enemy | - | - | **gap: defeat N kinds of enemy** |
| Caps | boxes per colour | - | - | **gap: wear all 3 caps** |
| Deaths, hat | - | - | - | ways (exist) |
| Movement | - | - | BLJ, wall kicks (exist) | - |

## Accepted (Matt, 2026-09-26)

BUILT 2026-09-26 on overnight-0923: coins in N courses, 1-up in N courses,
all 3 caps, purple switches, stuck in the ground (types 65-69). Still to do:
enemy kinds, throwers.

- **K coins in each of N courses.** Twin of "K stars in each of N
  courses"; coins already count per entry. "Might be ok."
- **A 1-up in each of N courses.** Same shape. "Might be ok."
- **Wear all 3 caps** (wing, metal, vanish). N fixed at 3.
- **Defeat N kinds of enemy.** Good, but must cover EVERY killable enemy,
  or a player kills an uncounted one and feels cheated. Big lift; expect
  bugs. Needs its own audit (below) before any code.
- **Press N purple switches.** 13 switches over 10 courses: WDW 3, BitDW 2,
  DDD, JRB, TTM, THI, RR, HMC, BoB, BitS 1 each. Hook: purple_switch.inc.c.
- **Get stuck in the ground in N courses** (head / butt / feet). Only
  snow/sand areas: SL, CCM, SSL outside, WMotR. Rule in
  mario_actions_airborne.c should_get_stuck_in_ground: fall > 1000 units
  onto flat, soft, static ground. Unique courses so it can't be farmed.
- **Get thrown by N throwers.** Chuckya x5 (BitS, RR, THI, TTM, WDW),
  Heave-Ho x4 (WDW 3, TTC), King Bob-omb. Hard to communicate, but the
  crusher tile ("Get crushed by N unique Crushers") is the precedent.
  Inherits the known Chuckya UID quirk.

## Maybe (not yet discussed)

- **Carry N kinds of thing -- ACCEPTED (Matt, 2026-09-26).** Grabbables
  from behavior_data.c: Bob-omb, cork box, crazy box, underwater shell,
  Chuckya, Heave-Ho, King Bob-omb, Bowser, Ukiki, baby penguin, MIPS. Kind
  list is crisp (it's the grab interaction). Check each against unlock-all
  before building; some dialogs/events are disabled there.
- **Talk to N characters -- PARKED.** Matt: "fine, not crazy about them".
  Many talk dialogs are intentionally disabled under unlock-all (Hoot etc.),
  and it feels like signpost reading. Needs a strict list if revived.

## Rejected

- **N kinds of star**: "kind" is not well defined (is King Bob-omb's star a
  boss star or a box star?).
- **Swim through N rings**: DDD only (manta + jet-stream ring spawners);
  JRB's jet stream has no rings. It's just those two stars.
- **Get hurt N ways**: rejected earlier (not well defined).
- Breakable boxes (17 of 30 in WDW), butterflies (castle grounds + WF),
  warp pipes (6 of 8 in THI), wooden posts (BoB/THI only), Hoot / Dorrie /
  Tweesters (one course each), defeat N bosses (mostly duplicates stars).
- Swoops (HMC 11) belong in the enemy roster, not their own tile.

## Shared UI need: a checklist in the description

"Die in 3 ways" only says "Remaining: 2" (bingo_descriptions.c has a TODO).
Every N-kinds tile needs the description to list the kinds with checkmarks;
it also fixes the two existing ways tiles. Build this first.

BUILT 2026-09-26: bingo_objective_kinds() names the kinds; the board screen
draws two columns under the description, done kinds green (deaths, hat,
caps). A new kinds tile only needs a names list there.

## Enemy-kinds audit (before code)

Killable behaviors (sm64.sql: death/explode/die-if-attacked calls) not yet
tracked include Big Bully, Big Chill Bully, small chill bully, Eyerok,
Wiggler, Big Boo (ghost hunt / balcony / merry-go-round), bookends, haunted
chairs, Klepto, Moneybag, Pokey, Spiny, fire piranhas; the query misses
bespoke death paths (Swoop, Monty Mole, Enemy Lakitu, Piranha Plant,
King Bob-omb, King Whomp, Bowser...). Steps:
1. Enumerate every object Mario can damage (attack handlers, not just
   death calls) from the decomp; decide killable yes/no per behavior.
2. Rule on kind merges once, publicly (big/small/chill bully = Bully?
   King Bob-omb = Bob-omb? ghost-hunt Boo = Boo?). The checklist shows the
   rulings, which is what makes the tile well defined.
3. One BINGO_UPDATE event per kind, fired at the single death site;
   reuse the existing KILLED_* events where they exist.
4. Test every kind (PC state injection or a scripted run); expect bugs.
