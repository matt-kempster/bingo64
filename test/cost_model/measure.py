#!/usr/bin/env python3
"""Stage 2: run the cost model over real master boards.

    python3 measure.py [--boards DIR] [--limit N] [--jobs 8] [--examples]

Per board: exact cost of each of the 12 lines, spread (max/min, CV), synergy
per line (sum standalone - exact), strong pairs, near-tie count (lines within
10% of the cheapest), which structural line is cheapest, type repetition.
Writes out/master_boards.jsonl and prints a summary. --examples prints 10
varied real lines with their plans for a gut check.
"""
import argparse
import glob
import json
import math
import os
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import data as D  # noqa: E402
import model as M  # noqa: E402

DEFAULT_BOARDS = ("/tmp/claude-1000/-mnt-c-users-matt-documents-git-bingo64/"
                  "419e5d66-afab-4331-bb83-09923dcff035/scratchpad/boards")
STRONG = 450      # ds: a pair saving >= 45 s counts as a strong overlap
SYN_MIN = 600     # ds: "line has >= 1 min of synergy"

WILD = {"MULTICOIN", "MULTISTAR", "STARS_MULTIPLE_LEVELS", "LIVES", "UNIQUE_DEATHS", "BLJ",
        "DANGEROUS_WALL_KICKS", "RED_COIN_STARS"}


def line_kind(name):
    if name in ("row2", "col2"):
        return "middle"
    if name.startswith("diag"):
        return "diagonal"
    return "other"


def board_metrics(m, cells, lines=True):
    """Metrics for one 25-tile board (cells in index order)."""
    sa = [m.standalone_cost(t) for t in cells]
    out = {"standalone": sa, "types": Counter(t.type for t in cells).most_common(1)[0][1]}
    if not lines:
        return out
    lc, syn, strong = [], [], []
    for ln in D.LINES:
        tiles = [cells[i] for i in ln]
        e = m.exact_set_cost(tiles)
        lc.append(e)
        syn.append(sum(sa[i] for i in ln) - e)
        sp = 0
        for a in range(5):
            for b in range(a + 1, 5):
                if m.pair_synergy(tiles[a], tiles[b]) >= STRONG:
                    sp += 1
        strong.append(sp)
    mn = min(lc)
    mean = sum(lc) / 12.0
    sd = math.sqrt(sum((x - mean) ** 2 for x in lc) / 12.0)
    cheapest = D.LINE_NAMES[lc.index(mn)]
    out.update({
        "line_cost": lc, "synergy": syn, "strong_pairs": strong,
        "ratio": max(lc) / float(mn), "cv": sd / mean,
        "near_ties": sum(1 for x in lc if x * 10 <= mn * 11),
        "cheapest": cheapest,
    })
    return out


_M = None


def _work(path):
    global _M
    if _M is None:
        _M = M.Model()
    cells = M.load_board(path)
    if not cells:
        return None
    r = board_metrics(_M, cells)
    r["seed"] = int(os.path.basename(path)[:-4])
    return r


def summarize(rows, label, out=sys.stdout):
    n = len(rows)
    lines = n * 12

    def q(xs, p):
        xs = sorted(xs)
        return xs[min(len(xs) - 1, int(p * len(xs)))]
    ratio = [r["ratio"] for r in rows]
    cv = [r["cv"] for r in rows]
    syn = [s for r in rows for s in r["synergy"]]
    lc = [c for r in rows for c in r["line_cost"]]
    strong = [s for r in rows for s in r["strong_pairs"]]
    ties = [r["near_ties"] for r in rows]
    cheap = Counter(r["cheapest"] for r in rows)
    kinds = Counter(line_kind(r["cheapest"]) for r in rows)
    rep4 = sum(1 for r in rows if r["types"] >= 4)
    p = lambda s: print(s, file=out)  # noqa: E731
    p("== %s: %d boards, %d lines ==" % (label, n, lines))
    p("line cost (exact): median %s, p10 %s, p90 %s" % (
        M.fmt_time(q(lc, .5)), M.fmt_time(q(lc, .1)), M.fmt_time(q(lc, .9))))
    p("within-board spread max/min: median %.2f, p90 %.2f;  CV median %.3f, p90 %.3f" % (
        q(ratio, .5), q(ratio, .9), q(cv, .5), q(cv, .9)))
    p("synergy per line: mean %s, median %s, p90 %s; share >= 1 min: %.1f%%; negative: %.1f%%" % (
        M.fmt_time(int(sum(syn) / len(syn))), M.fmt_time(q(syn, .5)), M.fmt_time(q(syn, .9)),
        100.0 * sum(1 for s in syn if s >= SYN_MIN) / lines,
        100.0 * sum(1 for s in syn if s < 0) / lines))
    p("strong pairs (>= %ds saved) per line: 0: %.1f%%, 1: %.1f%%, 2: %.1f%%, 3+: %.1f%%" % (
        STRONG // 10, *[100.0 * sum(1 for s in strong if (s == k if k < 3 else s >= 3)) / lines
                        for k in range(4)]))
    p("near-tie lines (within 10%% of cheapest): mean %.2f, share of boards with a unique best: %.1f%%" % (
        sum(ties) / float(n), 100.0 * sum(1 for t in ties if t == 1) / n))
    p("cheapest line kind: middle row/col %.1f%% (2/12 = 16.7%%), diagonal %.1f%% (16.7%%), other %.1f%% (66.7%%)" % (
        100.0 * kinds["middle"] / n, 100.0 * kinds["diagonal"] / n, 100.0 * kinds["other"] / n))
    p("cheapest line by name: " + ", ".join("%s %.1f%%" % (k, 100.0 * v / n)
                                           for k, v in sorted(cheap.items())))
    p("boards with >= 4 copies of one type: %.1f%%" % (100.0 * rep4 / n))


