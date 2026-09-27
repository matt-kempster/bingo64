# Kinds audit: "Defeat N kinds of enemy" + "Carry N kinds of thing" (2026-09-27)

Research only, no code. Supply = sm64.sql placed objects (object + macro +
special), plus known spawners. Follows plans/horizontal-objectives.md.

## 1. Enemy kinds

Proposed grouping rule, one sentence for the checklist: **"Big, King and
Chill versions count as the regular enemy."** That matches how the existing
kill tiles already count (King Whomp is a Whomp, Big Boos are Boos, Big
Mr. I is a Mr. I, Big Bully is a Bully). Bosses with no small version are
their own kind.

Result: **26 kinds.** 14 already have a kill hook; 12 need one.

| # | Kind | Behaviors | How it dies | Supply (course: count) | Kill hook today |
|---|---|---|---|---|---|
| 1 | Goomba (all sizes) | bhvGoomba (+ bhvGoombaTripletSpawner x3) | stomp, punch, kick, ground pound | THI 18+3, SSL 9+3, BitDW 6, BoB 2+9, TTM 9, SL 3, BitS 2+6, BitFS 3, JRB 3, RR 1 | YES goomba.inc.c:324 |
| 2 | Bob-omb (+ King Bob-omb) | bhvBobomb, bhvKingBobomb | any explosion (thrown, kicked, or its own fuse!); King: thrown 3x | BoB 12 + King, TTM 5, RR 4, BitS 4, SSL 2, TTC 2, BitFS 1 | Bob-omb YES bobomb.inc.c:31 (bobomb_spawn_coin); King NO: king_bobomb_act_7 |
| 3 | Koopa (shell-less) | bhvKoopa | knock shell off, then hit | BoB 1, THI 2 (excl. 2 Koopa the Quicks) | YES koopa.inc.c:116 |
| 4 | Boo (+ Big Boos) | bhvBoo, bhvGhostHuntBoo, bhvGhostHuntBigBoo, bhvBalconyBigBoo, bhvMerryGoRoundBoo/BigBoo, bhvCourtyardBooTriplet, bhvBooWithCage | hit from any side | BBH ~20 incl. 3 Big Boos, courtyard 9 + cage Boo | YES boo.inc.c:53 (boo_bingo_died: small, big, cage) |
| 5 | Bully (+ Big, Big Chill) | bhvSmallBully, bhvBigBully, bhvBigBullyWithMinions, bhvBigChillBully | pushed into lava / icy water | LLL 7+3 minions+2 Big, BitFS 4, SL 1 Chill | Small + Big YES bully.inc.c:216/229/315; **Big Chill NO** (chill branch of bully_act_level_death skips it) |
| 6 | Chuckya | bhvChuckya | thrown / thrown into | WDW, TTM, THI, RR, BitS 1 each | YES chuckya.inc.c:239 |
| 7 | Whomp (+ King Whomp) | bhvSmallWhomp, bhvWhompKingBoss | ground pound back | WF 2 + King, BitS 1 | YES whomp.inc.c:283/287 (both) |
| 8 | Mr. I (+ Big Mr. I) | bhvMrI | circle it | BBH 4 (1 big), HMC 2, LLL 2 | YES mr_i.inc.c:159 (both) |
| 9 | Snufit | bhvSnufit | any attack | HMC 4, CotMC 4 | YES snufit.inc.c:174 |
| 10 | Fly Guy | bhvFlyGuy | stomp / attack | SSL 3, THI 3, TTM, SL, RR 1 | YES fly_guy.inc.c:225 |
| 11 | Mr. Blizzard | bhvMrBlizzard | attack | SL 4, CCM 3 | YES mr_blizzard.inc.c:186 |
| 12 | Skeeter | bhvSkeeter | stomp / attack | WDW 4 | YES skeeter.inc.c:166 |
| 13 | Scuttlebug | bhvScuttlebug (+ bhvScuttlebugSpawn) | stomp / attack | HMC 4 + 2 spawners, BBH 3 | YES object_helpers.c:2369 (die_if_attacked_bingo) |
| 14 | Spindrift | bhvSpindrift | stomp / attack | SL 14, CCM 5 | YES same helper |
| 15 | Piranha Plant (+ fire) | bhvPiranhaPlant, bhvFirePiranhaPlant | sneak-hit (sleeping); fire: any attack | WF 3; THI 6, BitS 2 | NO: piranha_plant_act_shrink_and_die; fire: fire_piranha_plant.inc.c:135 |
| 16 | Pokey | bhvPokey + bhvPokeyBodyPart | **hit the head** (body segments regrow) | SSL 4 | NO: head hit, pokey.inc.c:114 (oPokeyHeadWasKilled) |
| 17 | Swoop | bhvSwoop | any attack | HMC 11 | NO: generic death (see hook note) |
| 18 | Monty Mole | bhvMontyMole (+ holes) | stomp / attack; respawns forever | TTM 2 (9 holes), HMC 1 (3 holes) | NO: generic death |
| 19 | Lakitu (enemy) | bhvEnemyLakitu | hit from below / attack | RR 2, THI 1 | NO: generic death |
| 20 | Spiny | bhvSpiny (thrown by Lakitu) | punch / kick only (stomp does nothing) | RR, THI (spawned) | NO: generic death (knockback path) |
| 21 | Moneybag | bhvMoneybag (from bhvMoneybagHidden) | stomp / attack | SL 2 (hidden as a coin) | NO: moneybag_act_death |
| 22 | Bookend | bhvFlyingBookend (+ bhvBookendSpawn, bhvBookSwitch) | attack | BBH 3 + 4 spawners | NO: generic death |
| 23 | Haunted Chair | bhvHauntedChair | attack | BBH 3 | NO: generic death |
| 24 | Wiggler | bhvWigglerHead | 4 stomps | THI 1 | NO: wiggler_act_shrink |
| 25 | Eyerok | bhvEyerokBoss / bhvEyerokHand | both hands killed | SSL 1 | NO: boss star spawn in eyerok.inc.c |
| 26 | Bowser | bhvBowser | thrown into a bomb | BitDW, BitFS, BitS | PARTIAL: BINGO_UPDATE_BOWSER_KILLED fires in BitS only (bowser.inc.c:1166/1292); key fights need the other branch of bowser_spawn_collectable |

