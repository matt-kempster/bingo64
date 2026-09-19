#!/usr/bin/env python3
"""Checks the random-stars modifier: three purple stars spawn in-level.

Boots, warps to BOB, selects the random-stars bingo modifier on the star
select screen (three Z presses: the selection wraps backwards from NONE
through splatoon and daredevil), and enters the level. Exactly three
purple star objects must exist, at distinct positions inside the level's
spawn bounds. Reaching the read frame at all also proves the safe-spawn
search loop terminated on level load.

Run under xvfb (make test-rando does this).
"""

import os
import struct
import sys
import tempfile

import m64p_core

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, "..", "..")
ROM = os.path.join(REPO, "build", "us", "sm64.us.f3dex.z64")
MAP = os.path.join(REPO, "build", "us", "sm64.us.map")

LEVEL_BOB = 9
MODEL_STAR_PURPLE = 0x30

OBJECT_POOL_CAPACITY = 240
OBJECT_SIZE = 0x260  # (gMacroObjectDefaultParent - gObjectPool) / 240 in the map
OFF_SHARED_CHILD = 0x14
OFF_ACTIVE_FLAGS = 0x74
OFF_POS = 0xA0

# BoB's spawn bounding box from bingo_rando_spawn.c, generously padded for
# the wall-collision shove.
BOB_BOUNDS = (-8300, 8300, -100, 5300, -8300, 8300)

failures = []


def check(cond, message):
    if not cond:
        failures.append(message)
        print("  check failed: %s" % message)


def main():
    if not os.path.exists(ROM):
        print("missing %s -- run the main build first" % ROM)
        return 1

    syms = m64p_core.load_map_symbols(MAP, [
        "gTestWarpRequest", "gBingoRandomStarsActive",
        "gObjectPool", "gLoadedGraphNodes",
    ])

    os.environ.pop("WAYLAND_DISPLAY", None)
    os.environ["SDL_VIDEODRIVER"] = "x11"
    os.environ["SDL_AUDIODRIVER"] = "dummy"
    os.environ["BINGO_INPUT_SCRIPT"] = os.path.join(HERE, "scripts", "rando_enter.txt")

    core = m64p_core.Core(
        os.environ.get("M64P", os.path.expanduser("~/opt/m64p/install")),
        tempfile.mkdtemp(prefix="bingo64_rando_cfg_"))
    core.load_rom(ROM)
    core.attach_standard_plugins(os.path.join(HERE, "build", "input_script.so"))

    state = {"active": None, "stars": None}

    def read_star_positions():
        # gLoadedGraphNodes is a struct GraphNode ** (it points at the table).
        table = core.read_u32(syms["gLoadedGraphNodes"]) & 0x7FFFFFFF | 0x80000000
        purple_node = core.read_u32(table + 4 * MODEL_STAR_PURPLE)
        stars = []
        for i in range(OBJECT_POOL_CAPACITY):
            obj = syms["gObjectPool"] + i * OBJECT_SIZE
            if core.read_u32(obj + OFF_ACTIVE_FLAGS) >> 16 == 0:
                continue  # activeFlags is the top halfword; 0 = free slot
            if core.read_u32(obj + OFF_SHARED_CHILD) != purple_node:
                continue
            pos = tuple(
                struct.unpack(">f", struct.pack(">I", core.read_u32(obj + OFF_POS + 4 * j)))[0]
                for j in range(3))
            stars.append(pos)
        return stars

    def on_frame(frame):
        if frame == 450:
            core.write_u32(syms["gTestWarpRequest"], LEVEL_BOB)
        elif frame == 800:
            state["active"] = core.read_u32(syms["gBingoRandomStarsActive"])
            state["stars"] = read_star_positions()
        elif frame >= 810:
            core.stop()

    core.run(on_frame)
    core.shutdown()

    check(state["active"] == 1,
          "selecting the random-stars modifier did not activate it "
          "(active=%r)" % state["active"])
    stars = state["stars"] or []
    check(len(stars) == 3, "expected 3 purple stars, found %d: %r" % (len(stars), stars))
    minx, maxx, miny, maxy, minz, maxz = BOB_BOUNDS
    for pos in stars:
        check(minx <= pos[0] <= maxx and miny <= pos[1] <= maxy and minz <= pos[2] <= maxz,
              "star out of BOB spawn bounds: %r" % (pos,))
    for i in range(len(stars)):
        for j in range(i + 1, len(stars)):
            dx = stars[i][0] - stars[j][0]
            dy = stars[i][1] - stars[j][1]
            dz = stars[i][2] - stars[j][2]
            check(dx * dx + dy * dy + dz * dz > 100.0,
                  "stars %d and %d spawned on top of each other: %r %r"
                  % (i, j, stars[i], stars[j]))

    if failures:
        print("RANDO TEST FAILED (%d problems)" % len(failures))
        return 1
    print("rando test ok (3 purple stars in BOB at %s)"
          % ", ".join("(%.0f, %.0f, %.0f)" % p for p in stars))
    return 0


if __name__ == "__main__":
    sys.exit(main())
