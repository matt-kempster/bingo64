#!/usr/bin/env python3
"""Boots the built ROM headless and checks that it plays and renders.

The input script walks the game from power-on to gameplay and then holds L
to show the bingo board. Screenshots are taken along the way and checked:
they must exist, must not be a single flat color, and must change between
scenes. A reference shot of the board screen guards rendering; re-bless it
with UPDATE_GOLDENS=1 after an intentional change.
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROM = os.path.join(HERE, "..", "..", "build", "us", "sm64.us.z64")
SCRIPT = os.path.join(HERE, "scripts", "boot_and_board.txt")
GOLDEN_BOARD_SHOT = os.path.join(HERE, "golden", "board_screen.png")

# Frame numbers to capture: seed screen, gameplay, board held open.
SHOT_FRAMES = [120, 470, 560]

failures = []


def check(cond, message):
    if not cond:
        failures.append(message)
        print("  check failed: %s" % message)


def decode_png(path):
    """Returns (width, height, list of RGB rows). Minimal PNG reader."""
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a png"
    pos = 8
    width = height = None
    bit_depth = color_type = None
    idat = b""
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", body[:10])
            assert bit_depth == 8, "unsupported bit depth"
            assert color_type in (2, 6), "unsupported color type"
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break
        pos += 12 + length
    raw = zlib.decompress(idat)
    channels = 3 if color_type == 2 else 4
    stride = width * channels
    rows = []
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        filt = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if filt == 1:  # sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filt == 2:  # up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:  # average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filt == 4:  # paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        prev = line
        if channels == 4:
            rows.append(bytes(b for i, b in enumerate(line) if i % 4 != 3))
        else:
            rows.append(bytes(line))
    return width, height, rows


def flat_stats(rows):
    """Mean and rough spread of the pixel bytes, sampled."""
    total = 0
    count = 0
    values = []
    for row in rows[::8]:
        for b in row[::16]:
            values.append(b)
            total += b
            count += 1
    mean = total / count
    spread = sum(abs(v - mean) for v in values) / count
    return mean, spread


def image_difference(rows_a, rows_b):
    diff = 0
    count = 0
    for ra, rb in zip(rows_a[::8], rows_b[::8]):
        for a, b in zip(ra[::16], rb[::16]):
            diff += abs(a - b)
            count += 1
    return diff / count


def main():
    if not os.path.exists(ROM):
        print("ROM not found at %s -- run the main build first" % ROM)
        return 1

    shotdir = tempfile.mkdtemp(prefix="bingo64_smoke_")
    frames = ",".join(str(f) for f in SHOT_FRAMES)
    proc = subprocess.run(
        [os.path.join(HERE, "run_rom.sh"), ROM, shotdir, frames, SCRIPT],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600,
    )
    log = proc.stdout.decode(errors="replace")

    check(proc.returncode == 0, "emulator exited with code %d" % proc.returncode)
    check(log.count("Captured screenshot") == len(SHOT_FRAMES),
          "expected %d screenshots, log shows %d"
          % (len(SHOT_FRAMES), log.count("Captured screenshot")))

    shots = sorted(
        os.path.join(shotdir, f) for f in os.listdir(shotdir) if f.endswith(".png")
    )
    check(len(shots) == len(SHOT_FRAMES),
          "expected %d png files, found %d" % (len(SHOT_FRAMES), len(shots)))
    if len(shots) != len(SHOT_FRAMES):
        print("\n".join(failures))
        return 1

    decoded = []
    for path in shots:
        w, h, rows = decode_png(path)
        decoded.append(rows)
        mean, spread = flat_stats(rows)
        check(spread > 8, "%s looks like a flat color (spread %.1f)"
              % (os.path.basename(path), spread))

    for i in range(len(decoded) - 1):
        d = image_difference(decoded[i], decoded[i + 1])
        check(d > 4, "shots %d and %d are nearly identical (diff %.1f); frozen?"
              % (i, i + 1, d))

    # The last shot shows the bingo board. Compare against the blessed one.
    board = decoded[-1]
    if os.environ.get("UPDATE_GOLDENS"):
        os.makedirs(os.path.dirname(GOLDEN_BOARD_SHOT), exist_ok=True)
        with open(shots[-1], "rb") as src, open(GOLDEN_BOARD_SHOT, "wb") as dst:
            dst.write(src.read())
        print("  wrote %s" % GOLDEN_BOARD_SHOT)
    elif os.path.exists(GOLDEN_BOARD_SHOT):
        _, _, golden = decode_png(GOLDEN_BOARD_SHOT)
        d = image_difference(board, golden)
        check(d < 8, "board screen drifted from golden (diff %.1f); "
              "UPDATE_GOLDENS=1 to re-bless" % d)
    else:
        print("  no golden board shot yet (UPDATE_GOLDENS=1 to create)")

    if failures:
        print("SMOKE TEST FAILED (%d problems), shots kept in %s" % (len(failures), shotdir))
        return 1
    print("smoke test ok (shots in %s)" % shotdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