**Not killable (never a kind):** Amp, Chain Chomp (freed, not killed),
Klepto (struck, flies off, never dies), Bullet Bill (a punch spins it
away, it respawns), Mad Piano (health 99), Bubba, Unagi, Sushi, clams,
Heave-Ho, Ukiki, Koopa the Quick, castle-lobby Boo (bhvBooInCastle, no
hitbox), Thwomp/Grindel/Spindel/Tox Box, water bombs, bowling balls,
flames/fire spitters, Tweester, manta, Dorrie, penguins, butterflies,
coffins. Unused: bhvSmallChillBully (never placed).

### Hook note (the 12 missing kinds)

Swoop, Monty Mole, Lakitu, Spiny, Bookend and Haunted Chair all die in
`obj_die_if_health_non_positive` (obj_behaviors_2.c:651). One hook there,
switched on `o->behavior`, covers all six. **Trap:** the same function is
reached from `obj_die_if_above_lava_and_health_non_positive` (an enemy that
wanders onto lava or sinks in water dies with no Mario involvement). Credit
only the attack paths (obj_handle_attacks / obj_check_attacks / knockback /
squish), not the lava one. Pokey body segments also die there; key Pokey
on the head kill only. The other six (King Bob-omb, Big Chill Bully,
piranhas, Moneybag, Wiggler, Eyerok, Bowser key fights) each get one line
at the site listed above.

### Hard-to-detect / odd cases

- **Bob-omb self-explosions already count.** bobomb_spawn_coin runs on any
  explode, including a lit Bob-omb blowing up next to Mario. Existing tile
  behaves this way; the kinds tile inherits it (lenient, not a cheat risk).
- **Bullies die by falling**, so a bully that chases Mario off an edge
  counts. Same as today.
- **Klepto and Bullet Bill look defeated but aren't.** Most likely source of
  "I killed it and it didn't count". The checklist is the answer (they're
  not on it).
- **Spiny** can't be stomped; only punch/kick. Spawn rate is Lakitu-driven.
- 26 names is long for the two-column checklist under the description;
  check it fits before building (or use short names: "Mr.Bliz", "Scuttle").

## 2. Carry kinds

