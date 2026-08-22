# PC-port build + screenshot E2E testing

Scripts for iterating on PC-only UI (toasts, roster, banners) with
screenshot verification, and for producing the Windows playtest exe.
They assume Matt's WSL2 machine: out-of-tree build checkouts at
`~/b64-refresh` (Linux) and `~/b64-win` (Windows cross), each with the
baserom and generated `res/` already in place; llvm-mingw under `~/opt`.

## The cycle

```
# once per boot: a headless X server (abstract socket, no /tmp perms)
Xvfb :99 -screen 0 1280x960x24 &

test/pc/linuxbuild.sh          # rsync worktree -> ~/b64-refresh, make
python3 test/pc/e2e.py         # in-process relay + RefClient + real game
                               #   (E2E_OUT=<dir> for the .png output)
test/pc/winbuild.sh            # cross-build the Windows exe
cp ~/b64-win/build/us_pc/sm64.us.f3dex2e.exe \
   /mnt/c/Users/Matt/AppData/Local/bingo64-test/bingo64-<tag>.exe
```

Run the whole thing as ONE background pipeline; don't stack pollers.

## What e2e.py does

Starts the real relay in-process (`server/test_relay.start_relay()`, own
ephemeral ports), joins a UDP reference client "quate", then boots the
actual game as "matt" over TCP (`--skip-intro --skip-title --net-*`; CLI
join auto-readies). quate starts the race (`X`), streams fake ghosts
(`G <level> <area> x y z yaw anim animframe` — level 9 = BOB, level 6 =
castle interior, pick any area/height to test whereabouts), claims cells
(`C n`), and finishes (`F` — the client judges its own bingo, the relay
only assigns places; nothing wins until someone sends F). Screenshots
via `import -window <id>` at scripted times: claim toast, L-screen
roster (hold `q` = the L trigger keyboard bind), race-verdict banner,
its dismiss hint after one L tap, and its absence after two.

## Gotchas (each cost a debugging session once)

- **Black screenshots**: SDL restores a saved window position that can
  sit outside the Xvfb root. Capture by window id AND
  `xdotool windowmove <id> 0 0` first. Unset `WAYLAND_DISPLAY` or WSLg
  grabs the window onto the real desktop.
- **Stale binary**: rsync preserves mtimes, so make may skip
  recompiling after a copy. linuxbuild.sh greps the binary for a known
  new symbol and fails loudly instead.
- **HUD renders twice per game-logic frame** on PC (interpolation):
  `buttonPressed` handlers in HUD code fire twice per physical press
  unless debounced on `gGlobalTimer` (see hud.c's L handler).
- **Demo hooks**: `BINGO64_TOAST_DEMO=1` cycles sample toasts,
  `BINGO64_WIN_DEMO=1` overlays the solo win banner — quick visual
  checks without a race. Keep attract demos in the Linux build (no
  NO_ROM_DEMOS) or the toast demo has no HUD to render onto.
- **Review the PNGs before showing anyone** — resize up (`convert
  -resize 640x`) and actually look; alignment bugs hide at 1x.
