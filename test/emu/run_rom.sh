#!/bin/bash
# Runs a ROM headless in mupen64plus, taking screenshots at chosen frames.
#
# usage: run_rom.sh <rom> <shot_dir> <frame_list> [input_script]
# example: run_rom.sh ../../build/us/sm64.us.f3dex.z64 /tmp/shots 30,300 scripts/boot.txt
#
# M64P points at the mupen64plus install (default: ~/opt/m64p/install).

set -e

ROM="$1"
SHOTDIR="$2"
FRAMES="$3"
SCRIPT="${4:-}"

M64P="${M64P:-$HOME/opt/m64p/install}"
HERE="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$SHOTDIR"

if [ -n "$SCRIPT" ]; then
    export BINGO_INPUT_SCRIPT="$(cd "$(dirname "$SCRIPT")" && pwd)/$(basename "$SCRIPT")"
fi

# Keep SDL inside the virtual display. Without this, WSLg can grab the
# window and Mario pops up on the real desktop.
unset WAYLAND_DISPLAY
export SDL_VIDEODRIVER=x11

SDL_AUDIODRIVER=dummy exec xvfb-run -a "$M64P/bin/mupen64plus" \
    --corelib "$M64P/lib/libmupen64plus.so.2.0.0" \
    --plugindir "$M64P/lib/mupen64plus" \
    --datadir "$M64P/share/mupen64plus" \
    --configdir "$SHOTDIR/cfg" \
    --sshotdir "$SHOTDIR" \
    --noosd --nospeedlimit \
    --audio dummy \
    --input "$HERE/build/input_script.so" \
    --testshots "$FRAMES" \
    "$ROM"
