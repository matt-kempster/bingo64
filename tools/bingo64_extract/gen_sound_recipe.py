#!/usr/bin/env python3
"""Build a byte-exact rebuild recipe for the four EXTERNAL_DATA sound files.

The audio toolchain output is deterministic for a given baserom, so instead
of running that toolchain on the user's machine, the release build records
how to reconstruct each output file from (a) ranges copied out of the ROM
and (b) small literal segments (structural headers, pointer tables). The
extractor replays this recipe.

Recipe stream format (little-endian):
  0x01 u32 rom_off u32 len   -> copy len bytes from rom[rom_off]
  0x02 u32 len, bytes        -> literal bytes
  0x00                       -> end of current file (files in FILES order)

Anything Nintendo-authored (samples, sequence data) is present in the ROM
and comes out as copy ops; literals stay a tiny structural fraction, which
gen_manifest.py prints so a human can sanity-check each release build.
"""
import os
import struct

FILES = ["bank_sets", "sequences.bin", "sound_data.ctl", "sound_data.tbl"]

MIN_MATCH = 16     # shortest copy worth encoding
PROBE = 16         # probe window for locating matches


def _longest_match(rom, out, pos):
    """Find the longest ROM match for out[pos:]; returns (rom_off, len) or None."""
    probe = out[pos:pos+PROBE]
    if len(probe) < MIN_MATCH:
        return None
    best = None
    start = 0
    while True:
        off = rom.find(probe, start)
        if off < 0:
            break
        n = PROBE
        limit = min(len(out) - pos, len(rom) - off)
        while n < limit and rom[off+n] == out[pos+n]:
            n += 1
        if best is None or n > best[1]:
            best = (off, n)
        start = off + 1
        if best[1] >= limit:
            break
    return best


def build_recipe(rom, builddir):
    recipe = bytearray()
    total = literal = 0
    for fname in FILES:
        out = open(os.path.join(builddir, "sound", fname), "rb").read()
        total += len(out)
        pos = 0
        lit_start = 0

        def flush_literal(end):
            nonlocal literal
            if end > lit_start:
                seg = out[lit_start:end]
                literal += len(seg)
                recipe.append(0x02)
                recipe.extend(struct.pack("<I", len(seg)))
                recipe.extend(seg)

        while pos < len(out):
            m = _longest_match(rom, out, pos)
            if m and m[1] >= MIN_MATCH:
                flush_literal(pos)
                recipe.append(0x01)
                recipe.extend(struct.pack("<II", m[0], m[1]))
                pos += m[1]
                lit_start = pos
            else:
                pos += 1
        flush_literal(pos)
        recipe.append(0x00)
    stats = f"{total} bytes total, {literal} literal ({100.0*literal/total:.2f}%)"
    return bytes(recipe), stats
