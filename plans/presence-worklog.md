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

## Deferred / follow-ups

- Room-setting visibility tiers + whereabouts toggle → protocol v6
  (options message grows); hold until no active playtest evening.
- R-menu local toggles (toasts on/off) — after the toast ships.
- Reconnect replay can re-deliver a claim burst; the 3-slot cap
  contains the spam, revisit if it looks bad in practice.
- Sound cue: skipped by design for now.
