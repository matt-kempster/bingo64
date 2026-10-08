#!/bin/bash
# Build the Windows exe from this checkout and install it at ONE fixed place,
# so "the current build" is always the same file:
#   %LOCALAPPDATA%\bingo64-test\latest\bingo64.exe   (+ res\base.zip, BUILD.txt)
# Run it after every commit that touches game code (and by hand to try
# uncommitted work); BUILD.txt next to the exe says what it was built from.
set -e -o pipefail
WT="$(cd "$(dirname "$0")/../.." && pwd)"
BW="${BW:-$HOME/b64-win}"
if [ -z "$BINGO64_PLAY_DIR" ]; then
    LOCALAPPDATA_WIN="$(cd /mnt/c && cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')"
    BINGO64_PLAY_DIR="$(wslpath "$LOCALAPPDATA_WIN")/bingo64-test/latest"
fi
DEST="$BINGO64_PLAY_DIR"

# One build at a time; a second caller waits, then builds whatever is newest.
exec 9>"$BW/.winplay.lock"
flock 9

"$WT/test/pc/winbuild.sh"

mkdir -p "$DEST/res"
# Windows locks a running exe against overwrite but not against rename, so
# move the old one aside when the game is open (cleaned up next time).
rm -f "$DEST"/bingo64.old-*.exe 2>/dev/null || true
if ! cp "$BW/build/us_pc/sm64.us.f3dex2e.exe" "$DEST/bingo64.exe" 2>/dev/null; then
    mv "$DEST/bingo64.exe" "$DEST/bingo64.old-$(date +%s).exe"
    cp "$BW/build/us_pc/sm64.us.f3dex2e.exe" "$DEST/bingo64.exe"
    echo "note: the game was running; restart it to get this build"
fi
cp "$BW/build/us_pc/res/base.zip" "$DEST/res/base.zip"

{
    echo "bingo64 test build"
    echo "built:  $(date '+%Y-%m-%d %H:%M')"
    echo "commit: $(git -C "$WT" describe --always --dirty)"
    echo
    echo "Latest changes:"
    git -C "$WT" log -12 --format='  %ad  %s' --date=format:'%m-%d %H:%M'
} | sed 's/$/\r/' > "$DEST/BUILD.txt"

echo "installed: $(wslpath -w "$DEST")\\bingo64.exe ($(git -C "$WT" describe --always --dirty))"
