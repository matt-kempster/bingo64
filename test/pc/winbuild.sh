#!/bin/bash
# Incremental Windows cross-build (EXTERNAL_DATA + NO_ROM_DEMOS, D3D11)
# with the NTSC pacing fix. Out-of-tree in $BW (default ~/b64-win, a
# checkout with baserom + res/ in place); llvm-mingw toolchain expected
# under ~/opt. Stage the result yourself, e.g.:
#   cp ~/b64-win/build/us_pc/sm64.us.f3dex2e.exe \
#      /mnt/c/Users/Matt/AppData/Local/bingo64-test/bingo64-<tag>.exe
set -e -o pipefail  # pipefail: make's status must survive the tail pipe
WT="$(cd "$(dirname "$0")/../.." && pwd)"
BW="${BW:-$HOME/b64-win}"
rsync -a "$WT/src/" "$BW/src/"
rsync -a "$WT/bin/" "$BW/bin/"
rsync -a "$WT/data/" "$BW/data/"
rsync -a "$WT/include/" "$BW/include/"
rsync -a "$WT/Makefile" "$WT/defines.mk" "$BW/"
export PATH=$HOME/opt/llvm-mingw-20240221-ucrt-ubuntu-20.04-x86_64/bin:$HOME/opt/cross-bin:$PATH
cd "$BW"
make WINDOWS_BUILD=1 CROSS=x86_64-w64-mingw32- CC=x86_64-w64-mingw32-gcc \
  CXX=x86_64-w64-mingw32-g++ WINDRES=x86_64-w64-mingw32-windres \
  RENDER_API=D3D11 WINDOW_API=DXGI SDLCROSS=x86_64-w64-mingw32- \
  VERSION=us EXTERNAL_DATA=1 NO_ROM_DEMOS=1 -j8 2>&1 | tail -15
ls -l "$BW/build/us_pc/sm64.us.f3dex2e.exe"
