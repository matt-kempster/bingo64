#!/usr/bin/env python3
"""Checks the bingo modifier star row on the star select screen.

Warps to BOB's star select, screenshots it, and verifies the modifier
row renders every star (one per enum BingoModifier entry, identified by
color) in enum order, evenly spaced, and centered on screen. Then a
single Z press wraps the selection backwards from NONE to the last
modifier (splatoon) and A enters the level, which must arrive with
gSplatoonEnabled set and Mario's landing floor painted.

Run under xvfb (make test-starui does this).
"""

import os
import sys
import tempfile

import m64p_core
from smoke_test import decode_png

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, "..", "..")
ROM = os.path.join(REPO, "build", "us", "sm64.us.f3dzex.z64")
MAP = os.path.join(REPO, "build", "us", "sm64.us.map")

LEVEL_BOB = 9
BINGO_MODIFIER_SPLATOON = 6

# Left-to-right star colors, matching enum BingoModifier order.
STAR_ORDER = ["yellow", "green", "blue", "orange", "gray", "red", "pink"]

failures = []


def check(cond, message):
    if not cond:
        failures.append(message)
        print("  check failed: %s" % message)


def classify(r, g, b):
    """Buckets a pixel into one of the star colors, or None."""
    mx, mn = max(r, g, b), min(r, g, b)
    if r > 200 and g > 190 and b < 140:
        return "yellow"
    if g - r > 40 and g - b > 40 and g > 120:
        return "green"
    if b - r > 60 and b - g > 40 and b > 120:
        return "blue"
    if r > 200 and 100 < g < 190 and b < 100:
        return "orange"
    if 90 < mx < 200 and mx - mn < 40:
        return "gray"
    if r > 180 and g < 80 and b < 80:
        return "red"
    if r > 180 and g < 130 and b > 100 and r - b > 30:
        return "pink"
    return None


def star_centroids(png_path):
    """Finds each star color's x centroid in the modifier row.

    Returns (width, {color: centroid_x}). The row's y position is found
    from the pink star (the only pink thing on the screen), then all
    colors are located in a band around it. Per color, only columns near
    that color's peak density count, which keeps thin star outlines from
    polluting the gray bucket.
    """
    width, height, rows = decode_png(png_path)

    pink_xs, pink_ys = [], []
    for y in range(height):
        row = rows[y]
        for x in range(width):
            if classify(row[x * 3], row[x * 3 + 1], row[x * 3 + 2]) == "pink":
                pink_xs.append(x)
                pink_ys.append(y)
    if len(pink_ys) < 100:
        return width, {}
    band_y = sum(pink_ys) // len(pink_ys)

    col_counts = {name: {} for name in STAR_ORDER}
    for y in range(max(0, band_y - 35), min(height, band_y + 35)):
        row = rows[y]
        for x in range(width):
            name = classify(row[x * 3], row[x * 3 + 1], row[x * 3 + 2])
            if name:
                col_counts[name][x] = col_counts[name].get(x, 0) + 1

    centroids = {}
    for name, cols in col_counts.items():
        if not cols:
            continue
        peak = max(cols.values())
        good = {x: n for x, n in cols.items() if n >= max(3, peak * 0.3)}
        if sum(good.values()) < 50:
            continue
        centroids[name] = sum(x * n for x, n in good.items()) / sum(good.values())
    return width, centroids


def main():
    if not os.path.exists(ROM):
        print("missing %s -- run the main build first" % ROM)
        return 1

    syms = m64p_core.load_map_symbols(MAP, [
        "gTestWarpRequest", "gBingoStarSelected", "gCurrLevelNum",
        "gSplatoonEnabled", "gSplatoonPaintedCount",
    ])

    shotdir = tempfile.mkdtemp(prefix="bingo64_starui_")
    os.environ["XDG_DATA_HOME"] = shotdir
    os.environ.pop("WAYLAND_DISPLAY", None)
    os.environ["SDL_VIDEODRIVER"] = "x11"
    os.environ["SDL_AUDIODRIVER"] = "dummy"
    os.environ["BINGO_INPUT_SCRIPT"] = os.path.join(HERE, "scripts", "star_ui.txt")

    core = m64p_core.Core(
        os.environ.get("M64P", os.path.expanduser("~/opt/m64p/install")),
        tempfile.mkdtemp(prefix="bingo64_starui_cfg_"))
    core.load_rom(ROM)
    core.attach_standard_plugins(os.path.join(HERE, "build", "input_script.so"))

    state = {}

    def read_s16(addr):
        raw = core.read_bytes(addr, 2)
        v = (raw[0] << 8) | raw[1]
        return v - 0x10000 if v & 0x8000 else v

    def on_frame(frame):
        if frame == 450:
            core.write_u32(syms["gTestWarpRequest"], LEVEL_BOB)
        elif frame == 560:
            core.take_screenshot()
        elif frame == 600:
            state["selected"] = core.read_u32(syms["gBingoStarSelected"])
        elif frame == 780:
            state["level"] = read_s16(syms["gCurrLevelNum"])
            state["splat"] = core.read_u32(syms["gSplatoonEnabled"])
            state["painted"] = core.read_u32(syms["gSplatoonPaintedCount"])
        elif frame >= 790:
            core.stop()

    core.run(on_frame)
    core.shutdown()

    check(state.get("selected") == BINGO_MODIFIER_SPLATOON,
          "Z did not wrap selection to splatoon (selected=%r)" % state.get("selected"))
    check(state.get("level") == LEVEL_BOB, "warp did not reach BOB")
    check(state.get("splat") == 1,
          "splatoon not enabled in level (splat=%r)" % state.get("splat"))
    check(state.get("painted", 0) >= 1,
          "landing floor not painted (painted=%r)" % state.get("painted"))

    shots = []
    shot_root = os.path.join(shotdir, "mupen64plus", "screenshot")
    if os.path.isdir(shot_root):
        shots = sorted(f for f in os.listdir(shot_root) if f.endswith(".png"))
    check(len(shots) == 1, "expected 1 screenshot, found %d" % len(shots))

    if shots:
        width, centroids = star_centroids(os.path.join(shot_root, shots[0]))
        missing = [n for n in STAR_ORDER if n not in centroids]
        check(not missing, "stars missing from modifier row: %s" % missing)

        if not missing:
            xs = [centroids[n] for n in STAR_ORDER]
            check(xs == sorted(xs),
                  "stars out of order: %s" % ["%.0f" % x for x in xs])

            spacings = [xs[i + 1] - xs[i] for i in range(len(xs) - 1)]
            mean = sum(spacings) / len(spacings)
            worst = max(abs(s - mean) for s in spacings)
            check(worst <= mean * 0.2,
                  "uneven star spacing %s (worst off by %.1f of mean %.1f)"
                  % (["%.1f" % s for s in spacings], worst, mean))

            row_center = (xs[0] + xs[-1]) / 2
            check(abs(row_center - width / 2) <= 15,
                  "modifier row off-center (row %.1f vs screen %.1f)"
                  % (row_center, width / 2))

    if failures:
        print("STAR UI TEST FAILED (%d problems), shots kept in %s"
              % (len(failures), shotdir))
        return 1
    print("star ui test ok (%d modifier stars evenly spaced and centered; "
          "Z selects splatoon)" % len(STAR_ORDER))
    return 0


if __name__ == "__main__":
    sys.exit(main())
