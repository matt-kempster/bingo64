#!/usr/bin/env python3
"""Count static floor triangles per main course from level collision data.

Replicates surface_load.c's read_surface_data: a triangle is a floor when
its (unnormalized) cross-product normal has y/mag > 0.01; degenerate
triangles (mag < 0.0001) are skipped, matching the game.

Preprocessor conditionals matter: RR's and LLL's collision files carry
both JP and US variants behind #ifdef VERSION_JP. Counting the raw file
without preprocessing double-counts those courses (that bug produced the
original RR=1113 / LLL=1266 values). This script keeps only the US branch.

Run from the repo root:  python3 test/count_floors.py
Then update course_floors in src/game/bingo_const.c if anything moved.
"""
import glob
import math
import re

COURSES = ["bob", "wf", "jrb", "ccm", "bbh", "hmc", "lll", "ssl", "ddd",
           "sl", "wdw", "ttm", "thi", "ttc", "rr"]


def preprocess_us(lines):
    """Keep lines active in a US build (VERSION_US defined, others not)."""
    out, stack = [], []
    for ln in lines:
        s = ln.strip()
        m = re.match(r"#\s*(ifdef|ifndef)\s+(\w+)", s)
        if m:
            defined = m.group(2) == "VERSION_US"
            stack.append(defined if m.group(1) == "ifdef" else not defined)
            continue
        if re.match(r"#\s*else", s):
            stack[-1] = not stack[-1]
            continue
        if re.match(r"#\s*endif", s):
            stack.pop()
            continue
        if all(stack):
            out.append(ln)
    return out


def count_floors(path):
    text = "".join(preprocess_us(open(path).readlines()))
    verts, floors = [], 0
    pat = r"COL_(VERTEX|TRI|TRI_SPECIAL)\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)"
    for m in re.finditer(pat, text):
        a, b, c = int(m.group(2)), int(m.group(3)), int(m.group(4))
        if m.group(1) == "VERTEX":
            verts.append((a, b, c))
            continue
        x1, y1, z1 = verts[a]
        x2, y2, z2 = verts[b]
        x3, y3, z3 = verts[c]
        nx = (y2 - y1) * (z3 - z2) - (z2 - z1) * (y3 - y2)
        ny = (z2 - z1) * (x3 - x2) - (x2 - x1) * (z3 - z2)
        nz = (x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2)
        mag = math.sqrt(nx * nx + ny * ny + nz * nz)
        if mag < 0.0001:
            continue
        if ny / mag > 0.01:
            floors += 1
    return floors


if __name__ == "__main__":
    for course in COURSES:
        areas = sorted(glob.glob(f"levels/{course}/areas/*/collision.inc.c"))
        per_area = [count_floors(f) for f in areas]
        detail = " + ".join(str(n) for n in per_area) if len(per_area) > 1 else ""
        print(f"{course:5} {sum(per_area):5}  {detail}")