def run(board_dir, limit, jobs):
    from multiprocessing import Pool
    files = sorted(glob.glob(os.path.join(board_dir, "*.txt")),
                   key=lambda p: int(os.path.basename(p)[:-4]))[:limit]
    with Pool(jobs) as pool:
        rows = [r for r in pool.map(_work, files, chunksize=8) if r]
    return rows


def examples(board_dir, count=10):
    """Pick varied real lines (deterministic) and print their plans."""
    m = M.Model()
    cands = []
    for seed in range(1, 301):
        cells = M.load_board(os.path.join(board_dir, "%d.txt" % seed))
        if not cells:
            continue
        for li, ln in enumerate(D.LINES):
            tiles = [cells[i] for i in ln]
            e = m.exact_set_cost(tiles)
            sa = [m.standalone_cost(t) for t in tiles]
            cands.append((seed, li, tiles, e, sum(sa) - e))
    picks = []
    used = set()

    def take(label, pred, key):
        pool = [c for c in cands if pred(c) and (c[0], c[1]) not in used]
        if not pool:
            return
        c = sorted(pool, key=key)[0]
        used.add((c[0], c[1]))
        picks.append((label, c))
    types = lambda c: {t.type for t in c[2]}  # noqa: E731
    med = sorted(c[3] for c in cands)[len(cands) // 2]
    take("largest synergy", lambda c: True, lambda c: -c[4])
    take("anti-synergy (deaths vs lives)", lambda c: {"LIVES", "UNIQUE_DEATHS"} <= types(c),
         lambda c: c[4])
    take("cheapest line", lambda c: True, lambda c: c[3])
    take("most expensive line", lambda c: True, lambda c: -c[3])
    take("typical: median cost, no synergy", lambda c: c[4] < 100, lambda c: abs(c[3] - med))
    take("typical: median cost, some synergy", lambda c: c[4] >= SYN_MIN, lambda c: abs(c[3] - med))
    take("star wildcard fed by pinned stars",
         lambda c: types(c) & {"MULTISTAR", "STARS_MULTIPLE_LEVELS"} and
         sum(1 for t in c[2] if t.type in D.STAR_TYPES) >= 2, lambda c: -c[4])
    take("two counters sharing a course",
         lambda c: sum(1 for t in c[2] if t.type in D.SUPPLY) >= 2 and
         not types(c) & WILD, lambda c: -c[4])
    take("middle row/col", lambda c: D.LINE_NAMES[c[1]] in ("row2", "col2"),
         lambda c: abs(c[3] - med))
    take("diagonal", lambda c: D.LINE_NAMES[c[1]].startswith("diag"), lambda c: abs(c[3] - med))
    rows = []
    for label, (seed, li, tiles, e, syn) in picks[:count]:
        c, log = m.exact_set_cost(tiles, explain=True)
        print("### %s -- seed %d %s: exact %s, synergy %s" % (
            label, seed, D.LINE_NAMES[li], M.fmt_time(e), M.fmt_time(syn)))
        for t in tiles:
            print("    %-40s standalone %s" % (t.key(), M.fmt_time(m.standalone_cost(t))))
        print("    plan:")
        for what, w in log:
            print("      %-66s %s" % (what, M.fmt_time(w)))
        rows.append({"label": label, "seed": seed, "line": D.LINE_NAMES[li],
                     "tiles": [(t.key(), m.standalone_cost(t)) for t in tiles],
                     "exact": e, "synergy": syn, "plan": log})
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--boards", default=DEFAULT_BOARDS)
    ap.add_argument("--limit", type=int, default=100000)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--examples", action="store_true")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if a.examples:
        rows = examples(a.boards)
        with open(os.path.join(a.out, "examples.json"), "w") as fh:
            json.dump(rows, fh, indent=1)
        return
    rows = run(a.boards, a.limit, a.jobs)
    with open(os.path.join(a.out, "master_boards.jsonl"), "w") as fh:
        for r in rows:
            fh.write(json.dumps(r) + "\n")
    summarize(rows, "master")


if __name__ == "__main__":
    main()
