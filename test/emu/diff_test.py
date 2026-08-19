#!/usr/bin/env python3
"""Frame-differential harness: proves two bingo64 ROMs simulate Mario
identically under identical scripted inputs.

This is the gatekeeper for the vanilla-by-construction policy: a refactor
of sim code may be reintegrated only when this test shows ZERO divergence
(bit-exact Mario state, every frame) against the reference ROM. The
reference is the last pre-alo build; see `make test-diff`.

How it works
------------
Each ROM runs in its own subprocess (one emulator core per process) with
the same input script. The run "syncs" on the first frame where Mario is
idle (ACT_IDLE) in the target level; from that frame on, a fixed window of
raw MarioState bytes is recorded per frame. The two recordings must have
the same sync frame (same absolute boot timing — inputs are absolute) and
byte-identical state on every recorded frame. On divergence, the first
differing frame is decoded field-by-field.

State compared per frame (raw big-endian bytes, zero tolerance):
action, intendedMag/intendedYaw, invincTimer, jump/wallkick timers,
faceAngle+angleVel+slideYaw+twirlYaw, pos, vel, forwardVel, slideVelX/Z,
ceilHeight, floorHeight. Surface/object POINTERS are deliberately skipped
(addresses legitimately differ between builds).

Both trees' MarioState layouts are identical through offset 0xA4 (checked
by eye against include/types.h in each tree, and sanity-checked at sync
time by requiring a plausible ACT_IDLE). If a future tree reorders the
struct, update CHUNKS below per side.

Authoring more corpus scripts: copy scripts/diff_move_bob.txt as a
template — boot timing (START at 150, A at 320, A at 700 for star select)
must stay fixed; the harness pokes gTestWarpRequest at --warp-frame to
choose the course; everything after ~750 is free movement. Aim inputs at
the mechanic under suspicion (slopes, walls, water, platforms).

Usage:
  orchestrate (normal):
    diff_test.py --rom-a A.z64 --map-a A.map --rom-b B.z64 --map-b B.map \
                 --script scripts/diff_move_bob.txt --warp 9 --frames 650
  record one side (called internally):
    diff_test.py --record out.json --rom R.z64 --map R.map --script S \
                 [--warp N] [--warp-frame 450] --sync-level L --frames N
"""

import argparse
import json
import os
import subprocess
import sys
import struct
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

ACT_IDLE = 0x0C400201

# (offset into gMarioStates[0], length, decode spec) — decode spec is a
# list of (name, fmt) consuming the chunk in order; ">" big-endian.
CHUNKS = [
    (0x0C, 4, [("action", ">I")]),
    (0x20, 8, [("intendedMag", ">f"), ("intendedYaw", ">h"), ("invincTimer", ">h")]),
    (0x28, 4, [("framesSinceA", ">B"), ("framesSinceB", ">B"),
               ("wallKickTimer", ">B"), ("doubleJumpTimer", ">B")]),
    (0x2C, 16, [("faceAngle", ">3h"), ("angleVel", ">3h"),
                ("slideYaw", ">h"), ("twirlYaw", ">h")]),
    (0x3C, 36, [("pos", ">3f"), ("vel", ">3f"), ("forwardVel", ">f"),
                ("slideVelX", ">f"), ("slideVelZ", ">f")]),
    (0x6C, 8, [("ceilHeight", ">f"), ("floorHeight", ">f")]),
]


def read_state(core, base):
    return b"".join(core.read_bytes(base + off, ln) for off, ln, _ in CHUNKS)


def decode_state(blob):
    out = {}
    pos = 0
    for _, ln, spec in CHUNKS:
        chunk = blob[pos:pos + ln]
        pos += ln
        o = 0
        for name, fmt in spec:
            sz = struct.calcsize(fmt)
            vals = struct.unpack(fmt, chunk[o:o + sz])
            out[name] = vals[0] if len(vals) == 1 else vals
            o += sz
    return out


def fmt_val(v):
    if isinstance(v, float):
        return "%.9g (0x%08X)" % (v, struct.unpack(">I", struct.pack(">f", v))[0])
    if isinstance(v, tuple):
        return "(" + ", ".join(fmt_val(x) for x in v) + ")"
    if isinstance(v, int) and v > 0xFFFF:
        return "0x%08X" % v
    return str(v)


