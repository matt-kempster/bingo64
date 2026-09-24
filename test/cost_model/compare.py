#!/usr/bin/env python3
"""Master vs gen_v2 on the stage-2 metrics, plus generator op counts.

    python3 compare.py [--seeds 2000] [--jobs 8] [--set line_tol=20 ...]

Line mode: the measure.py metrics (exact line costs, spread, synergy, strong
pairs, near-ties, cheapest structural line, type repetition) for master's
dumps vs v2 boards for the same number of seeds.
Lockout: snowball (most of the 13 cheapest tiles in one home course),
contention, standalone spread. Blackout: longest tile, total, whole-board
fast cost. Master has one generator for all modes, so its dumps (1-line
dedupe) stand in for its lockout/blackout boards.
Writes out/compare.txt and out/v2_*.jsonl.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import data as D  # noqa: E402
import gen_v2 as G  # noqa: E402
import measure as ME  # noqa: E402
import model as M  # noqa: E402

_M = None
_OVER = {}


def _model():
    global _M
    if _M is None:
        _M = M.Model()
    return _M


def _v2(args):
    mode, seed = args
    m = _model()
    g = G.Gen(model=m, mode=mode, **_OVER.get(mode, {}))
    t0 = time.time()
    b = g.generate(seed)
    dt = time.time() - t0
    ops = dict(g.ops)
    ops["dial_unique"] = len(g._sa)
    ops["pair_unique"] = len(g._pair)
    r = {"seed": seed, "mode": mode, "secs": dt, "ops": ops, "score": b["score"],
         "attempts": b["attempts"]}
    if mode == "line":
        r.update(ME.board_metrics(m, b["cells"]))
    else:
        r.update(set_metrics(m, g, b["cells"]))
    return r


def set_metrics(m, g, cells):
    sa = [m.standalone_cost(t) for t in cells]
    s = sorted(sa)
    return {"standalone": sa, "snowball": G.snowball(g, cells, sa),
            "contention": G.contention(g, cells), "longest": s[-1], "total": sum(sa),
            "spread": s[int(0.9 * 24)] / float(s[int(0.1 * 24)]),
            "board_fast": m.fast_line_cost(cells),
            "types": max(sum(1 for t in cells if t.type == x.type) for x in cells)}


def _master_set(path):
    m = _model()
    cells = M.load_board(path)
    if not cells:
        return None
    g = G.Gen(model=m, mode="lockout")
    r = set_metrics(m, g, cells)
    r["seed"] = int(os.path.basename(path)[:-4])
    return r


def q(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p * len(xs)))]


def set_summary(rows, label, p):
    n = len(rows)
    p("-- %s (%d boards)" % (label, n))
    sb = [r["snowball"] for r in rows]
    p("  snowball (max of 13 cheapest tiles in one home course): mean %.2f, p90 %d, "
      "boards > 3: %.1f%%" % (sum(sb) / float(n), q(sb, .9),
                              100.0 * sum(1 for x in sb if x > 3) / n))
    ct = [r["contention"] for r in rows]
    p("  contention (%% of tiles sharing a home course): median %d%%, p10 %d%%, p90 %d%%" % (
        q(ct, .5), q(ct, .1), q(ct, .9)))
    sp = [r["spread"] for r in rows]
    p("  standalone p90/p10 within board: median %.2f" % q(sp, .5))
    lg = [r["longest"] for r in rows]
    tt = [r["total"] for r in rows]
    bf = [r["board_fast"] for r in rows]
    p("  longest tile: median %s, p90 %s, max %s" % (M.fmt_time(q(lg, .5)),
                                                    M.fmt_time(q(lg, .9)), M.fmt_time(max(lg))))
    p("  total standalone: median %s (p10 %s, p90 %s); whole-board fast cost median %s "
      "(synergy %d%%)" % (M.fmt_time(q(tt, .5)), M.fmt_time(q(tt, .1)), M.fmt_time(q(tt, .9)),
                         M.fmt_time(q(bf, .5)),
                         int(100 - 100.0 * sum(bf) / sum(tt))))
    p("  boards with >= 4 copies of one type: %.1f%%" % (
        100.0 * sum(1 for r in rows if r["types"] >= 4) / n))


def ops_summary(rows, label, p):
    n = len(rows)
    keys = ["cand", "dial", "dial_unique", "pair", "pair_unique", "line", "restarts"]
    p("-- %s generator work per board (%d boards): python %.2fs mean, %.2fs max" % (
        label, n, sum(r["secs"] for r in rows) / n, max(r["secs"] for r in rows)))
    for k in keys:
        xs = [r["ops"][k] for r in rows]
        p("  %-12s mean %8.0f  p90 %8d  max %8d" % (k, sum(xs) / float(n), q(xs, .9), max(xs)))
    ok = sum(1 for r in rows if r["score"] == 0)
    p("  boards meeting every constraint: %.1f%% (rest: best of the restarts)" % (100.0 * ok / n))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seeds", type=int, default=2000)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--boards", default=ME.DEFAULT_BOARDS)
    ap.add_argument("--modes", default="line,lockout,blackout")
    ap.add_argument("--set", action="append", default=[], help="mode.key=json, e.g. line.line_tol=20")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--reuse", action="store_true",
                    help="re-summarize existing out/v2_*.jsonl instead of regenerating")
    a = ap.parse_args()
    for s in a.set:
        k, v = s.split("=", 1)
        mode, key = k.split(".", 1)
        _OVER.setdefault(mode, {})[key] = json.loads(v)
    os.makedirs(a.out, exist_ok=True)
    from multiprocessing import Pool
    lines_out = []
    p = lambda s: (print(s), lines_out.append(s))  # noqa: E731
    modes = a.modes.split(",")
    with Pool(a.jobs, initializer=_init, initargs=(_OVER,)) as pool:
        mpath = os.path.join(a.out, "master_boards.jsonl")
        if "line" in modes:
            if os.path.exists(mpath):
                master = [json.loads(l) for l in open(mpath)]
            else:
                master = ME.run(a.boards, 100000, a.jobs)
            master = [r for r in master if r["seed"] <= a.seeds]
            vpath = os.path.join(a.out, "v2_line.jsonl")
            if a.reuse and os.path.exists(vpath):
                v2 = [json.loads(l) for l in open(vpath)]
            else:
                v2 = pool.map(_v2, [("line", s) for s in range(1, a.seeds + 1)], chunksize=4)
            with open(os.path.join(a.out, "v2_line.jsonl"), "w") as fh:
                for r in v2:
                    fh.write(json.dumps(r) + "\n")
            import io
            for rows, label in ((master, "master (1-line dumps)"), (v2, "v2 line mode")):
                buf = io.StringIO()
                ME.summarize(rows, label, out=buf)
                for l in buf.getvalue().splitlines():
                    p(l)
            ops_summary(v2, "v2 line", p)
        if "lockout" in modes or "blackout" in modes:
            import glob
            files = sorted(glob.glob(os.path.join(a.boards, "*.txt")),
                           key=lambda x: int(os.path.basename(x)[:-4]))[:a.seeds]
            ms = [r for r in pool.map(_master_set, files, chunksize=8) if r]
            set_summary(ms, "master boards as a set", p)
        for mode in ("lockout", "blackout"):
            if mode not in modes:
                continue
            vpath = os.path.join(a.out, "v2_%s.jsonl" % mode)
            if a.reuse and os.path.exists(vpath):
                v2 = [json.loads(l) for l in open(vpath)]
            else:
                v2 = pool.map(_v2, [(mode, s) for s in range(1, a.seeds + 1)], chunksize=8)
            with open(os.path.join(a.out, "v2_%s.jsonl" % mode), "w") as fh:
                for r in v2:
                    fh.write(json.dumps(r) + "\n")
            set_summary(v2, "v2 %s" % mode, p)
            ops_summary(v2, "v2 %s" % mode, p)
    with open(os.path.join(a.out, "compare.txt"), "w") as fh:
        fh.write("\n".join(lines_out) + "\n")


def _init(over):
    _OVER.update(over)


if __name__ == "__main__":
    main()
