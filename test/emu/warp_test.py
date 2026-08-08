#!/usr/bin/env python3
"""Checks the test warp seam: writing a level number to gTestWarpRequest
makes the game warp there through its own painting-entry code.

The run boots to castle grounds, then the harness writes LEVEL_BOB into
gTestWarpRequest. The game must show the star select screen (so tests can
pick a specific act), and after the scripted A press Mario must be
standing in the course.

Run under xvfb (make test-warp does this).
"""

import os
import sys
import tempfile

import m64p_core

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, "..", "..")
ROM = os.path.join(REPO, "build", "us", "sm64.us.f3dex.z64")
MAP = os.path.join(REPO, "build", "us", "sm64.us.map")

LEVEL_CASTLE_GROUNDS = 16
LEVEL_BOB = 9
COURSE_BOB = 1

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
        "gTestWarpRequest", "gCurrLevelNum", "gCurrCourseNum", "gCurrActNum",
        "gGlobalTimer", "gMarioObject",
    ])

    os.environ.pop("WAYLAND_DISPLAY", None)
    os.environ["SDL_VIDEODRIVER"] = "x11"
    os.environ["SDL_AUDIODRIVER"] = "dummy"
    os.environ["BINGO_INPUT_SCRIPT"] = os.path.join(HERE, "scripts", "warp_bob.txt")

    core = m64p_core.Core(
        os.environ.get("M64P", os.path.expanduser("~/opt/m64p/install")),
        tempfile.mkdtemp(prefix="bingo64_warp_cfg_"))
    core.load_rom(ROM)
    core.attach_standard_plugins(os.path.join(HERE, "build", "input_script.so"))

    def read_s16(addr):
        b = core.read_bytes(addr, 2)
        v = (b[0] << 8) | b[1]
        return v - 0x10000 if v >= 0x8000 else v

    state = {"timer_at_60": None}

    def on_frame(frame):
        if frame == 60:
            state["timer_at_60"] = core.read_u32(syms["gGlobalTimer"])
        elif frame == 120:
            t2 = core.read_u32(syms["gGlobalTimer"])
            check(state["timer_at_60"] is not None and t2 > state["timer_at_60"],
                  "gGlobalTimer not advancing (%r -> %r)" % (state["timer_at_60"], t2))
        elif frame == 440:
            check(read_s16(syms["gCurrLevelNum"]) == LEVEL_CASTLE_GROUNDS,
                  "not in castle grounds before the warp")
        elif frame == 450:
            core.write_u32(syms["gTestWarpRequest"], LEVEL_BOB)
        elif frame == 600:
            # Star select screen: right level and course, no act chosen yet,
            # Mario despawned, and the game consumed the request.
            check(read_s16(syms["gCurrLevelNum"]) == LEVEL_BOB, "warp did not reach BOB")
            check(read_s16(syms["gCurrCourseNum"]) == COURSE_BOB, "course number wrong")
            check(read_s16(syms["gCurrActNum"]) == 0, "act chosen before star select")
            check(core.read_u32(syms["gMarioObject"]) == 0, "mario present on star select")
            check(core.read_u32(syms["gTestWarpRequest"]) == 0, "warp request not cleared")
        elif frame == 750:
            # After the scripted A press: act 1 picked, Mario spawned in BOB.
            check(read_s16(syms["gCurrActNum"]) == 1, "act not selected")
            check(core.read_u32(syms["gMarioObject"]) != 0, "mario not spawned in BOB")
        elif frame >= 760:
            core.stop()

    core.run(on_frame)
    core.shutdown()

    if failures:
        print("WARP TEST FAILED (%d problems)" % len(failures))
        return 1
    print("warp test ok (castle grounds -> star select -> BOB act 1)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
