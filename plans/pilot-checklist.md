# Pre-launch pilot checklist (v1.0)

Run top to bottom before tagging v1.0. Every line: **check -> expected**.
`[A]` = automated (command given; green = pass). `[M]` = manual, human eyes.
Manual part is sized for ~1 hour with one friend on two PCs (Windows exe).
Any red line = no launch. Record the tag/sha you ran against: `________`.

## 1. BUILD (preflight)
- [ ] [A] `grep PROTOCOL_VERSION server/relay.py` == `NET_PROTOCOL_VERSION` in `src/pc/network/network.h` (`test_protocol_versions_match`) -> equal (and the release name vs the beta.N = protocol N convention is decided).
- [ ] [M] Any board-generation change since last release (new tile, weight, re-blessed goldens)? -> protocol was bumped (seed-shared boards silently desync otherwise).
- [ ] [A] `cd test/host && make clean && make test` -> all pass, `board_gen_bias.py --check` OK (clean matters: no header deps).
- [ ] [A] N64 ROM in `~/b64-refresh` (TARGET_N64=1 GRUCODE=f3dzex, mips PATH + QEMU_IRIX) -> links; if it dies in `build/us/assets/`, rm the generated anim/demo files and retry.
- [ ] [A] `cd test/emu && make test-smoke test-ram test-warp test-splat test-starui` -> pass (`test-rando` is known broken since 2026-09-22; skip, don't count).
- [ ] [A] `cd web && make check` -> pass.
- [ ] [A] `test/pc/linuxbuild.sh` -> builds; fails loudly on a stale binary.
- [ ] [A] Before winbuild: no ELF `.o` in `~/b64-win/build/us_pc` (magic `7f454c46`) -> none; after a fresh-worktree rsync, `make -C ~/b64-win/tools` natively first.
- [ ] [A] `test/pc/winbuild.sh` -> exe builds and its `audit_texture_names.py` passes (no texture baked in as raw pixels).

## 2. SOLO (PC, offline, ~10 min)
- [ ] [M] Fresh profile, launch exe with extracted `res/` -> splash, title, FILE SELECT; no checkerboard textures anywhere.
- [ ] [M] 1 PLAYER -> OPTIONS: presets SRL / VANILLA / CASUAL each apply; editing a toggle shows custom -> preset label tracks.
- [ ] [M] Options page scrolls; hand cursor reaches every row (known limit rows 30..210) -> nothing unreachable.
- [ ] [M] Start a board, open the board view -> 25 icons, no checkerboard (esp. whomp/boo/snufit/clam/flyguy/blizzard/skeeter/koopa/thwomp).
- [ ] [M] Same seed twice -> identical board.
- [ ] [M] Vanilla policy: alo QoL switches all off by default.

## 3. EACH MODE (`enum BingoGameMode`, solo unless noted)
- [ ] [M] LINE_1: complete one row -> win banner. [A] host tests cover `bingo_mode_line_target`.
- [ ] [M] LINE_2 / LINE_3: first line does NOT win; Nth line does.
- [ ] [M] BLACKOUT: board requires all 25 (spot-check counter/HUD, don't play it out).
- [ ] [M] LOCKOUT (online, 2 players): a cell claimed by one is locked for the other; 13 (`BINGO_LOCKOUT_TARGET`) wins; server verdict banner shows the right winner. [A] relay lockout/timeout tests.
- [ ] [M] CALLS (Call and Response, online): open-calls 1..3 and target cycle in menu; first to target wins. [A] `test_v11_calls_*`.

## 4. ROOM SETTINGS
- [ ] [M] NONSTOP on: star does not kick you out; Bowser key / Grand Star still do; modifier stays on until course exit. [A] `test_v10_room_flags_carry_nonstop`.
- [ ] [M] Unlock full game ON: cannons open, Toad/MIPS stamped; board has NO progression tiles (open cannons / Toad stars / MIPS). OFF: progression tiles can appear.
- [ ] [M] Modifier stars on star select (Z cycles, wraps backwards from NONE): green demon, reverse joystick, ordered red coins, click game, random stars (3 purple stars spawn), daredevil, splatoon (ink decals, clear on exit) -> each visibly active, cleared on course exit.
- [ ] [A] Hidden tiers / time limit / wide objective mask ride the lobby: `test_v7_*`, `test_v8_options_ride_lobby`, `test_v10_wide_objective_mask`.

## 5. EACH TILE FAMILY (`enum BingoObjectiveType`; one tile per family, ~15 min)
[A] for all: `test/host` sim tests + goldens; `test/emu make test-ram` diffs live board vs host generator.
- [ ] [M] Single stars (plain, timed, TTC random, A/B/Z-button, coinless) -> plain star + one challenge tile tick; a broken challenge marks FAILED.
- [ ] [M] Game-modifying stars (click game, reverse joystick, green demon, daredevil) + random stars -> modifier auto-applies, tile ticks on the star.
- [ ] [M] Per-level (coins, 1-ups in level, stars in level, random red coins, splatoon %) -> counter HUD advances, ticks at N.
- [ ] [M] Specials (Bowser, roof w/o cannon, racing, secret, lives, cannon stars, red-coin stars, 100-coin stars, castle secret stars, dangerous wall kicks) -> spot-check 2. Cannon stars: in-game cannon hit still unverified -> verify now.
- [ ] [M] Collectables (multicoin/star, BLJ, hat loss, signposts, poles, cannons shot, red/blue coins, ! boxes + cap boxes, warp pads, koopa shells, spin hearts) -> spot-check 3, counts don't double on re-entry.
- [ ] [M] Enemies (goombas, bob-ombs, spindrifts, Mr. I, scuttlebugs, bullies, chuckyas, whomps, boos, snufits, clams, fly guys, Mr. Blizzards, skeeters, koopas, crushed, unique deaths) -> spot-check 3; chuckya counts once per chuckya (fixed f260efa87).
- [ ] [M] Progression (open cannons, Toad stars, MIPS) with unlock OFF -> ticks.
- [ ] [M] 2026-09-26 wave (caps worn, purple switches, stuck in ground, coins in N levels, 1-ups in N levels) -> spot-check 2.
- [ ] [M] Known open: weight-budget leak (exhausted type can overdeal) -> accept or fix before launch; fixing = golden re-bless + protocol bump.

## 6. ONLINE (two PCs vs LIVE relay, ~20 min)
[A] `python3 server/test_relay.py` (whole file green); `python3 test/pc/e2e.py` (relay + RefClient + real game, screenshots reviewed); `test/net/rematch_e2e.py`.
- [ ] [A/M] Live relay version: probe `J 0 probe` via the tunnel -> `E version <current>`. If stale, Matt runs `server/deploy/update.sh` (Claude is blocked) BEFORE publishing, else players get "E version".
- [ ] [M] Relay VM up (`mario-server`, billing account open) -> `?` probe answers occupancy.
- [ ] [M] Host creates room in FILE SELECT; friend joins the room -> both on roster, names de-duplicated.
- [ ] [M] Both READY -> countdown -> same board on both screens.
- [ ] [M] Claim cells -> claim toast + L-screen roster update on the other PC; whereabouts ghosts show (and hide when privacy is off).
- [ ] [M] Lockout race to verdict -> both see the same winner banner; tie finishes get distinct places.
- [ ] [M] Host pause+R -> ONLINE -> BACK TO LOBBY (K) -> both return to lobby, all unready; READY again -> fresh race (rematch).
- [ ] [M] Late joiner (protocol 12): third client joins mid-race -> waits in lobby, not dropped into the race; joins the next one.
- [ ] [M] Reconnect: kill friend's network ~10s mid-race -> resumes as racer with claims intact (reconnect token).
- [ ] [M] Host passing: host quits mid-race -> lowest-id connected member becomes host; old host rejoins as member.
- [ ] [M] Leave race from pause menu -> clean return, room unaffected.
- [ ] [M] Windows: drag the window by the title bar ~15s during a race -> no disconnect.
- [ ] [M] Relay match history written for the race (`.jsonl` on VM; disk-guard only pauses history).

## 7. N64 (~5 min, emulator or flashcart)
- [ ] [A] Tag ROM rebuilt in a fresh short-path worktree (`~/b64-bps`) -> md5 == sweep ROM.
- [ ] [A] Flips (Windows) `--create --bps-delta --exact`, then `--apply --exact` round-trip -> md5 match.
- [ ] [M] Patched ROM boots, offline board plays one star; no online menus (online is PC-only). Options scroll page renders (N64 build of scroll change was unverified).

## 8. WINDOWS
- [ ] [M] Run the exe from `AppData\Local\bingo64-test` (never Documents) on a machine without dev tools -> launches, no missing DLLs.
- [ ] [M] 120Hz+ monitor: game speed normal (DXGI pacing), audio not drifting.
- [ ] [M] `bingo64-extract.exe` on a clean folder with a user ROM -> produces `res/`, game uses it.

## 9. RELEASE PACKAGING
- [ ] [A] `tools/bingo64_extract/make_release.sh` from `~/b64-win` (llvm-mingw on PATH) -> passes `audit_release.py` (no ROM bytes), `audit_texture_names.py`, and `audit_custom_zip.py` (every texture the exe names is in the zip or extractor).
- [ ] [M] Art list came from the SOURCE worktree (`.bingo64_src` / `BINGO64_SRC`), not the stale `~/b64-win` index -> all icon PNGs present in `res/bingo64.custom.zip`.
- [ ] [M] Zip (python zipfile, flat): README.txt, bingo64-extract.exe, relay.py, exe, res/bingo64.custom.zip -> nothing else, nothing missing.
- [ ] [M] Unzip on the friend's PC fresh -> extract -> play one online race (this IS the section 6 run if done in order).
- [ ] [M] Tag pushed, GitHub release notes (compat note first, Setup last, N64 section), `.bps` attached; Pages deploy green (`gh run list --workflow=web-deploy.yml`).
- [ ] [M] Relay redeployed to match (section 6 probe re-run after publish) -> `E version` matches the release.
