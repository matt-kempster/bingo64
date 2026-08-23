# Presence & notifications — worklog

Branch: `presence`. A running catalog of decisions and commits so the
whole effort can be reviewed (and cherry-picked) as one story.

## Design decisions (Matt, interviewed 2026-08-22)

**Control model — hybrid.** Room settings (host-set, visible to all in
the lobby) decide what *information* is shared: claim-visibility tier,
whereabouts on/off. Local settings decide *presentation* only: toasts
on/off, ghost visibility. Everyone races on the same information.

**Claim-visibility tiers.** Mode-dependent set, invalid states
unrepresentable (e.g. "# of bingos" only exists where bingo count is
the win condition; lockout is always full-visibility because claims
lock cells). Tier set per mode to be finalized when the room-settings
work lands — it extends the options wire message, so it ships with the
next protocol bump (v6), NOT during an active playtest evening.

**Toast look (the design pass).**
- Bottom of screen, subtitle-style: centered dim strip (like sign
  text), dialog/menu font — NOT the huge colorful HUD font.
- Claim toast copy: `Quate got [icon]` — name rendered in the
  player's hat color (`gNetColorRGB`), icon is the objective's real
  16x16 board texture (`print_bingo_icon_alpha` draws it anywhere).
- No sound for now (revisit if a clearly-right cue shows up).
- Feed rules: ~4s life, fade-out, stack of 3, newest at the bottom;
  claims AND join/leave/disconnect/reconnect/host-change share it.

**Race verdict / win messages — quiet everything.** All race messaging
moves to the dialog-font-on-dim-strip register, including "X won — you
placed N" and the finish banner. Exception: your OWN win gets a
celebratory color-cycling (rainbow) treatment and lingers longer.

**Whereabouts.** Room setting, default ON: L-screen roster shows each
racer's current course (data already rides the ghost payload). Host
can disable for serious races. (Ships with the v6 room-settings bump;
the L-roster itself can ship earlier showing it unconditionally.)

## Infrastructure found (no new tech needed)

- `print_generic_string_ascii_detail(x, y, str, r,g,b,a, shadow, pad)`
  (src/extras/draw_util.c) — dialog font, ASCII in, per-call color.
- `print_solid_color_quad(x1,y1,x2,y2, r,g,b,a)` — the dim strip.
- `print_bingo_icon_alpha(x, y, icon, alpha)` (bingo_ui.c) — any
  objective icon at any screen position; the "tileset mixing" is free.
- `get_string_width_ascii` — centering math.
- Existing toast queue `bingo_notice` (bingo_ui.c) + network pushes
  (join/leave/host/finish already wired) — restyle it, add claims.
- Text draws inside `dl_ia_text_begin/end`; icons inside
  `dl_hud_img_begin/end`; the quad is self-contained.

## Screenshot testing (Matt's ask: "this is likely gonna be VERY
finnicky — use screenshot testing")

Loop: Linux PC build in /home/matt/b64-refresh → run under Xvfb :99
(`SDL_AUDIODRIVER=dummy`, `BINGO64_TOAST_DEMO=1`) → wait for the
attract demo (HUD renders there; the demo hook cycles sample toasts
every 3s) → `import -window <id>` PNGs → eyeball alignment, iterate.
Gotchas: the SDL window restores a saved position that can sit outside
the 1280x960 Xvfb root (captures come back black) — `xdotool
windowmove <id> 0 0` first; capture the game window id, not root.
`BINGO64_TOAST_DEMO` is a committed, env-gated dev hook in bingo_ui.c.

## Commits

- worklog: decisions + infra survey.
- toast feed v1: subtitle-style restyle of bingo_notice (bottom-LEFT
  dim strips — Matt switched from centered after seeing screenshots —
  dialog font, hat-colored names via bingo_notice_rich,
  real board icon inline); remote-claim toasts wired in bingo_net's
  apply_remote_claims; network.c notice_about now colors names; two
  screenshot-tuned alignment passes (text +1, icon -2 within the 19px
  strip, row pitch 21). Verified in BOB via `--level 9` under Xvfb.
  Third alignment pass from Matt's own read of the screenshots (text
  -6, icon +1 from the v1 offsets).
