#!/usr/bin/env python3
"""Audit a release: every texture the exe names must come from somewhere.

An EXTERNAL_DATA exe looks textures up in res/ by name. At runtime each
name is satisfied either by bingo64-extract (ROM-derived textures listed
in manifest.inc) or by res/bingo64.custom.zip (committed bingo64 art). A
name covered by neither draws as the missing-texture checkerboard for
every player, whatever ROM they extracted from.

v1.0-beta.8 shipped that way: the nine enemy-objective icons were
committed in the source worktree but untracked in the stale ~/b64-win
checkout whose `git ls-files` built the custom zip, so the exe named
them and nothing supplied them.

Usage: audit_custom_zip.py <exe> <bingo64.custom.zip> <manifest.inc>
Exit 0 = clean, 1 = uncovered names found.
"""
import re
import sys
import zipfile
from pathlib import Path

FORMATS = "rgba16|rgba32|ia16|ia8|ia4|ia1|i8|i4|ci8|ci4"
EXE_NAME_RE = re.compile(
    rb"(?:actors|levels|textures)/[A-Za-z0-9_./-]+\.(?:" + FORMATS.encode() + rb")(?=\x00)")
MANIFEST_RE = re.compile(r'"((?:actors|levels|textures)/[^"]+)\.png"')

# The extractor rebuilds skybox tiles from the skybox blobs in the manifest;
# the per-tile names the exe uses never appear there literally.
EXTRACTOR_PREFIXES = ("textures/skybox_tiles/",)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    exe = Path(sys.argv[1]).read_bytes()
    names = {m.decode() for m in EXE_NAME_RE.findall(exe)}
    with zipfile.ZipFile(sys.argv[2]) as z:
        in_zip = {n[len("gfx/"):-len(".png")] for n in z.namelist()
                  if n.startswith("gfx/") and n.endswith(".png")}
    in_manifest = set(MANIFEST_RE.findall(Path(sys.argv[3]).read_text()))

    uncovered = sorted(n for n in names
                       if n not in in_zip and n not in in_manifest
                       and not n.startswith(EXTRACTOR_PREFIXES))
    print(f"{len(names)} texture names in {sys.argv[1]}: "
          f"{len(names & in_zip)} from the custom zip, "
          f"{len(names & in_manifest)} from the extractor")
    if uncovered:
        for n in uncovered:
            print(f"  NOT SHIPPED: {n}")
        print("FAILED: these textures would render as the missing-texture checkerboard;")
        print("        they are neither in bingo64.custom.zip nor in the extractor manifest.")
        print("        Committed art? Check that the zip list came from the SOURCE worktree")
        print("        (BINGO64_SRC / .bingo64_src), not a stale build checkout.")
        sys.exit(1)
    print("OK: every texture the exe names is shipped or extracted")


if __name__ == "__main__":
    main()
