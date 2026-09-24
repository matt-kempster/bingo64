#!/usr/bin/env python3
"""Stage 4 prototype board generators driven by the cost model.

    python3 gen_v2.py --seed 5 --mode line      # print a board
    python3 gen_v2.py --seed 5 --mode lockout
    python3 gen_v2.py --seed 5 --mode blackout

Deterministic from the seed (random.Random(seed); a C port would use the
game's MT19937). All costs are integer deciseconds from model.fast_line_cost,
i.e. what a C port could compute; reports re-cost boards with the exact solver.

Line mode (1/2/3 lines)
  1. Seeded 5x5 magic square (SRL v5 construction) -> difficulty 1..25 per cell
     -> target standalone cost lo + (d-1)(hi-lo)/24, so every line has the same
     target sum but a different mix.
  2. Candidate pool (~200 skeletons) drawn with master's type weights; 2-3 hub
     courses per seed bias course choices (pinned tiles land in a hub with
     probability hub_p; counters with hub supply get weight x hub_boost).
  3. "N as a dial": a skeleton carries every sane N (kill count, coins,
     slack...), and the generator picks the N whose standalone cost is
     closest to the cell's target.
  4. Fill order: the 3 hardest cells, the center, the diagonals, the rest.
     First candidate within +-window of the target wins (window widens
     10% -> 20% -> 35%), subject to: master's are_duplicates (board-wide),
     per-type cap, category caps, and per-line synergy: at most syn_max strong
     pairs per line, and a line's last cell must bring it to >= syn_min.
  5. Line check: effective (post-synergy) line costs within line_tol of each
     other, strong pairs in [syn_min, syn_max]. Else restart the board
     (<= restarts); keep the best attempt.

Lockout: no lines. A cost ladder of 25 targets, same pool/dial/caps; snowball
cap (no course is home to more than k of the 13 cheapest tiles), contention
dial (share of tiles that share a home course with another tile), random
placement.

Blackout: cap the longest tile, total standalone within a band around the
ladder sum, hubs strongly encouraged (co-op teams split by course).
"""
import argparse
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import data as D  # noqa: E402
import model as M  # noqa: E402

DEFAULTS = {
    "line": {"lo": 600, "hi": 3600, "windows": [10, 20, 35], "min_window": 150,
             "line_tol": 25, "syn_min": 1, "syn_max": 2, "strong": 450,
             "restarts": 50, "pool": 200, "hubs": 2, "hub_p": 30, "hub_boost": 2,
             "type_cap": 2, "type_cap_overrides": {"STAR": 3},
             "cat_caps": {"star": 9, "course": 6, "counter": 8, "wild": 5}},
    "lockout": {"lo": 600, "hi": 3000, "windows": [10, 20, 35], "min_window": 150,
                "snowball_k": 3, "contention": [30, 60], "restarts": 50, "pool": 200,
                "hubs": 3, "hub_p": 30, "hub_boost": 2, "type_cap": 2,
                "type_cap_overrides": {"STAR": 3},
                "cat_caps": {"star": 9, "course": 6, "counter": 8, "wild": 4}},
    "blackout": {"lo": 600, "hi": 3000, "windows": [10, 20, 35], "min_window": 150,
                 "longest_cap": 3600, "total_band": 8, "restarts": 50, "pool": 200,
                 "hubs": 3, "hub_p": 50, "hub_boost": 3, "type_cap": 2,
                 "type_cap_overrides": {"STAR": 3},
                 "cat_caps": {"star": 9, "course": 6, "counter": 8, "wild": 5}},
}

CATEGORY = {}
for _t in D.STAR_TYPES + ["RANDOM_RED_COINS", "BOWSER", "RACING_STARS", "SECRETS_STARS"]:
    CATEGORY[_t] = "star"
for _t in ["COIN", "1UPS_IN_LEVEL", "STARS_IN_LEVEL", "SPLATOON", "RANDOM_STARS"]:
    CATEGORY[_t] = "course"
