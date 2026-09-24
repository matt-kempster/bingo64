#!/usr/bin/env python3
"""Fit the cost-model knobs from pairwise "which takes longer?" votes.

    python3 fit.py votes.json        # writes params.fitted.json + a diff
    python3 fit.py --selftest        # synthetic votes from known params

Votes: JSON array of {"a": key, "b": key, "winner": "a"|"b"|"tie",
"ctx": key-or-null, "voter": str, "t": iso}. winner = the tile that takes
LONGER; with ctx, the tile that ADDS more time given you are already doing
the ctx star in its course.

Step 1, Bradley-Terry: one strength theta per item (catalog key, or
"ctx|key" for in-context items), P(a beats b) = sigmoid(theta_a - theta_b),
ties count half a win each way, small ridge so unconnected items stay at 0.

Step 2, knobs: each knob is a log-multiplier x on a prior (V[course],
u[type], star scale, reentry, challenge multipliers, coin/1-up/f rates...).
Least squares on  LAMBDA * log(cost_i(x)) + offset_group(i) - theta_i
with a ridge on x (unvoted knobs stay at their prior). The standalone items
share one offset, each context gets its own (contexts are only compared
within themselves). Pairwise votes fix ratios, never the absolute scale, so
the fit keeps the geometric mean of the voted costs at the prior's.
LAMBDA = logit(0.9)/ln 2: "a tile twice as long wins 90% of votes" (an
assumption about voter noise -- change with --lam).
"""
import argparse
import copy
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import data as D  # noqa: E402
import model as M  # noqa: E402

LAM = math.log(9.0) / math.log(2.0)
RIDGE = 1.0 / (0.5 ** 2)     # prior sd of a log-multiplier: 0.5 (x1.65)


# --------------------------------------------------------------------------
# Bradley-Terry

def bradley_terry(votes, ridge=0.05, iters=300):
    items = sorted({v["_ia"] for v in votes} | {v["_ib"] for v in votes})
    idx = {k: i for i, k in enumerate(items)}
    th = [0.0] * len(items)
    pairs = []
    for v in votes:
        a, b = idx[v["_ia"]], idx[v["_ib"]]
        w = {"a": 1.0, "b": 0.0, "tie": 0.5}[v["winner"]]
        pairs.append((a, b, w))
    for _ in range(iters):
        g = [-ridge * t for t in th]
        h = [ridge] * len(th)
        for a, b, w in pairs:
            p = 1.0 / (1.0 + math.exp(th[b] - th[a]))
            g[a] += w - p
            g[b] -= w - p
            q = p * (1 - p)
            h[a] += q
            h[b] += q
        step = max(abs(gi / hi) for gi, hi in zip(g, h))
        th = [t + gi / hi for t, gi, hi in zip(th, g, h)]
        if step < 1e-7:
            break
    # Fisher information per item (inverse variance of theta): weights the
    # knob fit so thinly-voted items don't dominate it.
    info = [0.0] * len(th)
    for a, b, w in pairs:
        p = 1.0 / (1.0 + math.exp(th[b] - th[a]))
        info[a] += p * (1 - p)
        info[b] += p * (1 - p)
    return dict(zip(items, th)), dict(zip(items, info))


# --------------------------------------------------------------------------
# Knobs

def knob_list(p):
    ks = []
    for c in D.ALL_PLACES:
        if c != D.CASTLE:
            ks.append(("V", c))
    for t in sorted(p["u"]):
        if not t.startswith("_"):
            ks.append(("u", t))
    ks += [("star", "scale_pct"), ("reentry",), ("f", "pct"), ("coins", "u"),
           ("coins", "free_per_visit"), ("oneups", "u"), ("blj", "per_course"),
           ("dwk", "per_course"), ("splatoon", "per_tile"), ("purple", "per_star"),
           ("roof", "work"), ("modifiers", "STAR_TIMED", "base_pct"),
           ("modifiers", "STAR_CLICK_GAME", "base_pct"),
           ("modifiers", "STAR_DAREDEVIL", "base_pct")]
    for t, v in sorted(p["modifiers"].items()):
        if not t.startswith("_") and not isinstance(v, dict):
            ks.append(("modifiers", t))
    for k in sorted(p["star"]["special"]):
        ks.append(("star", "special", k))
    return ks


