#!/bin/bash
# Generates the ROM-build inputs the web and test/host builds need, without a
# baserom: the encoded text tables and the icon texture data. Used by the
# GitHub workflows; harmless to run locally too.
#
# Almost every icon texture is a custom PNG committed to the repo. The two
# vanilla ones (HUD star and coin) are extracted from the baserom, so CI
# cannot regenerate them; their generated .inc.c contents can be supplied via
# the WEB_TEX_STAR_05C00 / WEB_TEX_COIN_05800 environment variables (set as
# repository variables on GitHub). Without them, zero stubs are used: board
# content is unaffected, those two icons just render blank.
set -euo pipefail
cd "$(dirname "$0")/.."

write_tex() { # $1 = texture path under build/us, $2 = contents (may be empty)
    if [ -f "build/us/$1" ] && [ -z "$2" ]; then
        return # keep an existing real file over a stub
    fi
    mkdir -p "$(dirname "build/us/$1")"
    if [ -n "$2" ]; then
        printf '%s' "$2" > "build/us/$1"
    else
        python3 -c "print('0x0,' * 512)" > "build/us/$1"
    fi
}

write_tex textures/segment2/segment2.05C00.rgba16.inc.c "${WEB_TEX_STAR_05C00:-}"
write_tex textures/segment2/segment2.05800.rgba16.inc.c "${WEB_TEX_COIN_05800:-}"

targets="build/us/include/text_strings.h build/us/text/us/define_text.inc.c"
for tex in $(grep -o '"textures/[^"]*"' src/game/bingo_objective_info.c | tr -d '"'); do
    png="${tex%.inc.c}.png"
    if [ ! -f "$png" ] && [ ! -f "build/us/$tex" ]; then
        write_tex "$tex" "" # future baserom-derived texture: stub it
    fi
    targets="$targets build/us/$tex"
done

make QEMU_IRIX=true NOEXTRACT=1 GRUCODE=f3dex $targets