for _t in ["MULTICOIN", "MULTISTAR", "STARS_MULTIPLE_LEVELS", "LIVES", "UNIQUE_DEATHS", "BLJ",
           "DANGEROUS_WALL_KICKS", "RED_COIN_STARS", "CANNON_STARS", "LOSE_MARIO_HAT",
           "ROOF_WITHOUT_CANNON"]:
    CATEGORY[_t] = "wild"
for _t in D.SUPPLY:
    CATEGORY.setdefault(_t, "counter")


# --------------------------------------------------------------------------
# SRL v5 magic square (bingosync generator_bases/srl_generator_v5.js)

def srl_difficulty(seed_num):
    """25 difficulties (1..25), index = cell 0..24, rows/cols/diags sum 65."""
    def table(num3):
        rem8 = num3 % 8
        rem4 = rem8 // 2
        rem2 = rem8 % 2
        rem5 = num3 % 5
        rem3 = num3 % 3
        t = [0]
        t.insert(rem2, 1)
        t.insert(rem3, 2)
        t.insert(rem4, 3)
        t.insert(rem5, 4)
        return t
    num3 = seed_num % 1000
    t5 = table(num3)
    remt = num3 // 120
    num3 = (seed_num // 1000) % 1000
    t1 = table(num3)
    remt = (remt * 8 + num3 // 120) % 5
    out = []
    for i in range(25):
        x = (i + remt) % 5
        y = i // 5
        out.append(5 * t5[(x + 3 * y) % 5] + t1[(3 * x + y) % 5] + 1)
    return out


# --------------------------------------------------------------------------
# Candidate skeletons with an N dial

class Skeleton(object):
    __slots__ = ("type", "options", "home")

    def __init__(self, type, options):
        self.type = type
        self.options = options      # list of Tiles, the dial positions

    def key(self):
        return self.options[0].key()


def type_weights():
    """Expected cells per board of each type under master's tables."""
    cells = {"EASY": 4, "MEDIUM": 16, "HARD": 4, "CENTER": 1}
    w = {}
    for cls, tbl in D.WEIGHTS.items():
        tot = float(sum(x[1] for x in tbl))
        for t, wt, _ in tbl:
            w[t] = w.get(t, 0.0) + cells[cls] * wt / tot
    return w


def _star_any(rng):
    i = rng.randrange(115)
    if i < 105:
        return D.MAIN[i % 15], i % 7
    j = i - 105
    return [("PSS", 0), ("PSS", 1), ("SA", 0), ("TotWC", 0), ("CotMC", 0), ("VCutM", 0),
            ("WMotR", 0), ("BitDW", 0), ("BitFS", 0), ("BitS", 0)][j]


def _rng_range(lo, hi, step=1):
    return list(range(lo, hi + 1, step))


COIN_SPECIAL = {"BitDW": (32, 80), "BitFS": (32, 80), "BitS": (30, 76), "PSS": (32, 80),
                "CotMC": (37, 47), "TotWC": (37, 53), "VCutM": (27, 27), "WMotR": (22, 56),
                "SA": (56, 56)}


def make_skeleton(t, rng, hubs, hub_p):
    """One candidate skeleton of type t (mirrors the master init functions)."""
    T = M.Tile
    hub = rng.choice(hubs) if hubs and rng.randrange(100) < hub_p else None
    hub_main = hub if hub in D.MAIN else None

    def star_in(pred, any_fn):
        if hub:
            opts = [i for i in D.course_stars(hub) if pred(hub, i)]
            if opts:
                return hub, rng.choice(opts)
        while True:
            c, i = any_fn()
            if pred(c, i):
                return c, i

    if t == "STAR" or t == "STAR_REVERSE_JOYSTICK":
        c, i = star_in(lambda c, i: True, lambda: _star_any(rng))
        return Skeleton(t, [T(t, c, i)])
    if t == "STAR_TTC_RANDOM":
        return Skeleton(t, [T(t, "TTC", rng.randrange(7))])
    if t == "STAR_A_BUTTON_CHALLENGE":
        c, s = rng.choice(D.ABC_STARS)
        return Skeleton(t, [T(t, c, s - 1)])
    if t == "STAR_B_BUTTON_CHALLENGE":
        bad = {("BOB", 0), ("TTM", 1), ("CCM", 1)}
        c, i = star_in(lambda c, i: (c, i) not in bad, lambda: _star_any(rng))
        return Skeleton(t, [T(t, c, i)])
    if t == "STAR_Z_BUTTON_CHALLENGE":
        bad = {("WF", 0), ("JRB", 6), ("DDD", 6), ("TTC", 6)}
        c, i = star_in(lambda c, i: (c, i) not in bad, lambda: _star_any(rng))
        return Skeleton(t, [T(t, c, i)])
    if t in ("STAR_TIMED", "STAR_GREEN_DEMON"):
        c, i = star_in(lambda c, i: c in D.MAIN and i < 6,
                       lambda: (rng.choice(D.MAIN), rng.randrange(6)))
        if t == "STAR_GREEN_DEMON":
            return Skeleton(t, [T(t, c, i)])
        base = D.STAR_TIMES[c][i]
        return Skeleton(t, [T(t, c, i, extra=base + s) for s in range(35, -1, -5)])
    if t == "STAR_CLICK_GAME":
        keys = sorted(D.CLICK)
        if hub_main and any(k[0] == hub_main for k in keys):
            keys = [k for k in keys if k[0] == hub_main]
        c, s = rng.choice(keys)
        lo, hi = D.CLICK[(c, s)]
        return Skeleton(t, [T(t, c, s - 1, extra=k) for k in range(hi, lo - 1, -1)])
    if t == "STAR_DAREDEVIL":
        def ok(c, i):
            return c not in ("JRB", "DDD") and not (c == "WDW" and i in (4, 5))
        if rng.randrange(4) == 0:
            c = hub_main if hub_main and hub_main not in ("DDD", "JRB") else rng.choice(
                [x for x in D.MAIN if x not in ("DDD", "JRB")])
            return Skeleton(t, [T(t, c, 6)])
        c, i = star_in(ok, lambda: _star_any(rng))
        return Skeleton(t, [T(t, c, i)])
    if t == "RANDOM_RED_COINS":
        c = hub if hub and hub in D.RED_STAR else rng.choice(sorted(D.RED_STAR))
        return Skeleton(t, [T(t, c, n=8)])
    if t == "RANDOM_STARS":
        c = hub or rng.choice(D.COURSES)
        return Skeleton(t, [T(t, c, n=3)])
    if t == "COIN":
        if hub:
            c = hub
        elif rng.randrange(100) < 76:
            c = rng.choice(D.MAIN)
        else:
            c = rng.choice(D.SPECIAL)
        lo, hi = (30, 99) if c in D.MAIN else COIN_SPECIAL[c]
        return Skeleton(t, [T(t, c, n=n) for n in sorted(set(_rng_range(lo, hi, 5) + [hi]))])
    if t == "1UPS_IN_LEVEL":
        cs = [c for c in D.COURSES if D.ONEUPS[c] >= 3]
        c = hub if hub in cs else rng.choice(cs)
        tot = D.ONEUPS[c]
        return Skeleton(t, [T(t, c, n=n) for n in range(max(2, tot * 6 // 10), tot + 1)])
    if t == "STARS_IN_LEVEL":
        c = hub_main or rng.choice(D.MAIN)
        return Skeleton(t, [T(t, c, n=n) for n in range(3, 8)])
    if t == "SPLATOON":
        c = hub_main or rng.choice(D.MAIN)
        return Skeleton(t, [T(t, c, n=M.splat_tiles(c, p)) for p in range(10, 56, 5)])
    if t == "STARS_MULTIPLE_LEVELS":
        opts = ([T(t, k=1, n=n) for n in range(3, 16)] + [T(t, k=2, n=n) for n in range(2, 9)] +
                [T(t, k=3, n=n) for n in range(3, 7)])
        return Skeleton(t, opts)
    if t == "DANGEROUS_WALL_KICKS":
        return Skeleton(t, [T(t, k=k, n=n) for k in range(5, 8) for n in range(2, 5)])
    if t == "BOWSER":
        return Skeleton(t, [T(t, D.BOWSER_COURSE[n], D.BOWSER_STAR, n=n) for n in (1, 2, 3)])
    if t in ("ROOF_WITHOUT_CANNON",):
        return Skeleton(t, [T(t, n=1)])
    if t == "RACING_STARS":
        return Skeleton(t, [T(t, n=3)])
    if t == "SECRETS_STARS":
        return Skeleton(t, [T(t, n=4)])
    lo, hi = D.N_RANGE[t]
    step = D.N_STEP.get(t, 1)
    return Skeleton(t, [T(t, n=n) for n in _rng_range(lo, hi, step)])


def make_pool(rng, size, hubs, hub_p, hub_boost, disabled=()):
    w = type_weights()
    types = sorted(t for t in w if t not in disabled)
    wt = []
    for t in types:
        x = w[t]
        if t in D.SUPPLY and any(h in D.SUPPLY[t] for h in hubs):
            x *= hub_boost
        wt.append(x)
    tot = sum(wt)
    pool = {}
    tries = 0
    while len(pool) < size and tries < size * 5:
        tries += 1
        r = rng.random() * tot
        for t, x in zip(types, wt):
            r -= x
            if r <= 0:
                break
        sk = make_skeleton(t, rng, hubs, hub_p)
        if sk.key() not in pool:
            pool[sk.key()] = (w[t], sk)
    return list(pool.values())


# --------------------------------------------------------------------------
# Constraints

COLLECTABLE = set(D.TYPES[D.TYPE_NUM["MULTICOIN"]:D.TYPE_NUM["BLUE_COIN"] + 1])
NO_DUP_SAME = COLLECTABLE | {"LIVES", "BOWSER", "DANGEROUS_WALL_KICKS", "ROOF_WITHOUT_CANNON",
                             "RACING_STARS", "SECRETS_STARS", "CANNON_STARS", "RED_COIN_STARS"}
COURSE_DUP = {"COIN", "1UPS_IN_LEVEL", "STARS_IN_LEVEL", "RANDOM_RED_COINS", "SPLATOON",
              "RANDOM_STARS"}
NO_DUP_STARS = set(D.STAR_TYPES[:6])


def are_duplicates(a, b):
    """Port of master's are_duplicates()."""
    if a.type == b.type:
        if a.type in NO_DUP_SAME:
            return True
        if a.type in COURSE_DUP and a.course == b.course:
            return True
    if a.type in D.STAR_TYPES and b.type in D.STAR_TYPES and a.course == b.course \
            and a.star == b.star:
        return True
    if (a.type in NO_DUP_STARS or b.type in NO_DUP_STARS) and \
            "STARS_IN_LEVEL" in (a.type, b.type) and a.course == b.course:
        return True
    for x, y in ((a, b), (b, a)):
        if x.type == "STARS_IN_LEVEL" and y.type == "MULTISTAR" and y.n <= x.n:
            return True
    return False


class Gen(object):
    def __init__(self, model=None, mode="line", **over):
        self.m = model or M.Model()
        self.mode = mode
        self.cfg = json.loads(json.dumps(DEFAULTS[mode]))
        self.cfg.update(over)
        self.ops = {"cand": 0, "dial": 0, "pair": 0, "line": 0, "restarts": 0}
        self._sa = {}
        self._pair = {}

    # fast-path costs (what C would compute), memoised here but counted
    def sa(self, tile):
        self.ops["dial"] += 1
        k = tile.key()
        if k not in self._sa:
            self._sa[k] = self.m.fast_line_cost([tile])
        return self._sa[k]

    def pair_syn(self, a, b):
        self.ops["pair"] += 1
        k = (a.key(), b.key()) if a.key() < b.key() else (b.key(), a.key())
        if k not in self._pair:
            self._pair[k] = self.sa(a) + self.sa(b) - self.m.fast_line_cost([a, b])
        return self._pair[k]

    def line_cost(self, tiles):
        self.ops["line"] += 1
        return self.m.fast_line_cost(tiles)

    def dial(self, sk, target):
        best, bc = None, None
        for t in sk.options:
            c = self.sa(t)
            if best is None or abs(c - target) < abs(bc - target):
                best, bc = t, c
        return best, bc

    def _weighted_order(self, rng, pool):
        keyed = [(-math.log(1.0 - rng.random()) / w, i) for i, (w, _) in enumerate(pool)]
        keyed.sort()
        return [pool[i][1] for _, i in keyed]

    def _caps_ok(self, tile, placed):
        cfg = self.cfg
        same = sum(1 for p in placed if p.type == tile.type)
        if same >= cfg["type_cap_overrides"].get(tile.type, cfg["type_cap"]):
            return False
        cat = CATEGORY[tile.type]
        if sum(1 for p in placed if CATEGORY[p.type] == cat) >= cfg["cat_caps"][cat]:
            return False
        for p in placed:
            if are_duplicates(p, tile):
                return False
        return True

    # ---- line mode --------------------------------------------------
    def gen_line(self, seed):
        rng = random.Random(seed)
        cfg = self.cfg
        diff = srl_difficulty(rng.randrange(1000000))
        lo, hi = cfg["lo"], cfg["hi"]
        targets = [lo + (d - 1) * (hi - lo) // 24 for d in diff]
        hubs = rng.sample(D.MAIN, cfg["hubs"]) if cfg["hubs"] else []
        pool = make_pool(rng, cfg["pool"], hubs, cfg["hub_p"], cfg["hub_boost"])
        by_t = sorted(range(25), key=lambda i: -targets[i])
        diag = [i for i in range(25) if i % 6 == 0 or (i % 4 == 0 and 0 < i < 24)]
        order = by_t[:3] + [12] + [i for i in diag if i not in by_t[:3] and i != 12]
        order += [i for i in by_t if i not in order]
        best = None
        for attempt in range(cfg["restarts"]):
            cells = self._fill_line(rng, pool, targets, order)
            score, info = self._line_score(cells, targets)
            if best is None or score < best[0]:
                best = (score, cells, info, attempt)
            if score == 0:
                break
            self.ops["restarts"] += 1
        score, cells, info, attempt = best
        return {"cells": cells, "targets": targets, "difficulty": diff, "hubs": hubs,
                "score": score, "attempts": attempt + 1, "info": info}

    def _fill_line(self, rng, pool, targets, order):
        cfg = self.cfg
        cells = [None] * 25
        placed = []
        lines_of = [[ln for ln in D.LINES if i in ln] for i in range(25)]
        for idx in order:
            T = targets[idx]
            cand = self._weighted_order(rng, pool)
            chosen = None
            nwin = len(cfg["windows"])
            for wi, wpct in enumerate(cfg["windows"]):
                win = max(cfg["min_window"], T * wpct // 100)
                last = wi == nwin - 1
                for sk in cand:
                    self.ops["cand"] += 1
                    tile, c = self.dial(sk, T)
                    if abs(c - T) > win or not self._caps_ok(tile, placed):
                        continue
                    if not self._line_syn_ok(tile, idx, cells, lines_of[idx], strict=not last):
                        continue
                    chosen = tile
                    break
                if chosen:
                    break
            if chosen is None:
                # nothing fits: take the closest legal candidate at all
                bestd = None
                for sk in cand:
                    tile, c = self.dial(sk, T)
                    if self._caps_ok(tile, placed) and (bestd is None or abs(c - T) < bestd[0]):
                        bestd = (abs(c - T), tile)
                chosen = bestd[1]
            cells[idx] = chosen
            placed.append(chosen)
        return cells

    def _line_syn_ok(self, tile, idx, cells, lines, strict):
        cfg = self.cfg
        for ln in lines:
            others = [cells[i] for i in ln if cells[i] is not None and i != idx]
            strong = 0
            for a in range(len(others)):
                for b in range(a + 1, len(others)):
                    if self.pair_syn(others[a], others[b]) >= cfg["strong"]:
                        strong += 1
            new = sum(1 for o in others if self.pair_syn(tile, o) >= cfg["strong"])
            if strong + new > cfg["syn_max"]:
                return False
            if strict and len(others) == 4 and strong + new < cfg["syn_min"]:
                return False
        return True

    def _line_score(self, cells, targets):
        cfg = self.cfg
        costs, strongs = [], []
        for ln in D.LINES:
            tiles = [cells[i] for i in ln]
            costs.append(self.line_cost(tiles))
            s = 0
            for a in range(5):
                for b in range(a + 1, 5):
                    if self.pair_syn(tiles[a], tiles[b]) >= cfg["strong"]:
                        s += 1
            strongs.append(s)
        mn = min(costs)
        score = 0
        # every line within line_tol% of the cheapest (penalise the excess)
        for c in costs:
            excess = c * 100 - mn * (100 + cfg["line_tol"])
            if excess > 0:
                score += excess // mn + 1
        for s in strongs:
            if s < cfg["syn_min"]:
                score += 10 * (cfg["syn_min"] - s)
            if s > cfg["syn_max"]:
                score += 10 * (s - cfg["syn_max"])
        return score, {"fast_line_cost": costs, "strong": strongs}

    # ---- lockout / blackout -----------------------------------------
    def gen_set(self, seed):
        rng = random.Random(seed)
        cfg = self.cfg
        lo, hi = cfg["lo"], cfg["hi"]
        ladder = [lo + i * (hi - lo) // 24 for i in range(25)]
        hubs = rng.sample(D.MAIN, cfg["hubs"]) if cfg["hubs"] else []
        pool = make_pool(rng, cfg["pool"], hubs, cfg["hub_p"], cfg["hub_boost"])
        best = None
        for attempt in range(cfg["restarts"]):
            tiles = self._fill_set(rng, pool, ladder)
            score, info = self._set_score(tiles, ladder)
            if best is None or score < best[0]:
                best = (score, tiles, info, attempt)
            if score == 0:
                break
            self.ops["restarts"] += 1
        score, tiles, info, attempt = best
        cells = tiles[:]
        rng.shuffle(cells)                  # layout is free: random placement
        return {"cells": cells, "targets": ladder, "hubs": hubs, "score": score,
                "attempts": attempt + 1, "info": info}

    def home(self, tile):
        """Primary course a tile pulls players to (None = anywhere)."""
        if tile.course:
            return tile.course
        if tile.type in D.SUPPLY:
            sup = D.SUPPLY[tile.type]
            best = max((n, c) for c, n in sup.items() if c != D.CASTLE)
            return best[1]
        if tile.type == "RACING_STARS":
            return "BOB"
        if tile.type == "SECRETS_STARS":
            return "THI"
        return None

    def _fill_set(self, rng, pool, ladder):
        cfg = self.cfg
        placed = []
        order = list(range(25))
        order.sort(key=lambda i: -ladder[i])          # long tiles first
        cheap_home = {}
        for i in order:
            T = ladder[i]
            cand = self._weighted_order(rng, pool)
            chosen = None
            for wpct in cfg["windows"]:
                win = max(cfg["min_window"], T * wpct // 100)
                for sk in cand:
                    self.ops["cand"] += 1
                    tile, c = self.dial(sk, T)
                    if abs(c - T) > win or not self._caps_ok(tile, placed):
                        continue
                    if self.mode == "lockout" and i < 13:
                        h = self.home(tile)
                        if h and cheap_home.get(h, 0) >= cfg["snowball_k"]:
                            continue
                    if self.mode == "blackout" and c > cfg["longest_cap"]:
                        continue
                    chosen = tile
                    break
                if chosen:
                    break
            if chosen is None:
                bestd = None
                for sk in cand:
                    tile, c = self.dial(sk, T)
                    if self._caps_ok(tile, placed) and (bestd is None or abs(c - T) < bestd[0]):
                        bestd = (abs(c - T), tile)
                chosen = bestd[1]
            if i < 13:
                h = self.home(chosen)
                if h:
                    cheap_home[h] = cheap_home.get(h, 0) + 1
            placed.append(chosen)
        return placed

    def _set_score(self, tiles, ladder):
        cfg = self.cfg
        sa = [self.sa(t) for t in tiles]
        score = 0
        info = {"standalone": sa}
        if self.mode == "lockout":
            snow = snowball(self, tiles, sa)
            info["snowball"] = snow
            if snow > cfg["snowball_k"]:
                score += 10 * (snow - cfg["snowball_k"])
            ct = contention(self, tiles)
            info["contention"] = ct
            lo, hi = cfg["contention"]
            if ct < lo:
                score += (lo - ct + 9) // 10
            if ct > hi:
                score += (ct - hi + 9) // 10
        else:
            tot = sum(sa)
            want = sum(ladder)
            band = want * cfg["total_band"] // 100
            info["total"] = tot
            if abs(tot - want) > band:
                score += (abs(tot - want) - band) * 10 // want + 1
            if max(sa) > cfg["longest_cap"]:
                score += 10
        return score, info

    def generate(self, seed):
        if self.mode == "line":
            return self.gen_line(seed)
        return self.gen_set(seed)


def snowball(g, tiles, sa):
    """Max number of the 13 cheapest tiles that share one home course."""
    idx = sorted(range(len(tiles)), key=lambda i: sa[i])[:13]
    cnt = {}
    for i in idx:
        h = g.home(tiles[i])
        if h:
            cnt[h] = cnt.get(h, 0) + 1
    return max(cnt.values()) if cnt else 0


def contention(g, tiles):
    """% of tiles whose home course is also another tile's home course."""
    homes = [g.home(t) for t in tiles]
    n = 0
    for i, h in enumerate(homes):
        if h and any(homes[j] == h for j in range(len(homes)) if j != i):
            n += 1
    return n * 100 // len(tiles)


def render(board, m, exact=True):
    """5x5 text grid: tile key + standalone cost (+ line costs for line mode)."""
    cells = board["cells"]
    rows = []
    w = 26
    for r in range(5):
        top, bot = [], []
        for c in range(5):
            t = cells[r * 5 + c]
            top.append(t.key()[:w].ljust(w))
            bot.append(("  %s" % M.fmt_time(m.standalone_cost(t))).ljust(w))
        rows.append(" | ".join(top))
        rows.append(" | ".join(bot))
        rows.append("-" * (w * 5 + 12))
    out = "\n".join(rows)
    if "difficulty" in board:
        lc = [m.exact_set_cost([cells[i] for i in ln]) for ln in D.LINES]
        out += "\nexact line costs: " + ", ".join(
            "%s %s" % (n, M.fmt_time(c)) for n, c in zip(D.LINE_NAMES, lc))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--mode", choices=["line", "lockout", "blackout"], default="line")
    ap.add_argument("--set", action="append", default=[],
                    help="override a dial, e.g. --set line_tol=20 --set syn_min=0")
    a = ap.parse_args()
    over = {}
    for s in a.set:
        k, v = s.split("=", 1)
        over[k] = json.loads(v)
    g = Gen(mode=a.mode, **over)
    b = g.generate(a.seed)
    print("seed %d mode %s hubs %s score %s attempts %d ops %s" % (
        a.seed, a.mode, ",".join(b["hubs"]), b["score"], b["attempts"], g.ops))
    print(render(b, g.m))
    if a.mode == "lockout":
        print("snowball %s, contention %s%%" % (b["info"]["snowball"], b["info"]["contention"]))
    if a.mode == "blackout":
        print("total standalone %s, longest %s, whole-board fast cost %s" % (
            M.fmt_time(b["info"]["total"]), M.fmt_time(max(b["info"]["standalone"])),
            M.fmt_time(g.m.fast_line_cost(b["cells"]))))


if __name__ == "__main__":
    main()
