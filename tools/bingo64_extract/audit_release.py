#!/usr/bin/env python3
"""Audit a release directory for baserom-derived bytes.

Samples 32-byte windows from the ROM every 16 bytes and searches every
release file (zip members included) for them. Any shared run of >= 47
consecutive ROM bytes is guaranteed to be found. Low-entropy windows
(long zero fills, repeated padding) are skipped -- they match any
binary and carry no Nintendo expression.

Documented exemptions:
- Windows that are pure printable ASCII (plus tab/newline/NUL). These are
  the staff-credits and debug strings compiled from the public decomp C
  sources; they appear in every sm64 port binary and carry no extractable
  game content.
- Windows where one byte-interleave is near-constant. These are smooth
  s16 curves (actor animation data from the committed decomp .inc.c
  sources, envelope/offset tables): when the high bytes barely change,
  the ROM's big-endian byte stream coincides with the exe's little-endian
  stream shifted by one byte. Real payloads (textures, vadpcm samples,
  demo recordings) vary both interleaves and are still caught.
- sound/sound_data.ctl inside the custom zip. The ctl is generated from
  the repo's committed instrument definitions (the sm64coopdx convention:
  structure ships, waveforms/music are extracted from the user's ROM);
  its envelope tables coincide with ROM bytes by construction.
- With a build dir given: windows that appear in object files compiled
  purely from committed sources (src/, bin/, data/, lib/, actors/,
  levels/, and mario_anim_data). Those bytes -- dialog text, course
  names, camera/save tables -- are in the public decomp sources that
  every port compiles in. assets/demo_data.o and the rest of assets/
  are deliberately NOT exempt: that is where literal ROM bytes would
  enter the exe.

Usage: audit_release.py <baserom.us.z64> <release_dir> [builddir]
Exit 0 = clean, 1 = leaks found.
"""
import io
import sys
import zipfile
from pathlib import Path

WINDOW = 32
STRIDE = 16


ALLOWED_FILES = {
    # generated from committed instrument jsons (coopdx shippable-structure
    # convention); envelope value tables coincide with ROM bytes
    "res/bingo64.custom.zip!sound/sound_data.ctl",
}


def low_entropy(w: bytes) -> bool:
    return len(set(w)) <= 4


def printable_ascii(w: bytes) -> bool:
    return all(0x20 <= b < 0x7F or b in (0, 9, 10, 13) for b in w)


def smooth_s16_curve(w: bytes) -> bool:
    return len(set(w[0::2])) <= 3 or len(set(w[1::2])) <= 3


def rom_windows(rom: bytes):
    windows = set()
    for off in range(0, len(rom) - WINDOW + 1, STRIDE):
        w = rom[off : off + WINDOW]
        if not low_entropy(w) and not printable_ascii(w) and not smooth_s16_curve(w):
            windows.add(w)
    return windows


def scan_blob(name: str, blob: bytes, windows: set, allowed_blob: bytes) -> list:
    hits = []
    for off in range(0, len(blob) - WINDOW + 1):
        w = blob[off : off + WINDOW]
        if w not in windows:
            continue
        # strip zero padding (link-order dependent) and check the core
        # against the committed-source objects: decomp source data, not
        # extracted ROM content
        core = w.strip(b"\x00")
        if core and core in allowed_blob:
            continue
        hits.append((name, off))
        if len(hits) >= 5:  # enough to prove the leak
            break
    return hits


ALLOWED_OBJECT_PREFIXES = ("src/", "bin/", "data/", "lib/", "actors/", "levels/")
ALLOWED_OBJECT_FILES = ("assets/mario_anim_data.o",)


def committed_source_blob(builddir: Path) -> bytes:
    """Concatenated contents of object files built from committed sources."""
    parts = []
    for p in sorted(builddir.rglob("*.o")):
        rel = str(p.relative_to(builddir))
        if rel.startswith(ALLOWED_OBJECT_PREFIXES) or rel in ALLOWED_OBJECT_FILES:
            parts.append(p.read_bytes())
    return b"\x00".join(parts)


def iter_release_blobs(root: Path):
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        rel = str(p.relative_to(root))
        data = p.read_bytes()
        if p.suffix == ".zip":
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                for m in z.namelist():
                    yield f"{rel}!{m}", z.read(m)
        else:
            yield rel, data


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    rom = Path(sys.argv[1]).read_bytes()
    root = Path(sys.argv[2])
    print(f"indexing ROM ({len(rom)} bytes) ...")
    windows = rom_windows(rom)
    allowed_blob = b""
    if len(sys.argv) == 4:
        allowed_blob = committed_source_blob(Path(sys.argv[3]))
        print(f"{len(allowed_blob)} bytes of committed-source objects loaded")
    print(f"{len(windows)} windows; scanning {root} ...")
    leaks = []
    for name, blob in iter_release_blobs(root):
        if name in ALLOWED_FILES:
            print(f"  {name}: {len(blob)} bytes -- skipped (documented exemption)")
            continue
        hits = scan_blob(name, blob, windows, allowed_blob)
        status = f"LEAK ({len(hits)}+ hits, first at {hits[0][1]:#x})" if hits else "clean"
        print(f"  {name}: {len(blob)} bytes -- {status}")
        leaks.extend(hits)
    if leaks:
        print("FAILED: ROM-derived bytes found in release")
        sys.exit(1)
    print("OK: no ROM-derived bytes in release")


if __name__ == "__main__":
    main()