def _get(p, path):
    for k in path[:-1]:
        p = p[k]
    return p[path[-1]]


def _set(p, path, val):
    for k in path[:-1]:
        p = p[k]
    p[path[-1]] = val


# Every cost is a sum of these time-valued numbers times dimensionless
# factors, so multiplying all of them by s multiplies every cost by s.
def _time_paths(p):
    out = [("V", c) for c in p["V"] if not c.startswith("_")]
    out += [("u", t) for t in p["u"] if not t.startswith("_")]
    out += [("star", "special", k) for k in p["star"]["special"]]
    out += [("reentry",), ("star", "scale_pct"), ("star", "min"), ("star", "hundred_extra"),
            ("coins", "u"), ("oneups", "u"), ("oneups", "castle_u"), ("blj", "per_course"),
            ("dwk", "per_course"), ("dwk", "per_kick"), ("splatoon", "per_tile"),
            ("purple", "per_star"), ("roof", "work")]
    out += [("star", "overrides", k) for k in p["star"]["overrides"] if not k.startswith("_")]
    return out


def rescale(p, s):
    """Gauge fix: multiply every time-valued knob by s."""
    p = copy.deepcopy(p)
    for path in _time_paths(p):
        v = _get(p, path)
        if v:
            _set(p, path, max(1, int(round(v * s))))
    return p


def apply(prior, knobs, x):
    p = copy.deepcopy(prior)
    for path, xi in zip(knobs, x):
        if xi:
            _set(p, path, max(1, int(round(_get(prior, path) * math.exp(xi)))))
    return p


# --------------------------------------------------------------------------
# Predictions

def predict(params, items):
    """item -> log(cost). 'ctx|key' = extra time of key given the ctx star."""
    m = M.Model(params)
    out = {}
    for it in items:
        if "|" in it:
            ctx, k = it.split("|", 1)
            ct = M.parse_key(ctx)
            extra = m.exact_set_cost([ct, M.parse_key(k)]) - m.standalone_cost(ct)
            out[it] = math.log(max(extra, 50))
        else:
            out[it] = math.log(m.standalone_cost(M.parse_key(it)))
    return out


def group_of(it):
    return it.split("|", 1)[0] if "|" in it else ""


def solve_linear(A, b):
    """Gaussian elimination with partial pivoting (small dense systems)."""
    n = len(b)
    A = [row[:] + [bb] for row, bb in zip(A, b)]
    for i in range(n):
        piv = max(range(i, n), key=lambda r: abs(A[r][i]))
        A[i], A[piv] = A[piv], A[i]
        if abs(A[i][i]) < 1e-12:
            continue
        for r in range(i + 1, n):
            f = A[r][i] / A[i][i]
            if f:
                for c in range(i, n + 1):
                    A[r][c] -= f * A[i][c]
    x = [0.0] * n
    for i in range(n - 1, -1, -1):
        s = A[i][n] - sum(A[i][c] * x[c] for c in range(i + 1, n))
        x[i] = s / A[i][i] if abs(A[i][i]) > 1e-12 else 0.0
    return x


