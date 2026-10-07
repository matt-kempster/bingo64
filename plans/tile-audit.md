# Tile audit, 2026-10-06

Every tile checked against sm64.sql (placed objects) and the behavior code,
not the comments, after the blue coin miscount (29ab605f9). Three read-only
passes: enemies, collectables, stars/coins/progression. Nothing here was
checked in-game.

## Fixed (29ab605f9, a6fd98a64)

| Tile | Was | Now |
|---|---|---|
| Blue coins | "~160 sources, every goomba drops one" | 99; only THI's 11 huge goombas. Merry-go-round boos' 5 coins counted as 1; fixed |
| Coinless star | excluded BOB Koopa the Quick by mistake, dealt Chain Chomp's Gate | excludes Chain Chomp's Gate |
| Coins in BitS | up to 76 | up to 71 (72 placed, 1 unreachable) |
| Shoot cannons (centre) | 11-20; 20 impossible with unlock OFF | 11-18 |
| Mr. Blizzards | 3-5 of "7" | 3-4 of 5 killable (CCM's jumping two can't die) |
| 1-ups in a course | HMC 4, TTM 10 | HMC 2, TTM 8 (placed only) |
| Unique deaths | pit falls never counted | counted as "Fell out" |
| Random stars | flags survived a rematch | reset each race |

Plus comment/table corrections: bullies 17, Mr. Is 8, castle boos 10, Koopa
shells 7, ! boxes 65, red coins (no PSS), hat kinds in TTM 2.

## Open: Matt's call

1. **Zero-slack targets.** Possible, but the top target is the whole supply:
   Chuckyas 5/5 (needs RR and BitS), Koopas 3/3 (BOB's only exists in acts
   3-6), Skeeters 4/4, Scuttlebugs 9/9 (one that falls off is gone until
   reload), Bullies 16/17. Suggest capping each one short.
2. **Unlock OFF sizing.** Only the three progression tiles know about a
   fresh file. Still dealt there: Bowser 3 (70 stars), THI race, WDW/THI
   secrets, stars in 11-15 courses, anything in RR/TTC/BitS/WMotR, hat loss
   x3-4 (in the EASY table), wing cap boxes x9. Completable, but far past a
   ~12-star race. Needs a design pass.
3. **Daredevil pool (old code).** `course == DDD && (star != 1 || star != 4)`
   is always true, so all of DDD is excluded; the HARD JRB switch has no
   breaks, so it is always star 5. Unclear what was intended (DDD 1 and 4
   are underwater at 1 HP), so left alone.
4. **Tiny goombas** that die by touching Mario are not counted as kills.
   Intended ("requires action") or not?
5. **100% coin sweeps** in BitDW / BitFS / PSS / WMotR can be dealt (hard
   class, max fraction 1.0). Suggest ~0.85-0.9.
6. **Z-button challenge**: WF and HMC 100-coin stars have ~1-4 coins of
   slack; BOB 5, SL 5, THI 4/5 need clips. Keep or drop?
7. **Roof without cannon** is detected by height only (y > 2338 on castle
   grounds); the corner hills may reach it. Needs an in-game check.

## Matt's answers, 2026-10-06

- 1 zero-slack targets: roughly fine, leave.
- 2 unlock OFF: Bowser 3 on a fresh file is "funny but maybe not horrible"; no design pass for now.
- 3 Daredevil: excluding all of DDD is intentional (can't swim at 1 HP without dying). The JRB switch without breaks is still open.
- 7 roof without cannon: a little sad, OK for now.
- Still open: 4 tiny goombas, 5 100% coin sweeps, 6 Z-button stars.

## Not verifiable from code

Timed-star times, the A-button pool, click-game table, green demon on
underwater stars, which stars satisfy cannon stars, and BitFS 1-ups (7
placed, 2 need the Bowser key; table says 6).
