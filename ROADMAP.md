# bingo64 roadmap

The north star: port bingo64 to better surfaces, and add many more
objective types. This file collects the release TODO list and the
planned-objectives list (originally from Discord, 2022) so work can be
checked off in one place.

## TODO list

### Next release (v0.11a)

- [ ] Bully count fix *(likely fixed by PR #4 — confirm and check off)*
- [x] Redraw the unique-deaths skull icon (current one is a placeholder)
- [ ] Different game modes (easy, medium, hard, blind, classic bingo)
- [ ] Rando stars bingo
- [ ] On option select screen, use [R], [B], etc.
- [x] More objective types
- [x] Fix SA not counting towards star total bug
- [x] Classic bingo mode (including intro cutscene)
- [x] Allow bingo disabling during bowser stars
- [x] Fix wallkicks duplication icon typo

### Next version (v1.0)

- [ ] Make Mario say "Bingo"
- [ ] Build your own board
- [ ] Webapp (bingosync)
- [ ] Decomp hack progress display plugin integration
- [ ] Twitch plugin? OBS plugin?

### Things I may or may not do

- [ ] PC port release (v0.12)
- [ ] Press L: Big board, little board, off (?)
- [ ] Soft reset (v0.11)
- [ ] Reverse joystick AND camera (?)
- [ ] No more Mario head (v0.11)
- [ ] Make bingo sound effects have priority (?)
- [ ] Option to disable certain levels (v0.11)
- [ ] SFX for cursor (?)
- [ ] Transparent icon fixes (?)

### In the far future

- [ ] Online capabilities (netplay)

## Planned objectives

Items marked done are implemented but may not be in a release yet.

### Stars

- [ ] Collect N stars in 1 course
- [ ] Collect N 100c stars
- [ ] Collect N red coin stars

### Coins

- [ ] Collect N blue coins
- [ ] Collect coins from special stages
- [ ] Collect underwater coins

### Challenges

- [ ] Coinless/Capless/Cannonless stars
- [ ] Stars w/ special triple jump (especially ones that are harder
      because of it)
- [ ] ABZ challenge

### Enemies

- [ ] Kill all enemies of a type in one stage
- [ ] Kill all Boos in the courtyard
- [ ] Kill Lakitus
- [ ] Kill Fly Guys
- [ ] Kill Koopa Troopas
- [x] Kill Chuckyas
- *There are a lot of these...*

### Items

- [ ] Ride on all shells (including underwater shells)
- [ ] Use N unique spinning hearts (there are 13) *(PR #6 open)*
- [ ] Use N teleports
- [ ] Get lives counter to read N
- [ ] Break temporary boxes/crazy boxes
- [x] Collect N secrets

### Misc

- [ ] Swim with a cork box
- [x] Die in N unique ways
- [ ] Grab trees
- [ ] Get all 3 toad stars

### Bingo modifiers

- [ ] Blindfold/Strobe-light mode (black out the game every few
      seconds)/Low-vis mode
- [ ] Find the randomized silver star (per main course)
- [ ] Switch stars from DS
- [ ] Mario Cam Only
- [ ] 4 directional input
- [ ] Always being pushed forward/backward
- [ ] Cartridge tilt/corrupt animations
- [ ] Render Mario several units away from where he actually is
- [ ] Stripe updating mode
- [ ] Nonstop mode
- [ ] Splatoon mode (paint triangles you land on, paint N% of the
      level) (actually I think this is impossible)
- [x] Random Route Red Coins

### Options

- [ ] Toggle how to open bingo board
- [ ] Hard/medium/easy
- [ ] Long & short setting
- [ ] MetaBingo (each square is a bingo board)

## How the test suite supports this

The in-repo tests (PR #7, warp seam in PR #8) exist to make all of the
above safe to build quickly. The short version:

- **New objective type** → follow the checklist in `test/README.md`:
  one small simulation test, re-bless the golden boards, run the
  emulator tests.
- **Act-specific or modifier objectives** → the warp seam
  (`gTestWarpRequest`) jumps a test straight to any course's star
  select screen, where scripts can pick acts and bingo modifiers.
- **PC port** → `test/host` already runs the bingo logic on x86-64
  with plain gcc; keep it green and the logic survives the port. The
  emulator tests pin N64 behavior as the reference.
- **Game modes / board generation changes** → golden boards plus the
  10k-seed invariant sweep catch unintended generation changes.