def fit_knobs(prior, theta, lam=LAM, iters=8, log=print, weight=None):
    items = sorted(theta)
    wt = {i: (weight[i] if weight else 1.0) for i in items}
    sw = {i: math.sqrt(wt[i]) for i in items}
    knobs_all = knob_list(prior)
    base = predict(prior, items)
    # keep only knobs that move some voted item
    knobs = []
    eps = 0.2
    for path in knobs_all:
        pr = predict(apply(prior, [path], [eps]), items)
        if any(abs(pr[i] - base[i]) > 1e-9 for i in items):
            knobs.append(path)
    groups = sorted({group_of(i) for i in items})
    gi = {g: j for j, g in enumerate(groups)}
    nk, ng = len(knobs), len(groups)
    x = [0.0] * nk
    off = [0.0] * ng
    log("fitting %d knobs (of %d) on %d items, %d offset groups" % (nk, len(knobs_all),
                                                                  len(items), ng))

    def residuals(pred):
        return [sw[i] * (lam * pred[i] + off[gi[group_of(i)]] - theta[i]) for i in items]

    pred = base
    for it in range(iters):
        # offsets: closed form given x
        for g in groups:
            mem = [i for i in items if group_of(i) == g]
            off[gi[g]] = sum(wt[i] * (theta[i] - lam * pred[i]) for i in mem) / max(
                1e-9, sum(wt[i] for i in mem))
        r = residuals(pred)
        sse = sum(v * v for v in r) + RIDGE * sum(v * v for v in x)
        # finite-difference Jacobian of lam*log cost wrt x
        J = []
        h = 0.05
        for j in range(nk):
            xp = x[:]
            xp[j] += h
            pp = predict(apply(prior, knobs, xp), items)
            J.append([sw[i] * lam * (pp[i] - pred[i]) / h for i in items])
        # augmented: unknowns x (nk) + offsets (ng)
        n = nk + ng
        A = [[0.0] * n for _ in range(n)]
        b = [0.0] * n
        cols = []
        for j in range(nk):
            cols.append(J[j])
        for g in groups:
            cols.append([sw[i] if group_of(i) == g else 0.0 for i in items])
        for a in range(n):
            ca = cols[a]
            for c in range(a, n):
                cc = cols[c]
                A[a][c] = A[c][a] = sum(u * v for u, v in zip(ca, cc))
            b[a] = -sum(u * v for u, v in zip(ca, r))
        mu = 1.0
        for j in range(nk):
            A[j][j] += RIDGE + mu
            b[j] -= RIDGE * x[j]
        for g in range(ng):
            A[nk + g][nk + g] += 1e-6
        dx = solve_linear(A, b)
        xn = [max(-2.0, min(2.0, xi + d)) for xi, d in zip(x, dx[:nk])]
        pn = predict(apply(prior, knobs, xn), items)
        offn = [o + d for o, d in zip(off, dx[nk:])]
        rn = [sw[i] * (lam * pn[i] + offn[gi[group_of(i)]] - theta[i]) for i in items]
        ssen = sum(v * v for v in rn) + RIDGE * sum(v * v for v in xn)
        log("  iter %d: objective %.3f -> %.3f" % (it, sse, ssen))
        if ssen < sse:
            x, pred, off = xn, pn, offn
            if sse - ssen < 1e-3:
                break
        else:
            break
    # pairwise votes cannot fix the absolute scale: shift all multipliers
    # uniformly? No -- a uniform shift is not a knob. Instead report the
    # geometric-mean ratio so a reader sees any drift.
    gm = sum(pred[i] - base[i] for i in items if "|" not in i) / max(
        1, sum(1 for i in items if "|" not in i))
    log("geometric-mean drift of voted standalone tiles: x%.2f (not identifiable from "
        "votes; undone by rescaling all time-valued knobs)" % math.exp(gm))
    return knobs, x, math.exp(-gm)


def fitted_params(prior, knobs, x, gauge):
    return rescale(apply(prior, knobs, x), gauge)


def diff_text(prior, fitted, knobs, _unused=None):
    rows = []
    seen = set()
    for path in knobs:
        if path in seen:
            continue
        seen.add(path)
        a, b = _get(prior, path), _get(fitted, path)
        if a != b:
            rows.append((abs(math.log(b / float(a))), "%-36s %8s %8s  x%.2f" % (
                ".".join(path), a, b, b / float(a))))
    rows.sort(reverse=True)
    return "\n".join(["knob                                    prior   fitted"] +
                     [r for _, r in rows])


def prepare(votes):
    out = []
    for v in votes:
        if v.get("winner") not in ("a", "b", "tie"):
            continue
        v = dict(v)
        if v.get("ctx"):
            v["_ia"] = v["ctx"] + "|" + v["a"]
            v["_ib"] = v["ctx"] + "|" + v["b"]
        else:
            v["_ia"], v["_ib"] = v["a"], v["b"]
        out.append(v)
    return out


def fit_file(path, lam, outdir):
    try:
        with open(path) as fh:
            votes = json.load(fh)
    except (IOError, ValueError):
        votes = []
    votes = prepare(votes)
    if not votes:
        print("no votes")
        return 0
    prior = M.load_params()
    theta, info = bradley_terry(votes)
    print("%d votes, %d items" % (len(votes), len(theta)))
    knobs, x, gauge = fit_knobs(prior, theta, lam, weight=info)
    fitted = fitted_params(prior, knobs, x, gauge)
    fitted["_about"] = "FITTED from %d votes by fit.py (lambda %.2f). Priors: params.json." % (
        len(votes), lam)
    with open(os.path.join(HERE, "params.fitted.json"), "w") as fh:
        json.dump(fitted, fh, indent=1)
    d = diff_text(prior, fitted, _time_paths(prior) + knobs, [1.0] * 999)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "fit_diff.txt"), "w") as fh:
        fh.write(d + "\n")
    print(d)
    return 0


