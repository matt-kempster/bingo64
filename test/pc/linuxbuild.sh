#!/bin/bash
# Incremental Linux PC build for screenshot/E2E testing (test/pc/e2e.py).
# Builds OUT-OF-TREE in $BL (default ~/b64-refresh: a full checkout with
# baserom + generated res/ already in place) because the worktree lives on
# /mnt/c where make is unusably slow. rsync only copies sources; the
# staleness check catches the rsync-mtime trap (unchanged mtimes -> make
# skips recompiling -> old binary looks "rebuilt").
# Attract demos must stay IN (no NO_ROM_DEMOS): the toast demo hook
# (BINGO64_TOAST_DEMO=1) renders during the demo's gameplay HUD.
set -e
WT="$(cd "$(dirname "$0")/../.." && pwd)"
BL="${BL:-$HOME/b64-refresh}"
rsync -a "$WT/src/" "$BL/src/"
rsync -a "$WT/bin/" "$BL/bin/"
rsync -a "$WT/data/" "$BL/data/"
rsync -a "$WT/Makefile" "$WT/defines.mk" "$BL/"
cd "$BL"
make VERSION=us -j8 2>&1 | tail -8
strings build/us_pc/sm64.us.f3dex2e | grep -q BINGO64_TOAST_DEMO \
  && echo "OK: new code is in the binary" \
  || { echo "STALE BINARY (rsync mtime trap) - forcing rebuild"; exit 1; }
ls -l build/us_pc/sm64.us.f3dex2e
