#!/usr/bin/env python3
"""Audit an EXTERNAL_DATA build: every texture must reach the exe as a name.

Under EXTERNAL_DATA=1 each texture's generated <name>.inc.c holds the
zero-terminated res/ filename ("actors/x/y.rgba16"), which the engine
looks up in res/ at runtime. The embedded-mode rule writes raw pixel data
to the SAME path, so a build dir that once ran an embedded make can hand
pixel bytes to an external-data link. The engine then treats the pixels as
a filename, finds nothing, and draws the pink/black checkerboard -- for
every player, whatever their res/ (v1.0-beta.6.1 shipped nine such
textures). The ROM-bytes audit does not catch this: it exempts objects
compiled from actors/ and levels/, which is where these includes land.

Checks:
  1. every generated texture .inc.c decodes to its own res/ name;
  2. every texture name (skybox tiles included) is a NUL-terminated
     string in the exe -- or, when it is not (textures nothing references
     are dropped by the compiler), the texture's N64 pixel bytes are not
     in the exe either. The conversion mirrors tools/sm64tools/n64graphics
     so the byte search is exact.

Usage: audit_texture_names.py <builddir> <exe>   (run from the repo root)
Exit 0 = clean, 1 = problems found.
"""
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("audit_texture_names.py needs Pillow (python3 -m pip install pillow)")

HEX_RE = re.compile(r"0x([0-9A-Fa-f]{1,2})")
FORMATS = ("rgba16", "rgba32", "ia16", "ia8", "ia4", "ia1", "i8", "i4", "ci8", "ci4")


def decode_inc(path: Path) -> bytes:
    return bytes(int(h, 16) for h in HEX_RE.findall(path.read_text(errors="replace")))


def png_pixels(png: Path):
    """(channels, flat byte list) the way stb_image hands them to n64graphics."""
    im = Image.open(png)
    if im.mode == "LA":
        return 2, list(im.tobytes())
    if im.mode in ("RGBA",) or (im.mode == "P" and "transparency" in im.info):
        return 4, list(im.convert("RGBA").tobytes())
    if im.mode in ("RGB", "P"):
        return 3, list(im.convert("RGB").tobytes())
    return 0, []  # 1-channel grey: n64graphics refuses these too


def to_raw(png: Path, fmt: str) -> bytes:
    """Port of n64graphics' png2rgba/png2ia + rgba2raw/ia2raw/i2raw."""
    ch, d = png_pixels(png)
    if ch == 0:
        return b""
    n = len(d) // ch
    if fmt.startswith("rgba"):
        px = [(d[ch * i], d[ch * i + 1], d[ch * i + 2],
               d[ch * i + 3] if ch == 4 else 0xFF) if ch >= 3
              else (d[2 * i], d[2 * i], d[2 * i], d[2 * i + 1]) for i in range(n)]
        out = bytearray()
        if fmt == "rgba16":
            for r, g, b, a in px:
                r, g, b = ((r + 4) * 0x1F) // 0xFF, ((g + 4) * 0x1F) // 0xFF, ((b + 4) * 0x1F) // 0xFF
                a = 1 if a else 0
                out += bytes(((r << 3) | (g >> 2), ((g & 3) << 6) | (b << 1) | a))
        else:
            for p in px:
                out += bytes(p)
        return bytes(out)
    # intensity/alpha formats
    if ch == 2:
        ia = [(d[2 * i], d[2 * i + 1]) for i in range(n)]
    else:
        ia = [((d[ch * i] + d[ch * i + 1] + d[ch * i + 2] + 1) // 3,
               d[ch * i + 3] if ch == 4 else 0xFF) for i in range(n)]
    depth = int(fmt[2:] if fmt.startswith("ia") else fmt[1:])
    size = (n * depth + 7) // 8
    out = bytearray(size)
    if fmt == "ia16":
        for i, (v, a) in enumerate(ia):
            out[2 * i], out[2 * i + 1] = v, a
    elif fmt == "ia8":
        for i, (v, a) in enumerate(ia):
            out[i] = ((v // 0x11) << 4) | (a // 0x11)
    elif fmt == "ia4":
        for i, (v, a) in enumerate(ia):
            nib = ((v // 0x24) << 1) | (1 if a else 0)
            out[i // 2] |= nib if i % 2 else nib << 4
    elif fmt == "ia1":
        for i, (v, _) in enumerate(ia):
            if v:
                out[i // 8] |= 1 << (7 - i % 8)
    elif fmt == "i8":
        for i, (v, _) in enumerate(ia):
            out[i] = v
    elif fmt == "i4":
        for i, (v, _) in enumerate(ia):
            out[i // 2] |= (v // 0x11) if i % 2 else (v // 0x11) << 4
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    build = Path(sys.argv[1])
    exe = Path(sys.argv[2]).read_bytes()
    bad_inc, poisoned, unverified = [], [], []
    checked = unreferenced = 0

    def check_exe(name: str, png: Path):
        nonlocal unreferenced
        if name.encode() + b"\x00" in exe:
            return
        fmt = name.rsplit(".", 1)[-1]
        if fmt not in FORMATS or fmt.startswith("ci"):
            unverified.append(name)  # CI: palette+index encoding not mirrored
            return
        raw = to_raw(png, fmt)
        if len(raw) < 128 or len(set(raw)) <= 1:
            unverified.append(name)  # blank/tiny bitmaps match anything
        elif raw in exe:
            poisoned.append(name)
        else:
            unreferenced += 1

    for inc in sorted(build.rglob("*.inc.c")):
        rel = inc.relative_to(build)
        name = str(rel)[: -len(".inc.c")]
        png = Path(name + ".png")
        if not png.is_file() or name.startswith("levels/ending/cake"):
            continue  # not a converted texture (anim data, skyconv cake tiles)
        checked += 1
        data = decode_inc(inc).rstrip(b"\x00")
        if data != name.encode():
            bad_inc.append((str(rel), len(data)))
        check_exe(name, png)

    tiles = build / "textures" / "skybox_tiles"
    for tile in sorted(tiles.glob("*.png")) if tiles.is_dir() else []:
        checked += 1
        check_exe("textures/skybox_tiles/" + tile.name[: -len(".png")], tile)

    print(f"{checked} textures checked against {sys.argv[2]} "
          f"({unreferenced} unreferenced by the build, pixels absent as expected)")
    for rel, n in bad_inc:
        print(f"  PIXEL DATA in {rel} ({n} bytes; expected the res/ name)")
    for name in poisoned:
        print(f"  PIXELS IN EXE, NAME MISSING: {name}")
    if unverified:
        print(f"  ({len(unverified)} unreferenced names could not be pixel-verified: "
              "blank/tiny bitmaps or CI formats)")
    if bad_inc or poisoned:
        print("FAILED: these textures would render as the missing-texture checkerboard;")
        print("        rebuild with EXTERNAL_DATA=1 (the .texture_mode stamp forces "
              "regeneration; `rm -rf build/us_pc` if in doubt)")
        sys.exit(1)
    print("OK: every texture reaches the exe by name")


if __name__ == "__main__":
    main()