# --------------------------------------------------------------------------
# Self-test

def selftest(n_votes=4000, n_ctx=1200, seed=7):
    rng = random.Random(seed)
    prior = M.load_params()
    truth = copy.deepcopy(prior)
    changes = [(("V", "THI"), 1.6), (("u", "KILL_GOOMBAS"), 0.5), (("star", "scale_pct"), 1.25),
               (("modifiers", "STAR_A_BUTTON_CHALLENGE"), 0.7), (("coins", "u"), 1.5),
               (("V", "BOB"), 0.7), (("u", "SIGNPOST"), 1.5)]
    for path, f in changes:
        _set(truth, path, int(round(_get(prior, path) * f)))
    cat = M.load_catalog()
    keys = [t["key"] for t in cat["tiles"]]
    ctxs = [c["key"] for c in cat["contexts"]]
    # each context gets a fixed shortlist of 12 tiles (as a voting page would)
    short = {c: rng.sample(keys, 12) for c in ctxs}
    items = keys + ["%s|%s" % (c, k) for c in ctxs for k in short[c]]
    tp = predict(truth, items)
    votes = []

    def vote(ia, ib, a, b, ctx):
        z = LAM * (tp[ia] - tp[ib])
        p = 1.0 / (1.0 + math.exp(-z))
        r = rng.random()
        w = "tie" if abs(z) < 0.3 and rng.random() < 0.3 else ("a" if r < p else "b")
        votes.append({"a": a, "b": b, "winner": w, "ctx": ctx, "voter": "synthetic",
                      "t": "2026-09-23T00:00:00Z"})
    for _ in range(n_votes):
        a, b = rng.sample(keys, 2)
        vote(a, b, a, b, None)
    for _ in range(n_ctx):
        c = rng.choice(ctxs)
        a, b = rng.sample(short[c], 2)
        vote("%s|%s" % (c, a), "%s|%s" % (c, b), a, b, c)
    votes = prepare(votes)
    theta, info = bradley_terry(votes)
    knobs, x, gauge = fit_knobs(prior, theta, LAM, log=lambda s: print("  " + s), weight=info)
    fitted = fitted_params(prior, knobs, x, gauge)
    print("recovery of the perturbed knobs (truth / prior -> fitted / prior):")
    hits = 0
    for path, f in changes:
        got = _get(fitted, path) / float(_get(prior, path))
        # right direction, and at least 40% of the way there
        good = (math.log(got) / math.log(f)) >= 0.4
        hits += good
        print("  %-40s truth x%.2f  fitted x%.2f  %s" % (".".join(path), f, got,
                                                         "ok" if good else "MISS"))
    sa = [k for k in keys]
    pp, pf = predict(prior, sa), predict(fitted, sa)
    # compare after removing the unidentifiable global scale
    def rmse(pred):
        d = [pred[k] - tp[k] for k in sa]
        mu = sum(d) / len(d)
        return math.sqrt(sum((v - mu) ** 2 for v in d) / len(d))
    r0, r1 = rmse(pp), rmse(pf)
    print("log-cost RMSE vs truth over the catalog (scale removed): prior %.3f -> fitted %.3f" % (
        r0, r1))
    ok = r1 < 0.8 * r0 and hits >= len(changes) - 2
    big = []
    for p in knobs:
        if p in [c[0] for c in changes]:
            continue
        r = _get(fitted, p) / float(_get(prior, p))
        if abs(math.log(r)) > 0.25:
            big.append((".".join(p), round(r, 2)))
    print("unperturbed knobs that moved > x1.28: %s" % (big or "none"))
    print("SELFTEST %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("votes", nargs="?", default=os.path.join(HERE, "votes.json"))
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--lam", type=float, default=LAM)
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    return fit_file(a.votes, a.lam, a.out)


if __name__ == "__main__":
    sys.exit(main())
