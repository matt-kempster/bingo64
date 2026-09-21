#!/bin/bash
# Assemble the public Windows release folder. The result contains no
# Nintendo data: users generate res/ with bingo64-extract from their own ROM.
#
# Run from the repo root of a tree built with:
#   make WINDOWS_BUILD=1 ... EXTERNAL_DATA=1 NO_ROM_DEMOS=1 VERSION=us
# Needs: baserom.us.z64 in the tree (build-time only, never copied out),
# python3, and x86_64-w64-mingw32-gcc on PATH for the extractor exe.
#
# Usage: tools/bingo64_extract/make_release.sh <outdir>
set -e
OUT=${1:?usage: make_release.sh <outdir>}
BUILD=build/us_pc
EXTDIR=tools/bingo64_extract

EXE=$(ls "$BUILD"/sm64.us.*.exe 2>/dev/null | head -1)
[ -n "$EXE" ] || { echo "error: no Windows exe in $BUILD"; exit 1; }
[ -f "$BUILD/sound/sound_data.ctl" ] || { echo "error: no built sound data in $BUILD"; exit 1; }
grep -q "NO_ROM_DEMOS" "$BUILD/assets/demo_data.c" 2>/dev/null || \
    [ "$(grep -c 0x "$BUILD/assets/demo_data.c")" = "0" ] || \
    { echo "error: demo_data.c contains demo bytes; rebuild with NO_ROM_DEMOS=1"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/res"

# Extractor: manifest from this build + baserom, then the Windows binary.
python3 "$EXTDIR/gen_manifest.py" "$BUILD" "$EXTDIR/manifest.inc"
x86_64-w64-mingw32-gcc -O2 -o "$OUT/bingo64-extract.exe" "$EXTDIR/extract.c"

cp "$EXE" "$OUT/"
cp server/relay.py "$OUT/"
cp "$EXTDIR/README.release.txt" "$OUT/README.txt"

# bingo64.custom.zip: committed bingo64 art (gfx/ paths) + the structural
# sound_data.ctl (generated from committed jsons; no ROM waveforms inside).
# The art list comes from the SOURCE worktree's git index: the build
# checkout (~/b64-win) is a stale clone that winbuild.sh rsyncs sources
# into, so its own `git ls-files` misses any PNG committed since it was
# cloned (beta.8 shipped without the nine enemy-objective icons that way).
# winbuild.sh records the source worktree in .bingo64_src; BINGO64_SRC
# overrides it.
SRC="${BINGO64_SRC:-$(cat .bingo64_src 2>/dev/null || echo .)}"
echo "custom art list from $SRC"
LST=$(mktemp)
git -C "$SRC" ls-files actors levels textures | grep '\.png$' | \
    while read -r p; do echo "$SRC/$p gfx/$p"; done > "$LST"
echo "$BUILD/sound/sound_data.ctl sound/sound_data.ctl" >> "$LST"
python3 tools/mkzip.py "$LST" "$OUT/res/bingo64.custom.zip"
rm -f "$LST"

echo "release assembled in $OUT:"
find "$OUT" -type f | sort
echo
echo "auditing texture names in the exe ..."
python3 "$EXTDIR/audit_texture_names.py" "$BUILD" "$EXE"
echo
echo "auditing that every texture the exe names is shipped or extracted ..."
python3 "$EXTDIR/audit_custom_zip.py" "$EXE" "$OUT/res/bingo64.custom.zip" "$EXTDIR/manifest.inc"
echo
echo "auditing for ROM bytes ..."
python3 "$EXTDIR/audit_release.py" baserom.us.z64 "$OUT" "$BUILD"