- quiet banners: draw_quiet_line extracted and reused for the
  persistent race verdict ("Quate | won - you placed 2", winner name
  in hat color, centered y=189) and the win screen — all three cases
  (lockout, race finish, solo) now dialog-font strips; your OWN win
  (lockout win / place 1 / solo completion) renders in color-cycling
  rainbow (sins-based, ~2s period). Dismiss hint is one centered line.
  BINGO64_WIN_DEMO=1 dev hook overlays the solo banner for screenshots.
  N64 keeps the old HUD-font win screen untouched.
- L-screen presence roster: the right-column standings block becomes a
  full roster (all modes) in the DIALOG font (Matt: "bigger font, PC
  has room") — names in hat colors, `?` appended when the connection
  is in doubt (dropped OR ghost silent 2s+; dialog font has a real
  question mark, the tiny font's rendered like a comma). Finished
  racers show "1st 12'34.50"; racing players show claim count +
  current course (ghost level → gLevelToCourseNumTable →
  courseAbbreviations, "CASTLE" for hubs). Rows x=236, y=150 step 15.
  Verified END-TO-END: scratchpad/presence_e2e.py runs the real relay
  in-process (test_relay.start_relay) + RefClient host "quate" (UDP) +
  the actual game as "matt" (TCP, --skip-title, CLI auto-ready);
  quate sends X (GO), ghosts in BOB, claims cells 7/12 — screenshots
  caught the live claim toast ("quate got <icon>", pink name, real
  board icon) and the roster ("MATT 0 CASTLE / QUATE 2 BOB", then
  "QUATE* 2 BOB" after quate went silent). Reusable rig for all
  future presence work.

- toast copy + roster table (Matt's review, 2026-08-22 evening): claim
  toasts now read "quate completed <icon> WF*7" — verb changed from
  "got", and the cell's mini subtitle (objective->title, with the HUD
  0xFA star translated to the dialog font's '*' star) rides after the
  icon for disambiguation. The L-screen roster became a two-line
  aligned table per player: colored name row, then a data row at fixed
  columns (x=244 status: "2 sq" / "1st"; x=276 where/time: lowercase
  course code / 0'21.86). course_code_for_level now emits lowercase.

- roster polish round 2 (Matt): data row is now "2 [] ; in BoB" — the
  squares unit is a literal drawn quad (no square glyph in the dialog
  font), the delimiter is the dialog font's interpunct 0xFC (ascii ';'
  now maps to it in draw_util's converter), courses read "in BoB" /
  "in WF" ("Castle" drops the "in" to fit the fixed column), and
  course codes use the community-standard caps — the repo's
  courseAbbreviations already had them except BOB, which the roster
  displays as BoB (the shared table stays caps: the board's HUD-font
  captions can't render lowercase).

- castle whereabouts refined (Matt): the hub blob became in lobby /
  in basement / upstairs / in tippy (castle interior by area, area 2
  split at y=2000 for the third floor), outside (grounds), in
  courtyard (Boo courtyard). whereabouts_for_level now returns the
  whole phrase — adverbs carry no "in". E2E fake ghost verified tippy
  (level 6 area 2 y=3000). OPEN: Matt said "courtyard and back
  courtyard" — only one courtyard level exists; asked whether he wants
  castle grounds split front/back by position.

- roster polish round 3: square+interpunct clashed side by side →
  racing rows drop the interpunct (the square already breaks the
  fields; finished rows keep it — no square there). "in Castle"
  restored (its omission was an interpunct-era width special case).
  Then Matt's "castle (tippy)" format measured ~95px against the
  ~54px column and ran off the physical screen edge; he picked the
  parens-only form: (lobby) (basement) (upstairs) (tippy) (outside)
  (courtyard), fallback (castle) — parens alone mean "castle" by
  convention, courses keep "in BoB". E2E fake ghost at level 6 area 3
  (basement, the widest) verified the fit.

- playtest feedback round (Matt, 2026-08-22 night): five fixes.
  (1) The persistent race-verdict banner was undismissable → now the
  win screen's two-L-press pattern: first press shows "press L again
  to dismiss" under the banner, second hides it; state resets when
  the winner id changes (next race / back to lobby). hud.c forwards
  every L buttonPressed via bingo_race_verdict_on_l(). Found via the
  E2E rig: the PC port renders the HUD twice per game-logic frame
  (interpolation), so buttonPressed counted twice per physical press
  — the pre-existing win screen thus dismissed on ONE press and its
  "press L again" hint never showed on PC. Both counters now
  debounced on gGlobalTimer in hud.c. E2E extended: quate claims a
  full row + sends F (the client judges its own bingo; the relay
  only assigns places), screenshots verify banner, hint after one
  tap, gone after two.
  (2) The d-pad objective description overlapped the roster (same
  right column) → the description takes the column while the cursor
  is up; the cursor now resets when the board closes (it used to
  persist forever after the first d-pad touch), so reopening shows
  the roster again.
  (3) "finished 1ST" toast → place_suffix in network.c lowercased
  (was written for the caps-only HUD font) + 11th-13th handled; the
  roster's inline ordinal ternary replaced by a shared
  ordinal_suffix() in bingo_ui.c.
  (4) Verdict copy "won - you placed 2" / "race for place 2" →
  "won - you took 2nd place" / "won - racing for 2nd place"; win
  screen "finished number 2 in ..." → "finished 2nd in ...".
  (5) "QUATE won" → "quate won": net_name_of_id ran names through
  hud_upper, a leftover from the HUD-font win screen; names now
  render as typed (the caps-only lobby font is why nobody notices
  what case they typed). Also "you are a super player" → "You are a
  super player!".

- protocol v6: visibility room settings (Matt's interview,
  2026-08-22 night — he OK'd the version bump): claim-visibility tier
  Open / Counts / Bingos / Hidden + a separate Locations (whereabouts)
  toggle, both host-owned room settings on the options screen (two
  new rows under Game mode; lobby-only, frozen at start like the
  seed). Invalid states unrepresentable via coerce (client
  net_claimvis_coerce + relay coerce_claimvis): lockout AND blackout
  force Open (lockout is about the squares; blackout turned out to be
  CO-OP on a shared board — peer claims complete your board — so
  hiding is nonsense there; my interview question wrongly called it a
  race mode, flagged to Matt), Bingos tier only in 2/3-bingo modes
  (else falls to Counts). Wire: O grows to
  "O mode unlock mask seed claimvis where" (rebroadcast
  "O mode claimvis where"), W and S carry both fields, PROTOCOL 5→6
  both sides. Client display gating: board chips only in Open; claim
  toasts per tier ("completed <icon> WF*7" / "completed a square" /
  "got a bingo" via bingo_net_bingo_count line counting / silence);
  roster progress column count+square in Open/Counts, "2 *" (dialog
  star) in Bingos, blank in Hidden; own row always full; whereabouts
  column obeys Locations (self always shown). Enforcement is
  CLIENT-side (Matt: ok for now, relay-side withholding recorded as
  potentially release-blocking in the checklist §2). Local R-menu
  toggle: Settings → "Online Toasts" (configBingoToasts) mutes the
  feed locally; notices still age (lobby status line unaffected).
  Relay tests 23/23 incl. new test_v6_visibility_settings; e2e.py
  gained $E2E_ROOMOPTS/$E2E_PREFIX for screenshot passes per config.

- v6 DEPLOYED live (2026-08-22 ~23:31 UTC, room empty per Matt).
  New "?" occupancy probe (no join needed) answers
  "? rooms= members= racing="; deploy/update.sh asks it before
  restarting and aborts on members>0 (BINGO64_FORCE=1 overrides).
  Verified live: real client joined via the auto TXT resolver
  against the v6 relay; probe answers rooms=0 over gcloud ssh; the
  journald flush fix rode along and log lines now appear instantly.
  gcloud lives at ~/google-cloud-sdk/bin (not on the default PATH).
  bingo64-v6.exe staged in AppData; bingo64-presence.exe (v5) is now
  DEAD against the live relay — players must switch to the v6 exe.

- Matt's v6 review nits (2026-08-22 late): options labels became
  "Opp. squares" (values Visible/Counts/Bingos/Hidden) and
  "Opp. locations", all colons dropped, "Unlock full game"
  lowercased. The Opp. rows only render while in an online room —
  offline shows the classic 3 rows (BINGO_CONFIGS_IN_LEFT_COL is now
  runtime on PC; N64 keeps 3). Win screen: "Finished 2nd in ..."
  capital F, and the 1-frame flash before it is fixed — between the
  local finish and the server's F broadcast the win screen fell
  through to the solo "your time is" banner; online it now draws
  nothing until the placement arrives. Both option screens
  screenshot-verified (offline via 1P->OPTIONS clicks, online via a
  held in-process relay + lobby OPTIONS clicks).

- options value column right-aligned by measurement (Matt: "Visible"
  and "Lockout" hung short): the hand-tuned per-string x offsets are
  gone — bingo_config_value_x() right-aligns every value (mode,
  tier, On/Off) to BINGO_CONFIG_VALUE_RIGHT_X 158 via
  get_string_width. Window-size question answered (sm64config.txt
  window_w/h persisted on quit, shared by all exes in the folder;
  640x480 only on fresh config) — Matt: current behavior is fine.

- lockout verdict rewritten in the race register (Matt: "you win 13
  squares is pathetic"): "Won 13 squares in 0'21.20" rainbow for the
  winner / "quate won 13 squares in 0'21.20" for the rest, using the
  V result's server time (local clocks keep running for losers).
  Winner now gets the super-player hint. e2e.py grew a lockout
  scenario (E2E_ROOMOPTS mode 4 → quate claims the 13 majority,
  relay decides) — banner/hint/dismiss screenshot-verified.

- "auto server lookup failed" root-caused twice (2026-08-22 night):
  first pass added retries + a net_auto_cache config fallback
  (legit hardening, wrong diagnosis); Matt still failed → the REAL
  bug: net_dial ran the auto TXT lookup before WSAStartup, so on a
  fresh Windows launch socket() died with WSANOTINITIALISED — auto
  NEVER worked cold on Windows, and only seemed flaky because a
  prior manual-address dial initialized Winsock. Linux (all E2E) was
  immune, which is why tests stayed green. WSAStartup hoisted to the
  top of net_dial.

- RELEASE v1.0-beta.6 drafted (Matt said "deploy as 5.3"; the
  convention ties the beta number to the protocol — this is wire v6
  and refuses 5.x, so it ships as beta.6). Board overlay now prints
  "V1.0 BETA <protocol>" instead of the stale "VERSION 0.11a".
  make_release.sh run from ~/b64-win (winbuild.sh now rsyncs tools/
  + server/ so the extractor and the packed relay.py match); audit
  passed ("no ROM-derived bytes"); zip layout matches 5.2. Branch
  `presence` pushed to origin; DRAFT release created for Matt to
  publish (his final check per the drafts-first convention).

## Deferred / follow-ups

- Relay-side visibility enforcement (see checklist §2) — potentially
  release-blocking; client-side hiding shipped with v6.
- Reconnect replay can re-deliver a claim burst; the 3-slot cap
  contains the spam, revisit if it looks bad in practice.
- Sound cue: skipped by design for now.
- ~~Dropout claim attribution (zcoop98, 2026-08-23)~~ DONE 2026-08-23:
  bingo_net.c snapshots the local id + per-id hat colors every frame
  while connected and the board reads the frozen copy after a terminal
  disconnect (`bingo_net_dropped`). Covers the owner chips, the solo
  lockout win-count fallback (peers' squares no longer count as yours),
  local Mario's hat color, and post-drop completions (self bit set
  locally). E2E: lockout race, 12 quate + 2 zed claims, relay killed +
  resume refused ("E") -> chips/color persist, no bogus win banner.