Detection: **one hook in `mario_grab_used_object` (interaction.c:301)**, in
the `heldObj == NULL` branch, switched on `usedObj->behavior`. Every grab
funnels through it: ground pickup (mario_actions_object.c:189), Bowser
(mario_actions_object.c:321), dive grab (mario_actions_airborne.c:792),
dive-slide (mario_actions_moving.c:1684), water grab
(mario_actions_submerged.c:791). Being grabbed BY Chuckya / King Bob-omb /
Heave-Ho never sets heldObj, so it can't false-positive. Ukiki needs both
bhvUkiki and bhvMacroUkiki. interaction.c is a vanilla-by-construction
file; this is a normal bingo-hook graft.

Full grabbable list from behavior_data.c (INTERACT_GRABBABLE /
OBJ_FLAG_HOLDABLE): the 11 below plus bhvBetaHoldableObject (unused) and
Bob-omb Buddy (HOLDABLE flag but INTERACT_TEXT, not grabbable). **Nothing
missing from the list.**

| Kind | Behavior | Where (count) | Unlock ON | Unlock OFF | Verdict |
|---|---|---|---|---|---|
| Bob-omb | bhvBobomb | BoB 12, TTM 5, RR 4, BitS 4, SSL 2, TTC 2, BitFS 1 | yes | yes | keep |
| Cork box | bhvBreakableBoxSmall | BoB 2, WF 2, SSL 1 | yes | yes | keep |
| Crazy box | bhvJumpingBox | SSL 2, BBH 1, LLL 1, TTM 1 | yes | yes | keep |
| Underwater shell | bhvKoopaShellUnderwater | JRB 1, DDD 1 (sub area) | yes | yes | keep (water grab) |
| Chuckya | bhvChuckya | WDW, TTM, THI, RR, BitS | yes | yes | keep (grab from behind) |
| King Bob-omb | bhvKingBobomb | BoB act 1 only | yes | yes | keep |
| Bowser | bhvBowser | BitDW, BitFS, BitS | yes | yes (8 stars) | keep |
| Ukiki | bhvUkiki / bhvMacroUkiki | TTM: cap Ukiki all acts, cage Ukiki act 2 | yes | yes | keep |
| Baby penguin | bhvSmallPenguin | CCM 2, all acts, no star gate in code | yes | yes | keep |
| **Heave-Ho** | bhvHeaveHo | WDW 3, TTC 1 | - | - | **drop**: INT_SUBTYPE_NOT_GRABBABLE. Only the water-grab glitch picks one up (WDW with water raised) |
| **MIPS** | bhvMips | castle basement | **NO** | 15 stars (and again at 50) | **drop**: the unlock stamp (flags 0x1F10FFCF) marks both MIPS stars collected, so bhv_mips_init deactivates him |

That leaves **9 kinds**. Of those, 5 live in 3 courses (BoB: Bob-omb,
cork box, King; TTM: Ukiki, crazy box), so N around 3-5 is plenty.

## Recommendations

1. Adopt the family rule for enemies (Big/King/Chill = regular). 26 kinds.
2. Build the enemy hooks in two passes: the generic obj_die_if_health_non_positive
   hook (6 kinds, attack paths only), then the 6 bespoke sites. Also fix
   the Big Chill Bully gap in the existing Bully tile (or rule it out
   explicitly; today it silently doesn't count).
3. Carry: ship with 9 kinds, drop Heave-Ho and MIPS. One hook.
4. Test list: every enemy kind once (PC state injection), plus
   carry-each-kind; include a lava-walk Goomba/Swoop to prove no false credit.

## Open questions for Matt

1. **Family rule OK?** Specifically King Bob-omb = Bob-omb (today's Bob-omb
   tile doesn't count King), and fire piranhas = Piranha Plant.
2. **Bowser: any of the three fights, or BitS only?** Recommend any.
3. **Spiny separate from Lakitu?** Different object, same course; separate
   gives RR/THI two kinds from one encounter.
4. **Bookend + Haunted Chair separate?** Both BBH; could merge as one
   "haunted furniture" kind (piano excluded, it can't die).
5. **Carry: drop Heave-Ho and MIPS** (glitch-only / impossible with
   unlock ON)? Alternative for MIPS: unlock-OFF-only kind, but then the
   kind list differs by mode.
