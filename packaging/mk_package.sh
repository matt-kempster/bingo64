#!/bin/bash
# Dev-side: assemble the user-facing asset-tool package from a repo checkout
# plus a completed EXTERNAL_DATA build tree (for prebuilt 00_sound_player.m64
# and native tools). The result is a folder the end user runs gen_assets.py in
# with only Python installed.
#
# Usage: mk_package.sh <repo> <buildtree> <outdir> [wintools-dir]
#   repo         repo checkout (source of scripts, jsons, custom PNGs)
#   buildtree    build copy that has tools/ compiled and build/us_pc done
#   outdir       package folder to create
#   wintools-dir optional dir of Windows .exe tools to include instead of
#                the native ones (for the Windows package)
set -e
REPO=$1; BUILD=$2; OUT=$3; WINTOOLS=$4
[ -z "$OUT" ] && { echo "usage: mk_package.sh <repo> <buildtree> <outdir> [wintools]"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/tools/sm64tools" "$OUT/sound/sequences" "$OUT/res"

cp "$REPO/packaging/gen_assets.py" "$OUT/"
cp "$REPO/assets.json" "$OUT/"
cp "$REPO/sm64.us.sha1" "$OUT/"

# extract_assets.py, patched for standalone use: tools are prebuilt (no make
# on user machines) and python is invoked via sys.executable.
python3 - "$REPO/extract_assets.py" "$OUT/extract_assets_repo.py" <<'EOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src).read()
text = text.replace(
    '''    subprocess.check_call(
        ["make", "-s", "-C", "tools/sm64tools/", "n64graphics", "mio0"]
    )''',
    '    pass  # packaged: tools/sm64tools binaries are prebuilt')
text = text.replace(
    '''    subprocess.check_call(
        ["make", "-s", "-C", "tools/", "skyconv", "aifc_decode"]
    )''',
    '    pass  # packaged: tools binaries are prebuilt')
text = text.replace('"python3",', 'sys.executable,')
assert 'prebuilt' in text and 'sys.executable,' in text
open(dst, 'w').write(text)
EOF

# Python tools used at pack time.
for t in assemble_sound.py mkzip.py disassemble_sound.py; do
    cp "$REPO/tools/$t" "$OUT/tools/"
done
# audiofile python helper if assemble_sound needs it
[ -f "$REPO/tools/aifc.py" ] && cp "$REPO/tools/aifc.py" "$OUT/tools/"

# Native tool binaries.
if [ -n "$WINTOOLS" ]; then
    cp "$WINTOOLS"/skyconv.exe "$WINTOOLS"/aifc_decode.exe \
       "$WINTOOLS"/tabledesign.exe "$WINTOOLS"/vadpcm_enc.exe "$OUT/tools/"
    cp "$WINTOOLS"/n64graphics.exe "$WINTOOLS"/mio0.exe "$OUT/tools/sm64tools/"
else
    cp "$BUILD/tools/skyconv" "$BUILD/tools/aifc_decode" \
       "$BUILD/tools/tabledesign" "$BUILD/tools/vadpcm_enc" "$OUT/tools/"
    cp "$BUILD/tools/sm64tools/n64graphics" "$BUILD/tools/sm64tools/mio0" \
       "$OUT/tools/sm64tools/"
fi

# Sound metadata (ours) + the one sequence assembled from source rather than
# extracted from the ROM.
cp -r "$REPO/sound/sound_banks" "$OUT/sound/"
cp "$REPO/sound/sequences.json" "$OUT/sound/"
cp "$BUILD/build/us_pc/sound/sequences/00_sound_player.m64" "$OUT/sound/sequences/"

# Custom (repo-tracked, non-Nintendo) textures, preserving tree layout.
(cd "$REPO" && git ls-files 'textures/*.png' 'actors/*.png' 'levels/*.png') | \
while read -r f; do
    mkdir -p "$OUT/$(dirname "$f")"
    cp "$REPO/$f" "$OUT/$f"
done

cat > "$OUT/make_assets.bat" <<'EOF'
@echo off
rem Drag your SM64 US ROM onto this file, or put it here as baserom.us.z64
rem and double-click. Requires Python 3 from python.org.
python "%~dp0gen_assets.py" %1
pause
EOF

cat > "$OUT/README.txt" <<'EOF'
bingo64 asset pack generator
============================
The game download contains no Nintendo assets. To play, generate the asset
pack from your own Super Mario 64 ROM (US version, .z64):

  1. Install Python 3 (python.org) if you don't have it.
  2. Copy your ROM here, named: baserom.us.z64
  3. Run make_assets.bat (Windows) or `python3 gen_assets.py` (Linux/Mac).
  4. Move the generated `res` folder next to the game executable.
EOF

echo "package assembled at $OUT"