def record(args):
    import m64p_core

    syms = m64p_core.load_map_symbols(args.map, [
        "gMarioStates", "gCurrLevelNum", "gTestWarpRequest", "gGlobalTimer",
    ])
    base = syms["gMarioStates"]

    os.environ.pop("WAYLAND_DISPLAY", None)
    os.environ["SDL_VIDEODRIVER"] = "x11"
    os.environ["SDL_AUDIODRIVER"] = "dummy"
    os.environ["BINGO_INPUT_SCRIPT"] = args.script

    core = m64p_core.Core(
        os.environ.get("M64P", os.path.expanduser("~/opt/m64p/install")),
        tempfile.mkdtemp(prefix="bingo64_diff_cfg_"))
    core.load_rom(args.rom)
    core.attach_standard_plugins(os.path.join(HERE, "build", "input_script.so"))

    def read_s16(addr):
        b = core.read_bytes(addr, 2)
        v = (b[0] << 8) | b[1]
        return v - 0x10000 if v >= 0x8000 else v

    state = {"sync": None, "frames": [], "gave_up": False}
    deadline = args.warp_frame + 2000

    def on_frame(frame):
        if args.warp and frame == args.warp_frame:
            core.write_u32(syms["gTestWarpRequest"], args.warp)
        if state["sync"] is None:
            if frame > deadline:
                state["gave_up"] = True
                core.stop()
                return
            action = core.read_u32(base + 0x0C)
            if action == ACT_IDLE and read_s16(syms["gCurrLevelNum"]) == args.sync_level:
                state["sync"] = frame
        if state["sync"] is not None:
            state["frames"].append(read_state(core, base).hex())
            if len(state["frames"]) >= args.frames:
                core.stop()

    core.run(on_frame)
    core.shutdown()

    if state["gave_up"] or state["sync"] is None:
        print("record: never reached sync (idle in level %d)" % args.sync_level)
        return 1
    with open(args.record, "w") as f:
        json.dump({"rom": args.rom, "sync_frame": state["sync"],
                   "frames": state["frames"]}, f)
    return 0


def run_side(args, rom, mapfile, out):
    cmd = [sys.executable, os.path.abspath(__file__), "--record", out,
           "--rom", rom, "--map", mapfile, "--script", args.script,
           "--sync-level", str(args.sync_level), "--frames", str(args.frames),
           "--warp-frame", str(args.warp_frame)]
    if args.warp:
        cmd += ["--warp", str(args.warp)]
    r = subprocess.run(cmd)
    if r.returncode != 0:
        raise SystemExit("recording failed for %s" % rom)
    with open(out) as f:
        return json.load(f)


def orchestrate(args):
    tmp = tempfile.mkdtemp(prefix="bingo64_diff_")
    print("[A] %s" % args.rom_a)
    a = run_side(args, args.rom_a, args.map_a, os.path.join(tmp, "a.json"))
    print("[B] %s" % args.rom_b)
    b = run_side(args, args.rom_b, args.map_b, os.path.join(tmp, "b.json"))

    if a["sync_frame"] != b["sync_frame"]:
        print("DIFF FAIL: sync frames differ (A=%d, B=%d) — boot timing has"
              " diverged; absolute-frame inputs are not comparable. Author a"
              " per-ROM script offset before trusting any frame comparison."
              % (a["sync_frame"], b["sync_frame"]))
        return 1

    n = min(len(a["frames"]), len(b["frames"]))
    for i in range(n):
        if a["frames"][i] != b["frames"][i]:
            da = decode_state(bytes.fromhex(a["frames"][i]))
            db = decode_state(bytes.fromhex(b["frames"][i]))
            print("DIVERGENCE at sync+%d (absolute frame %d):"
                  % (i, a["sync_frame"] + i))
            for k in da:
                if da[k] != db[k]:
                    print("  %-16s A=%s" % (k, fmt_val(da[k])))
                    print("  %-16s B=%s" % ("", fmt_val(db[k])))
            if i > 0:
                prev = decode_state(bytes.fromhex(a["frames"][i - 1]))
                print("  (frame before: action=%s pos=%s)"
                      % (fmt_val(prev["action"]), fmt_val(prev["pos"])))
            return 1
    print("DIFF PASS: %d frames bit-identical from sync frame %d"
          % (n, a["sync_frame"]))
    return 0


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--record", help="(internal) record one ROM to this JSON")
    p.add_argument("--rom"), p.add_argument("--map")
    p.add_argument("--rom-a"), p.add_argument("--map-a")
    p.add_argument("--rom-b"), p.add_argument("--map-b")
    p.add_argument("--script", required=True)
    p.add_argument("--warp", type=int, default=0,
                   help="level to poke into gTestWarpRequest (0 = no warp)")
    p.add_argument("--warp-frame", type=int, default=450)
    p.add_argument("--sync-level", type=int, default=9,
                   help="level where the idle sync point is awaited")
    p.add_argument("--frames", type=int, default=650,
                   help="frames to compare after the sync point")
    args = p.parse_args()

    if args.record:
        if not (args.rom and args.map):
            p.error("--record needs --rom and --map")
        return record(args)
    for req in ("rom_a", "map_a", "rom_b", "map_b"):
        if getattr(args, req) is None:
            p.error("--%s is required" % req.replace("_", "-"))
    return orchestrate(args)


if __name__ == "__main__":
    sys.exit(main())
