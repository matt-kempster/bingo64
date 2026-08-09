#!/usr/bin/env python3
"""bingo64 asset pack generator.

Builds res/base.zip (textures + sound) from the user's own Super Mario 64 ROM,
so the game executable can be distributed without any Nintendo assets.

Usage: put your ROM next to this script as `baserom.us.z64` (US version,
big-endian .z64) and run:

    python gen_assets.py            (or drag the ROM onto make_assets.bat)

This is a user-side re-implementation of the repo build's `make res` steps:
asset extraction (extract_assets.py), skybox tile splitting (skyconv), ADPCM
sound encoding (tabledesign/vadpcm_enc + assemble_sound.py), and zipping
(mkzip.py). Tool binaries are expected under tools/ (prebuilt per-OS).
"""
import glob
import os
import platform
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = ".exe" if platform.system() == "Windows" else ""

# Must match the game build's settings (see Makefile: C_DEFINES and
# endian-and-bitwidth for the PC target).
SOUND_DEFINES = [
    "-DVERSION_US=1", "-DF3DEX_GBI_2E=1", "-DLIBULTRA_VERSION=9",
    "-DLIBULTRA_REVISION=0", '-DLIBULTRA_STR_VER="L"', "-DNON_MATCHING=1",
    "-DAVOID_UB=1",
]
ENDIAN_ARGS = ["--endian", "little", "--bitwidth", "64"]


def tool(name):
    for cand in (os.path.join(HERE, "tools", name + EXE),
                 os.path.join(HERE, "tools", "sm64tools", name + EXE)):
        if os.path.exists(cand):
            return cand
    sys.exit("missing tool: %s (broken package?)" % name)


def run(args, **kwargs):
    r = subprocess.run(args, **kwargs)
    if r.returncode != 0:
        sys.exit("step failed: %s" % " ".join(map(str, args[:3])))
    return r


def main():
    os.chdir(HERE)

    rom = sys.argv[1] if len(sys.argv) > 1 else "baserom.us.z64"
    if not os.path.exists(rom):
        sys.exit("Put your US Super Mario 64 ROM here as baserom.us.z64 "
                 "(or pass its path / drag it onto make_assets.bat).")
    if os.path.abspath(rom) != os.path.join(HERE, "baserom.us.z64"):
        shutil.copyfile(rom, "baserom.us.z64")

    print("== 1/5 extracting assets from ROM ==")
    run([sys.executable, "extract_assets_repo.py", "us"])

    print("== 2/5 splitting skybox tiles ==")
    # skyconv only understands forward slashes, even on Windows.
    tiles = "textures/skybox_tiles"
    os.makedirs(tiles, exist_ok=True)
    scrap = "out/skyconv_scrap"
    os.makedirs(scrap, exist_ok=True)
    for png in sorted(glob.glob(os.path.join("textures", "skyboxes", "*.png"))):
        run([tool("skyconv"), "--store-names", "--write-tiles", tiles,
             "--type", "sky", "--split", png.replace(os.sep, "/"), scrap])
    cake = os.path.join("levels", "ending", "cake.png")
    if os.path.exists(cake):
        run([tool("skyconv"), "--store-names", "--write-tiles", tiles,
             "--type", "cake", "--split", cake.replace(os.sep, "/"), scrap])

    print("== 3/5 encoding sound samples (this is the slow part) ==")
    samples_out = os.path.join("out", "sound", "samples")
    for aiff in sorted(glob.glob(os.path.join("sound", "samples", "*", "*.aiff"))):
        rel = os.path.relpath(aiff, os.path.join("sound", "samples"))
        dst_dir = os.path.join(samples_out, os.path.dirname(rel))
        os.makedirs(dst_dir, exist_ok=True)
        base = os.path.splitext(os.path.basename(aiff))[0]
        table = os.path.join(dst_dir, base + ".table")
        aifc = os.path.join(dst_dir, base + ".aifc")
        if os.path.exists(aifc):
            continue
        with open(table, "wb") as f:
            run([tool("tabledesign"), "-s", "1", aiff], stdout=f)
        run([tool("vadpcm_enc"), "-c", table, aiff, aifc])

    print("== 4/5 assembling sound banks and sequences ==")
    snd = os.path.join("out", "sound")
    run([sys.executable, os.path.join("tools", "assemble_sound.py"),
         samples_out + os.sep, os.path.join("sound", "sound_banks") + os.sep,
         os.path.join(snd, "sound_data.ctl"), os.path.join(snd, "ctl_header"),
         os.path.join(snd, "sound_data.tbl"), os.path.join(snd, "tbl_header")]
        + SOUND_DEFINES + ENDIAN_ARGS)
    seqs = sorted(glob.glob(os.path.join("sound", "sequences", "*.m64"))
                  + glob.glob(os.path.join("sound", "sequences", "us", "*.m64")),
                  key=os.path.basename)
    run([sys.executable, os.path.join("tools", "assemble_sound.py"),
         "--sequences", os.path.join(snd, "sequences.bin"),
         os.path.join(snd, "sequences_header"), os.path.join(snd, "bank_sets"),
         os.path.join("sound", "sound_banks") + os.sep,
         os.path.join("sound", "sequences.json")] + seqs
        + SOUND_DEFINES + ENDIAN_ARGS)

    print("== 5/5 packing res/base.zip ==")
    lst = os.path.join("out", "basepack.lst")
    with open(lst, "w") as f:
        for name in ("bank_sets", "sequences.bin", "sound_data.ctl",
                     "sound_data.tbl"):
            f.write("%s sound/%s\n" % (os.path.join(snd, name), name))
        for png in sorted(glob.glob(os.path.join(tiles, "*"))):
            f.write("%s gfx/%s\n" % (png, png.replace(os.sep, "/")))
        for top in ("actors", "levels", "textures"):
            for root, _, files in os.walk(top):
                if root.startswith(tiles):
                    continue
                for fn in sorted(files):
                    if fn.endswith(".png"):
                        p = os.path.join(root, fn)
                        f.write("%s gfx/%s\n" % (p, p.replace(os.sep, "/")))
    os.makedirs("res", exist_ok=True)
    run([sys.executable, os.path.join("tools", "mkzip.py"), lst,
         os.path.join("res", "base.zip")])

    print()
    print("Done! res/base.zip is ready — keep it next to the game executable.")


if __name__ == "__main__":
    main()
