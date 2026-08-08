#!/usr/bin/env python3
"""Checks splatoon mode: floors Mario walks over get painted.

Boots, warps to BOB, selects the splatoon bingo modifier on the star
select screen (Z wraps the selection backwards from NONE to it), and
walks around. The painted-triangle counter must grow as Mario moves, and
the total-floors counter must be a sane level-sized number.

Run under xvfb (make test-splat does this).
"""

import os
import sys
import tempfile

import m64p_core

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, "..", "..")
ROM = os.path.join(REPO, "build", "us", "sm64.us.f3dex.z64")
MAP = os.path.join(REPO, "build", "us", "sm64.us.map")

LEVEL_BOB = 9

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
        "gTestWarpRequest", "gSplatoonEnabled",
        "gSplatoonPaintedCount", "gSplatoonTotalFloors",
    ])

    os.environ.pop("WAYLAND_DISPLAY", None)
    os.environ["SDL_VIDEODRIVER"] = "x11"
    os.environ["SDL_AUDIODRIVER"] = "dummy"
    os.environ["BINGO_INPUT_SCRIPT"] = os.path.join(HERE, "scripts", "splat_walk.txt")

    core = m64p_core.Core(
        os.environ.get("M64P", os.path.expanduser("~/opt/m64p/install")),
        tempfile.mkdtemp(prefix="bingo64_splat_cfg_"))
    core.load_rom(ROM)
    core.attach_standard_plugins(os.path.join(HERE, "build", "input_script.so"))

    state = {"enabled": None, "early": None, "late": None, "total": None}

    def on_frame(frame):
        if frame == 450:
            core.write_u32(syms["gTestWarpRequest"], LEVEL_BOB)
        elif frame == 740:
            state["enabled"] = core.read_u32(syms["gSplatoonEnabled"])
        elif frame == 820:
            state["early"] = core.read_u32(syms["gSplatoonPaintedCount"])
        elif frame == 995:
            state["late"] = core.read_u32(syms["gSplatoonPaintedCount"])
            state["total"] = core.read_u32(syms["gSplatoonTotalFloors"])
        elif frame >= 1005:
            core.stop()

    core.run(on_frame)
    core.shutdown()

    check(state["enabled"] == 1,
          "selecting the splatoon modifier star did not enable splatoon "
          "(enabled=%r)" % state["enabled"])
    check(state["early"] is not None and state["early"] >= 2,
          "no paint after landing (early=%r)" % state["early"])
    check(state["late"] is not None and state["late"] > (state["early"] or 0) + 3,
          "walking painted too little (early=%r late=%r)" % (state["early"], state["late"]))
    check(state["total"] is not None and 500 < state["total"] < 20000,
          "total floor count looks wrong (%r)" % state["total"])
    check((state["late"] or 0) < (state["total"] or 1),
          "painted more floors than exist")

    if failures:
        print("SPLATOON TEST FAILED (%d problems)" % len(failures))
        return 1
    print("splatoon test ok (painted %d then %d of %d floors in BOB)"
          % (state["early"], state["late"], state["total"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
