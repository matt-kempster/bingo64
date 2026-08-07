#!/usr/bin/env python3
"""Reads bingo state out of emulated RAM and checks it against the
host-compiled board generator.

The run uses the same input script as the smoke test. At set frames it:
  - checks that gGlobalTimer keeps counting (the game loop is alive),
  - reads the seed and all 25 board cells from RAM,
  - renders them in the same text format as the host tester's board dump
    and diffs the two.

Run under xvfb (make test-ram does this).
"""

import os
import subprocess
import sys
import tempfile

import m64p_core

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, "..", "..")
ROM = os.path.join(REPO, "build", "us", "sm64.us.f3dex.z64")
MAP = os.path.join(REPO, "build", "us", "sm64.us.map")
HOST_TESTER = os.path.join(REPO, "test", "host", "build", "run_tests")

# The committed input script always produces this seed. If you change the
# script's menu timing, this changes too; update it from the failure message.
EXPECTED_SEED = 435573511

# BingoObjective layout in the N64 build (32-bit, big-endian):
# 0 initialized(u8+pad)  4 type  8 state  12 icon  16 class
# 20 title[30] (+2 pad)  52 union  -> 68 bytes per cell
CELL_SIZE = 68
OFF_TYPE, OFF_STATE, OFF_ICON, OFF_CLASS, OFF_TITLE, OFF_DATA = 4, 8, 12, 16, 20, 52

# Objective type numbers, matching enum BingoObjectiveType.
STAR_PLAIN = (0, 2, 7, 8, 9)
STAR_TIMED = 1
STAR_ABC = (3, 4, 5)
STAR_CLICK = 6
COURSE_COLLECT = (10, 11, 12, 13)
WALL_KICKS = 14
BOWSER = 15

failures = []


def check(cond, message):
    if not cond:
        failures.append(message)
        print("  check failed: %s" % message)


def dump_cell_from_ram(core, base, index):
    """Same output as dump_cell() in test/host/test_bingo.c."""
    addr = base + index * CELL_SIZE
    otype = core.read_s32(addr + OFF_TYPE)
    oclass = core.read_s32(addr + OFF_CLASS)
    icon = core.read_s32(addr + OFF_ICON)
    title = core.read_cstring(addr + OFF_TITLE, 30)
    d = [core.read_s32(addr + OFF_DATA + 4 * i) for i in range(4)]

    line = b'%02d type=%02d class=%d icon=%02d title="%s"' % (index, otype, oclass, icon, title)
    if otype in STAR_PLAIN:
        line += b" course=%d star=%d" % (d[0], d[1])
    elif otype in STAR_ABC:
        ptr = core.read_u32(addr + OFF_DATA + 8)
        hint = core.read_cstring((ptr & 0x7FFFFFFF) | 0x80000000) if ptr else b""
        line += b' course=%d star=%d hint="%s"' % (d[0], d[1], hint)
    elif otype == STAR_TIMED:
        line += b" course=%d star=%d maxTime=%d" % (d[0], d[1], d[2])
    elif otype == STAR_CLICK:
        line += b" course=%d star=%d maxClicks=%d" % (d[0], d[1], d[2])
    elif otype in COURSE_COLLECT:
        line += b" course=%d toGet=%d" % (d[0], d[1])
    elif otype == WALL_KICKS:
        line += b" toGetTotal=%d toGetEachCourse=%d" % (d[0], d[2])
    elif otype == BOWSER:
        line += b" level=%d" % d[0]
    else:
        line += b" toGet=%d" % d[0]
    return line


def main():
    for path, hint in ((ROM, "run the main build"), (MAP, "run the main build"),
                       (HOST_TESTER, "run make in test/host")):
        if not os.path.exists(path):
            print("missing %s -- %s first" % (path, hint))
            return 1

    syms = m64p_core.load_map_symbols(MAP, [
        "gGlobalTimer", "gBingoInitialized", "gBingoInitialSeed", "gBingoObjectives",
    ])

    os.environ.pop("WAYLAND_DISPLAY", None)
    os.environ["SDL_VIDEODRIVER"] = "x11"
    os.environ["SDL_AUDIODRIVER"] = "dummy"
    os.environ["BINGO_INPUT_SCRIPT"] = os.path.join(HERE, "scripts", "boot_and_board.txt")

    core = m64p_core.Core(
        os.environ.get("M64P", os.path.expanduser("~/opt/m64p/install")),
        tempfile.mkdtemp(prefix="bingo64_ram_cfg_"))
    core.load_rom(ROM)
    core.attach_standard_plugins(os.path.join(HERE, "build", "input_script.so"))

    state = {"timer_at_60": None, "seed": None, "ram_dump": None}

    def on_frame(frame):
        if frame == 60:
            state["timer_at_60"] = core.read_u32(syms["gGlobalTimer"])
        elif frame == 120:
            t2 = core.read_u32(syms["gGlobalTimer"])
            check(state["timer_at_60"] is not None and t2 > state["timer_at_60"],
                  "gGlobalTimer not advancing (%r -> %r)" % (state["timer_at_60"], t2))
        elif frame == 450:
            check(core.read_s32(syms["gBingoInitialized"]) == 1, "bingo never initialized")
            state["seed"] = core.read_u32(syms["gBingoInitialSeed"])
            state["ram_dump"] = b"\n".join(
                dump_cell_from_ram(core, syms["gBingoObjectives"], i) for i in range(25)
            ) + b"\n"
        elif frame >= 460:
            core.stop()

    core.run(on_frame)
    core.shutdown()

    check(state["seed"] is not None, "never reached the board-read frame")
    if state["seed"] is None:
        return 1
    check(state["seed"] == EXPECTED_SEED,
          "seed %d != expected %d (input script timing changed? update EXPECTED_SEED)"
          % (state["seed"], EXPECTED_SEED))

    host = subprocess.run([HOST_TESTER], env=dict(os.environ, BOARD_SEED=str(state["seed"])),
                          stdout=subprocess.PIPE, cwd=os.path.dirname(HOST_TESTER))
    check(host.returncode == 0, "host board generator failed")
    if host.stdout != state["ram_dump"]:
        check(False, "RAM board differs from host generator for seed %d" % state["seed"])
        ram_lines = state["ram_dump"].splitlines()
        host_lines = host.stdout.splitlines()
        for i, (a, b) in enumerate(zip(host_lines, ram_lines)):
            if a != b:
                print("  cell %02d host: %s" % (i, a.decode(errors="replace")))
                print("  cell %02d ram : %s" % (i, b.decode(errors="replace")))

    if failures:
        print("RAM TEST FAILED (%d problems)" % len(failures))
        return 1
    print("ram test ok (seed %d, board matches host generator)" % state["seed"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
