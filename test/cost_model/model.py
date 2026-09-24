#!/usr/bin/env python3
"""bingo64 cost model: tiles -> demands -> cheapest plan.

Units: integer deciseconds (ds). All arithmetic is integer so the fast path
can be ported to C for the N64.

A tile becomes *demands*:
  * pinned stars   (course, star, work incl. challenge multiplier)
  * course-local   COIN / 1UPS_IN_LEVEL / SPLATOON / RANDOM_STARS / roof
  * star wildcards STARS_IN_LEVEL, RED_COIN_STARS, STARS_MULTIPLE_LEVELS,
                   MULTISTAR (fed by every star in the plan)
  * counters       global unique counts with per-course supply
                   (kills, signs, poles, reds, deaths, hat ways...); several
                   tiles of one type take the MAX, not the sum
  * other wilds    BLJ / wall kicks (N distinct courses), MULTICOIN (fed by
                   every entry's coins), LIVES (fed by 1-ups and coins,
                   drained by the deaths UNIQUE_DEATHS forces)

cost(set) = cheapest plan found by `_solve(S0)`: visit every course in
S0 (+ mandatory courses + hub), then fill each demand group in a fixed order,
freebies first, then the cheapest paid units, opening new courses greedily
whenever a new course's average unit cost (V included) beats the next paid
unit in an already-visited course.

  exact_set_cost : min over all subsets of candidate courses (<= 2^10)
  fast_line_cost : one pass with S0 = mandatory courses only (the C port)
Since the empty subset is one of exact's candidates, exact <= fast always.
Within a subset the fill is greedy (star picks, counter order), so "exact"
means "exact over course subsets", not a true optimum over all plans.
"""
import json
import os
import re
import sys
from itertools import combinations

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import data as D  # noqa: E402

INF = 1 << 40


# --------------------------------------------------------------------------
# Tiles

class Tile(object):
    __slots__ = ("type", "course", "star", "n", "k", "extra", "cls", "raw")

    def __init__(self, type, course=None, star=None, n=None, k=None, extra=None,
                 cls=None, raw=None):
        self.type = type
        self.course = course      # course abbreviation or None
        self.star = star          # 0-based star index (BOWSER: data.BOWSER_STAR)
        self.n = n                # count (N); SML/DWK: number of courses
        self.k = k                # per-course count for SML/DWK
        self.extra = extra        # STAR_TIMED: limit in seconds; CLICK: max clicks
        self.cls = cls            # master class (0 E,1 M,2 H,3 C) if from a dump
        self.raw = raw

    def key(self):
        t = self.type
        if t in D.STAR_TYPES:
            s = "%s:%s:%d" % (t, self.course, self.star + 1)
            if t in ("STAR_TIMED", "STAR_CLICK_GAME") and self.extra is not None:
                s += ":%d" % self.extra
            return s
        if t == "SPLATOON":
            return "%s:%s:%d" % (t, self.course, splat_pct(self.course, self.n))
        if t in D.COURSE_LOCAL_TYPES:
            return "%s:%s:%d" % (t, self.course, self.n)
        if t in ("STARS_MULTIPLE_LEVELS", "DANGEROUS_WALL_KICKS"):
            return "%s:%dx%d" % (t, self.k, self.n)
        if t == "BOWSER":
            return "BOWSER:%d" % self.n
        return "%s:%d" % (t, self.n if self.n is not None else 1)

    def __repr__(self):
        return self.key()

    def __hash__(self):
        return hash(self.key())

    def __eq__(self, o):
        return isinstance(o, Tile) and self.key() == o.key()


def splat_pct(course, tiles):
    return (tiles * 100 + D.FLOORS[course] // 2) // D.FLOORS[course]


def splat_tiles(course, pct):
    t = (D.FLOORS[course] * pct // 100) // 10 * 10
    return max(t, 20)


def parse_key(key):
    """Catalog key -> Tile. Grammar in catalog.json 'keyGrammar'."""
    parts = key.split(":")
    t = parts[0]
    if t in D.STAR_TYPES:
        extra = int(parts[3]) if len(parts) > 3 else None
        return Tile(t, parts[1], int(parts[2]) - 1, extra=extra, raw=key)
    if t == "SPLATOON":
        return Tile(t, parts[1], n=splat_tiles(parts[1], int(parts[2])), raw=key)
    if t in D.COURSE_LOCAL_TYPES:
        n = int(parts[2]) if len(parts) > 2 else 3
        return Tile(t, parts[1], n=n, raw=key)
    if t in ("STARS_MULTIPLE_LEVELS", "DANGEROUS_WALL_KICKS"):
        k, n = parts[1].split("x")
        return Tile(t, k=int(k), n=int(n), raw=key)
    if t == "BOWSER":
        n = int(parts[1])
        return Tile(t, D.BOWSER_COURSE[n], D.BOWSER_STAR, n=n, raw=key)
    return Tile(t, n=int(parts[1]) if len(parts) > 1 else 1, raw=key)


_FIELD = re.compile(r'(\w+)=("[^"]*"|\S+)')


def parse_dump_line(line):
    """Host-harness dump_cell line -> (cell index, Tile)."""
    idx = int(line[:2])
    f = dict(_FIELD.findall(line))
    t = D.TYPES[int(f["type"])]
    cls = int(f["class"])
    course = D.NUM_COURSE.get(int(f["course"])) if "course" in f else None
    if t in D.STAR_TYPES:
        extra = None
        if t == "STAR_TIMED":
            extra = int(f["maxTime"]) // 30
        elif t == "STAR_CLICK_GAME":
            extra = int(f["maxClicks"])
        return idx, Tile(t, course, int(f["star"]), extra=extra, cls=cls, raw=line)
    if t in D.COURSE_LOCAL_TYPES:
        return idx, Tile(t, course, n=int(f["toGet"]), cls=cls, raw=line)
    if t in ("STARS_MULTIPLE_LEVELS", "DANGEROUS_WALL_KICKS"):
        return idx, Tile(t, k=int(f["toGetEachCourse"]), n=int(f["toGetTotal"]), cls=cls,
                         raw=line)
    if t == "BOWSER":
        n = D.BOWSER_LEVEL[int(f["level"])]
        return idx, Tile(t, D.BOWSER_COURSE[n], D.BOWSER_STAR, n=n, cls=cls, raw=line)
    return idx, Tile(t, n=int(f.get("toGet", 1)), cls=cls, raw=line)


def load_board(path):
    """25 Tiles in cell order, or None for a broken dump."""
    try:
        lines = [l for l in open(path, errors="replace").read().splitlines() if l.strip()]
    except IOError:
        return None
    if len(lines) != 25:
        return None
    cells = [None] * 25
    for l in lines:
        i, tile = parse_dump_line(l)
        cells[i] = tile
    return cells


# --------------------------------------------------------------------------
# Parameters

def load_params(path=None):
    path = path or os.path.join(HERE, "params.json")
    with open(path) as fh:
        return json.load(fh)


class Demands(object):
    __slots__ = ("pinned", "must", "local", "sil", "red_stars", "sml", "multistar",
                 "counters", "blj", "dwk", "multicoin", "lives")

    def __init__(self):
        self.pinned = {}      # (course, star) -> work ds (max over tiles)
        self.must = set()     # mandatory courses
        self.local = []       # (kind, course, n)
        self.sil = {}         # course -> N
        self.red_stars = 0
        self.sml = []         # (k, n)
        self.multistar = 0
        self.counters = {}    # type -> N (max)
        self.blj = 0
        self.dwk = []         # (k, n)
        self.multicoin = 0
        self.lives = 0


class Model(object):
    def __init__(self, params=None):
        self.p = params if params is not None else load_params()
        self._build()
        self._memo = {}

    # ---- tables ------------------------------------------------------
    def _build(self):
        p = self.p
        self.V = {c: int(p["V"][c]) for c in D.ALL_PLACES}
        self.reentry = int(p["reentry"])
        cp = p["coins"]
        self.coin_free = int(cp["free_per_visit"])
        self.coin_free_star = int(cp["free_with_star"])
        # star base work table
        sp = p["star"]
        self.s = {}
        for c in D.COURSES:
            row = {}
            for i in D.course_stars(c):
                k1 = "%s:%d" % (c, i + 1)
                if k1 in sp["overrides"]:
                    w = int(sp["overrides"][k1])
                elif c in D.MAIN and i < 6:
                    w = D.STAR_TIMES[c][i] * 10 * int(sp["scale_pct"]) // 100
                elif c in D.MAIN:
                    w = self.coin_cost(c, self.coin_free, 100) + int(sp["hundred_extra"])
                else:
                    w = int(sp["special"][k1])
                row[i] = max(w, int(sp["min"]))
            if c in ("BitDW", "BitFS", "BitS"):
                row[D.BOWSER_STAR] = int(sp["special"]["%s:B" % c])
            self.s[c] = row
        # sorted real stars per course (exclude Bowser pseudo star)
        self.star_order = {c: sorted(D.course_stars(c), key=lambda i: (self.s[c][i], i))
                           for c in D.COURSES}
        # counter blocks per (type, course): [(unit_cost, units)] ascending
        fp = p["f"]
        tail = p["counter_tail"]
        self.cblocks = {}
        for t, sup in D.SUPPLY.items():
            u = int(p["u"][t])
            pct = int(fp["type_pct"].get(t, fp["pct"]))
            ov = fp["overrides"].get(t, {})
            for c, n in sup.items():
                if c in ov and not c.startswith("_"):
                    free = min(int(ov[c]), n)
                elif c == D.CASTLE:
                    free = 0
                else:
                    free = min(n * pct // 100, int(fp["cap"]))
                self.cblocks[(t, c)] = (free, n, u)
        self.tail_pct = int(tail["pct"])
        self.tail_mult = int(tail["mult"])
        op = p["oneups"]
        self.oneup_free_pct = int(op["free_pct"])
        self.oneup_u = int(op["u"])
        self.oneup_tail = int(op["tail_mult"])
        self.oneup_castle_u = int(op["castle_u"])
        self.start_lives = int(op["start_lives"])

    # ---- primitive costs --------------------------------------------
    def coin_cost(self, c, have, want):
        """Cost to go from `have` coins to `want` coins in one entry of c."""
        cp = self.p["coins"]
        tot = D.COIN_TOTAL[c]
        u = int(cp["u"])
        b1 = tot * int(cp["tier1_pct"]) // 100
        b2 = tot * int(cp["tier2_pct"]) // 100
        cost = 0
        for lo, hi, m in ((0, b1, 1), (b1, b2, int(cp["tier2_mult"])),
                          (b2, 1 << 30, int(cp["tier3_mult"]))):
            a = max(have, lo)
            b = min(want, hi)
            if b > a:
                cost += (b - a) * u * m
        return cost

    def counter_blocks(self, t, c, fed):
        """[(unit_cost, units)] for counter t in course c, `fed` units already had."""
        free, n, u = self.cblocks[(t, c)]
        avail = n - fed
        if avail <= 0:
            return []
        cut = n * self.tail_pct // 100
        out = []
        f = min(free, avail)
        if f:
            out.append((0, f))
        rest = avail - f
        # units fed/free consume from the cheap part first
        cheap = max(0, cut - fed - f)
        a = min(rest, cheap)
        if a:
            out.append((u, a))
        if rest - a:
            out.append((u * self.tail_mult, rest - a))
        return out

    def star_work(self, tile):
        """Work for a pinned star-type tile, challenge multiplier applied."""
        t, c, i = tile.type, tile.course, tile.star
        base = self.s[c][i]
        m = self.p["modifiers"]
        if t in ("STAR", "BOWSER"):
            return base
        if t == "STAR_TIMED":
            mt = m["STAR_TIMED"]
            pct = mt["base_pct"]
            if tile.extra is not None and c in D.STAR_TIMES and i < 6:
                slack = tile.extra - D.STAR_TIMES[c][i]
                if slack < mt["tight_s"]:
                    pct += (mt["tight_s"] - max(slack, 0)) * mt["per_s_pct"]
            return base * pct // 100
        if t == "STAR_CLICK_GAME":
            mc = m["STAR_CLICK_GAME"]
            lo = D.CLICK.get((c, i + 1), (0, 0))[0]
            tight = tile.extra is not None and tile.extra <= lo
            pct = mc["base_pct"] + (mc["tight_pct"] if tight else 0)
            return base * pct // 100
        if t == "STAR_DAREDEVIL":
            md = m["STAR_DAREDEVIL"]
            pct = md["hundred_pct"] if i == 6 else md["base_pct"]
            return base * pct // 100
        return base * int(m[t]) // 100

    # ---- demands ----------------------------------------------------
    def demands(self, tiles):
        d = Demands()
        for tl in tiles:
            t = tl.type
            if t in D.STAR_TYPES or t == "BOWSER":
                key = (tl.course, tl.star)
                d.pinned[key] = max(d.pinned.get(key, 0), self.star_work(tl))
                d.must.add(tl.course)
            elif t == "RANDOM_RED_COINS":
                key = (tl.course, D.RED_STAR[tl.course])
                w = self.s[tl.course][key[1]] * int(self.p["modifiers"][t]) // 100
                d.pinned[key] = max(d.pinned.get(key, 0), w)
                d.must.add(tl.course)
            elif t == "RACING_STARS" or t == "SECRETS_STARS":
                for c, i in (D.RACING if t == "RACING_STARS" else D.SECRETS):
                    d.pinned[(c, i)] = max(d.pinned.get((c, i), 0), self.s[c][i])
                    d.must.add(c)
            elif t == "STARS_IN_LEVEL":
                d.sil[tl.course] = max(d.sil.get(tl.course, 0), tl.n)
                d.must.add(tl.course)
            elif t in ("COIN", "1UPS_IN_LEVEL", "SPLATOON", "RANDOM_STARS"):
                d.local.append((t, tl.course, tl.n))
                d.must.add(tl.course)
            elif t == "ROOF_WITHOUT_CANNON":
                d.local.append((t, D.CASTLE, 1))
            elif t == "RED_COIN_STARS":
                d.red_stars = max(d.red_stars, tl.n)
            elif t == "STARS_MULTIPLE_LEVELS":
                d.sml.append((tl.k, tl.n))
            elif t == "MULTISTAR":
                d.multistar = max(d.multistar, tl.n)
            elif t in D.SUPPLY:
                d.counters[t] = max(d.counters.get(t, 0), tl.n)
            elif t == "BLJ":
                d.blj = max(d.blj, tl.n)
            elif t == "DANGEROUS_WALL_KICKS":
                d.dwk.append((tl.k, tl.n))
            elif t == "MULTICOIN":
                d.multicoin = max(d.multicoin, tl.n)
            elif t == "LIVES":
                d.lives = max(d.lives, tl.n)
            else:
                raise KeyError(t)
        return d

    # ---- generic greedy fill -----------------------------------------
    def _fill(self, need, S, blocks_of, courses, log=None, what=""):
        """Take `need` units. blocks_of(c) -> [(unit_cost, units)] ascending
        (current state). Courses in S are open; others cost V to open.
        Returns (cost, {course: units}) and adds opened courses to S."""
        cost = 0
        taken = {}
        if need <= 0:
            return 0, taken
        pool = {}
        for c in courses:
            if c in S:
                b = blocks_of(c)
                if b:
                    pool[c] = list(b)
        closed = [c for c in courses if c not in S]
        while need > 0:
            # cheapest open unit
            best_c, best_u = None, INF
            for c, b in pool.items():
                if b and b[0][0] < best_u:
                    best_u, best_c = b[0][0], c
            # best course to open: average cost for min(need, cap) units
            oc, on, ocost = None, 0, 0
            for c in closed:
                b = blocks_of(c)
                if not b:
                    continue
                n = 0
                w = self.V[c]
                for uc, un in b:
                    k = min(un, need - n)
                    w += uc * k
                    n += k
                    if n >= need:
                        break
                if n and (oc is None or w * on < ocost * n):
                    oc, on, ocost = c, n, w
            if oc is not None and (best_c is None or ocost < best_u * on):
                S.add(oc)
                closed.remove(oc)  # its V is charged once, in _solve's visit sum
                pool[oc] = list(blocks_of(oc))
                if log is not None:
                    log.append(("open %s for %s" % (oc, what), self.V[oc]))
                continue
            if best_c is None:
                return INF, taken
            uc, un = pool[best_c][0]
            k = min(un, need)
            cost += uc * k
            need -= k
            taken[best_c] = taken.get(best_c, 0) + k
            if k == un:
                pool[best_c].pop(0)
            else:
                pool[best_c][0] = (uc, un - k)
        return cost, taken

    # ---- the plan ----------------------------------------------------
    def _solve(self, d, S0, log=None, counters_first=False):
        S = set(S0) | d.must | {D.CASTLE}
        stars = {}                         # course -> {idx: work}
        for (c, i), w in d.pinned.items():
            stars.setdefault(c, {})[i] = w

        def n_real(c):
            return sum(1 for i in stars.get(c, ()) if i != D.BOWSER_STAR)

        def marg_star(c, i):
            """Marginal cost of adding star i to course c right now."""
            w = self.s[c][i]
            if c in ("BitDW", "BitFS", "BitS") or not stars.get(c):
                return w
            return w + self.reentry

        def free_stars(c):
            have = stars.get(c, {})
            return [i for i in self.star_order[c] if i not in have]

        def add_stars(c, k):
            for i in free_stars(c)[:k]:
                stars.setdefault(c, {})[i] = self.s[c][i]

        # 1. STARS_IN_LEVEL
        for c, n in d.sil.items():
            add_stars(c, max(0, n - n_real(c)))

        def phase_stars():
            # 2. RED_COIN_STARS
            if d.red_stars:
                have = sum(1 for c, i in D.RED_STAR.items() if i in stars.get(c, {}))

                def rblk(c):
                    i = D.RED_STAR[c]
                    return [] if i in stars.get(c, {}) else [(marg_star(c, i), 1)]
                _, taken = self._fill(d.red_stars - have, S, rblk, list(D.RED_STAR), log,
                                      "red-coin stars")
                for c in taken:
                    stars.setdefault(c, {})[D.RED_STAR[c]] = self.s[c][D.RED_STAR[c]]

            # 3. STARS_MULTIPLE_LEVELS (per course unit = top it up to k stars)
            for k, n in sorted(d.sml, reverse=True):
                def sblk(c, k=k):
                    need = k - n_real(c)
                    if need <= 0:
                        return [(0, 1)]
                    fs = free_stars(c)
                    if len(fs) < need:
                        return []
                    w = 0
                    first = not stars.get(c)
                    for j, i in enumerate(fs[:need]):
                        w += self.s[c][i] + (0 if (first and j == 0) else self.reentry)
                    return [(w, 1)]
                _, taken = self._fill(n, S, sblk, D.MAIN, log, "stars in %d courses" % n)
                for c in taken:
                    add_stars(c, max(0, k - n_real(c)))

            # 4. MULTISTAR
            if d.multistar:
                have = sum(n_real(c) for c in stars)

                def mblk(c):
                    fs = free_stars(c)
                    if not fs:
                        return []
                    out = [(marg_star(c, fs[0]), 1)]
                    for i in fs[1:]:
                        out.append((self.s[c][i] + self.reentry, 1))
                    return sorted(out)
                _, taken = self._fill(d.multistar - have, S, mblk, D.COURSES, log, "total stars")
                for c, k in taken.items():
                    add_stars(c, k)

            return 0

        def phase_counters():
            cost = 0
            deaths = 0
            # 6. counters (kills, signs, reds, deaths...), largest need first
            pass
            for t, n in sorted(d.counters.items(), key=lambda kv: -kv[1]):
                fed = {}
                if t == "RED_COIN":
                    for c, st in stars.items():
                        if c in D.RED_STAR and (D.RED_STAR[c] in st or 6 in st):
                            fed[c] = 8
                have = sum(fed.values())
                sup = D.SUPPLY[t]
                w, taken = self._fill(n - have, S, lambda c: self.counter_blocks(t, c, fed.get(c, 0)),
                                      list(sup), log, t)
                if t == "UNIQUE_DEATHS":
                    deaths = n
                cost += w
                if log is not None:
                    log.append(("%s %d via %s" % (t, n, ",".join(
                        "%s:%d" % kv for kv in sorted(taken.items()))), w))

            # 7. BLJ / wall kicks: N distinct (non-hub) courses
            if d.blj:
                per = int(self.p["blj"]["per_course"])
                w, taken = self._fill(d.blj, S, lambda c: [(per, 1)], D.COURSES, log, "BLJ")
                cost += w
                if log is not None:
                    log.append(("BLJ in %d courses" % d.blj, w))
            for k, n in d.dwk:
                dp = self.p["dwk"]
                per = int(dp["per_course"]) + k * int(dp["per_kick"])
                w, taken = self._fill(n, S, lambda c: [(per, 1)], D.MAIN, log, "wall kicks")
                cost += w
                if log is not None:
                    log.append(("wall kicks %dx%d" % (k, n), w))

            return cost, deaths

        if counters_first:
            cost_c, deaths = phase_counters()
            phase_stars()
        else:
            phase_stars()
            cost_c, deaths = phase_counters()

        # star cost
        cost = 0
        for c, st in stars.items():
            real = [w for i, w in st.items() if i != D.BOWSER_STAR]
            cost += sum(st.values())
            if c not in ("BitDW", "BitFS", "BitS") and len(real) > 1:
                cost += self.reentry * (len(real) - 1)
        if log is not None and stars:
            log.append(("stars " + " ".join("%s%s" % (c, ",".join(
                "B" if i == D.BOWSER_STAR else str(i + 1) for i in sorted(st)))
                for c, st in sorted(stars.items())), cost))

        cost += cost_c
        # 5. course-local work + coin ledger
        coins = {}
        lives_have = 0
        oneups_used = {}
        for c in S:
            if c != D.CASTLE:
                coins[c] = self.coin_free + (self.coin_free_star if n_real(c) else 0)
                if 6 in stars.get(c, {}):
                    coins[c] = max(coins[c], 100)
        for kind, c, n in d.local:
            if kind == "COIN":
                have = coins.get(c, self.coin_free)
                w = self.coin_cost(c, have, n) if n > have else 0
                coins[c] = max(have, n)
            elif kind == "1UPS_IN_LEVEL":
                tot = D.ONEUPS[c]
                free = tot * self.oneup_free_pct // 100
                cut = tot * 70 // 100
                w = 0
                for j in range(free, n):
                    w += self.oneup_u * (self.oneup_tail if j >= cut else 1)
                lives_have += n
                oneups_used[c] = max(oneups_used.get(c, 0), n)
            elif kind == "SPLATOON":
                sp = self.p["splatoon"]
                free = D.FLOORS[c] * (int(sp["free_pct"]) +
                                      int(sp["free_pct_per_star"]) * n_real(c)) // 100
                w = max(0, n - free) * int(sp["per_tile"])
            elif kind == "RANDOM_STARS":
                pp = self.p["purple"]
                w = int(pp["per_star"]) * n
                if c in pp["big"]:
                    w = w * int(pp["big_course_pct"]) // 100
            elif kind == "ROOF_WITHOUT_CANNON":
                w = int(self.p["roof"]["work"])
            cost += w
            if log is not None:
                log.append(("%s %s %d" % (kind, c, n), w))

        # entries may have grown: new courses give their free coins
        for c in S:
            if c != D.CASTLE and c not in coins:
                coins[c] = self.coin_free + (self.coin_free_star if n_real(c) else 0)

        # 8. MULTICOIN
        if d.multicoin:
            have = sum(coins.values())

            def cblk(c):
                h = coins.get(c, self.coin_free)
                tot = D.COIN_TOTAL[c]
                out = []
                cp = self.p["coins"]
                for lo, hi, m in ((0, tot * int(cp["tier1_pct"]) // 100, 1),
                                  (tot * int(cp["tier1_pct"]) // 100,
                                   tot * int(cp["tier2_pct"]) // 100, int(cp["tier2_mult"])),
                                  (tot * int(cp["tier2_pct"]) // 100, tot,
                                   int(cp["tier3_mult"]))):
                    a = max(h, lo)
                    if hi > a:
                        out.append((int(cp["u"]) * m, hi - a))
                if c not in S:  # a new course also brings its free coins
                    out.insert(0, (0, self.coin_free))
                return out
            w, taken = self._fill(d.multicoin - have, S, cblk, D.COURSES, log, "coins")
            for c, k in taken.items():
                coins[c] = coins.get(c, 0) + k
            cost += w
            if log is not None:
                log.append(("MULTICOIN %d (had %d)" % (d.multicoin, have), w))

        # 9. LIVES (deaths drain them)
        if d.lives:
            lives_have += sum(v // 50 for v in coins.values()) * int(self.p["coins"]["lives_per_50"])
            need = d.lives - self.start_lives + deaths - lives_have

            def lblk(c):
                tot = D.ONEUPS.get(c, 0) - oneups_used.get(c, 0)
                if tot <= 0:
                    return []
                if c == D.CASTLE:
                    return [(self.oneup_castle_u, tot)]
                free = min(tot, D.ONEUPS[c] * self.oneup_free_pct // 100)
                cut = D.ONEUPS[c] * 70 // 100
                out = []
                if free:
                    out.append((0, free))
                a = max(0, min(tot - free, cut - free))
                if a:
                    out.append((self.oneup_u, a))
                if tot - free - a > 0:
                    out.append((self.oneup_u * self.oneup_tail, tot - free - a))
                return out
            w, taken = self._fill(need, S, lblk, D.ALL_PLACES, log, "lives")
            cost += w
            if log is not None:
                log.append(("LIVES %d (+%d deaths, had %d)" % (d.lives, deaths, lives_have), w))

        vcost = sum(self.V[c] for c in S)
        if log is not None:
            log.insert(0, ("visit " + " ".join(sorted(c for c in S if c != D.CASTLE)), vcost))
        return cost + vcost, S

    # ---- candidate courses for the exact search ----------------------
    def _scores(self, d):
        """Course -> how many counter types rank it in their top 3."""
        must = d.must | {D.CASTLE}
        score = {}
        for t, n in d.counters.items():
            ranked = []
            for c in D.SUPPLY[t]:
                if c in must:
                    continue
                b = self.counter_blocks(t, c, 0)
                k = 0
                w = self.V[c]
                for uc, un in b:
                    take = min(un, n - k)
                    w += uc * take
                    k += take
                if k:
                    ranked.append((w * 1000 // k, c))
            ranked.sort()
            for rank, (_, c) in enumerate(ranked[:3]):
                score.setdefault(c, [0, 0])
                score[c][0] += 3 - rank
                score[c][1] += 1
        return score

    def _optional(self, d, cap=10):
        score = self._scores(d)
        if d.lives:
            for c in ("TTM", "RR", "SSL", "LLL"):
                if c not in d.must:
                    score.setdefault(c, [0, 0])[0] += 1
        opt = sorted(score, key=lambda c: (-score[c][0], self.V[c], c))
        return opt[:cap]

    # ---- public API --------------------------------------------------
    def exact_set_cost(self, tiles, explain=False):
        """Min over subsets of candidate courses x both phase orders."""
        key = tuple(sorted(t.key() for t in tiles))
        if not explain and key in self._memo:
            return self._memo[key]
        d = self.demands(tiles)
        opt = self._optional(d)
        best, arg = INF, None
        orders = (False, True) if (d.counters or d.blj or d.dwk) and \
            (d.red_stars or d.sml or d.multistar) else (False,)
        for r in range(len(opt) + 1):
            for sub in combinations(opt, r):
                for cf in orders:
                    c, S = self._solve(d, sub, counters_first=cf)
                    if c < best:
                        best, arg = c, (sub, cf)
        if not explain:
            self._memo[key] = best
            return best
        log = []
        c, S = self._solve(d, arg[0], log, counters_first=arg[1])
        return c, log

    def fast_line_cost(self, tiles, explain=False, passes=4):
        """What C would do: <= 4 fixed greedy passes, no subset search.
        pass 1: mandatory courses, stars-then-counters
        pass 2: same, counters-then-stars
        pass 3/4: also pre-open courses that >= 2 counter types rank in
                  their top 3 (the 'hub' of the line)
        passes=1 gives the pure one-pass number."""
        d = self.demands(tiles)
        variants = [((), False), ((), True)]
        pre = tuple(sorted(c for c, (sc, k) in self._scores(d).items() if k >= 2))
        if pre:
            variants += [(pre, False), (pre, True)]
        best, arg = INF, None
        for S0, cf in variants[:passes]:
            c, S = self._solve(d, S0, counters_first=cf)
            if c < best:
                best, arg = c, (S0, cf)
        if not explain:
            return best
        log = []
        c, S = self._solve(d, arg[0], log, counters_first=arg[1])
        return c, log

    def standalone_cost(self, tile):
        return self.exact_set_cost([tile])

    def synergy(self, tiles):
        return sum(self.standalone_cost(t) for t in tiles) - self.exact_set_cost(tiles)

    def pair_synergy(self, a, b):
        return self.standalone_cost(a) + self.standalone_cost(b) - self.exact_set_cost([a, b])

    def home_courses(self, tile):
        """Courses a tile lives in (pinned course, or its supply courses)."""
        if tile.course:
            return {tile.course}
        if tile.type == "RACING_STARS":
            return {c for c, _ in D.RACING}
        if tile.type == "SECRETS_STARS":
            return {c for c, _ in D.SECRETS}
        if tile.type in D.SUPPLY:
            return {c for c in D.SUPPLY[tile.type] if c != D.CASTLE}
        return set()


def fmt_time(ds):
    if ds >= INF:
        return "inf"
    s = ds // 10
    return "%d:%02d" % (s // 60, s % 60)


def load_catalog():
    with open(os.path.join(HERE, "catalog.json")) as fh:
        return json.load(fh)


def _main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("keys", nargs="*", help="catalog keys to cost as one set")
    ap.add_argument("--catalog", action="store_true", help="standalone cost of every catalog tile")
    ap.add_argument("--approx", type=str, default=None,
                    help="dir of board dumps: report fast-vs-exact error over all lines")
    ap.add_argument("--limit", type=int, default=400)
    a = ap.parse_args()
    m = Model()
    if a.catalog:
        cat = load_catalog()
        for t in cat["tiles"]:
            tile = parse_key(t["key"])
            print("%-34s %6s  %s" % (t["key"], fmt_time(m.standalone_cost(tile)), t["text"]))
    if a.keys:
        tiles = [parse_key(k) for k in a.keys]
        for t in tiles:
            print("%-34s standalone %s" % (t.key(), fmt_time(m.standalone_cost(t))))
        c, log = m.exact_set_cost(tiles, explain=True)
        print("exact set cost %s   synergy %s" % (fmt_time(c), fmt_time(m.synergy(tiles))))
        for what, w in log:
            print("   %-60s %s" % (what, fmt_time(w)))
        f, flog = m.fast_line_cost(tiles, explain=True)
        print("fast one-pass  %s" % fmt_time(f))
    if a.approx:
        approx_report(m, a.approx, a.limit)


def approx_report(m, board_dir, limit):
    import glob
    errs = []
    one = []
    files = sorted(glob.glob(os.path.join(board_dir, "*.txt")),
                   key=lambda p: int(os.path.basename(p)[:-4]))[:limit]
    for f in files:
        b = load_board(f)
        if not b:
            continue
        for ln in D.LINES:
            tiles = [b[i] for i in ln]
            e = m.exact_set_cost(tiles)
            fa = m.fast_line_cost(tiles)
            errs.append((fa - e) * 1000 // e)
            one.append((m.fast_line_cost(tiles, passes=1) - e) * 1000 // e)
    errs.sort()
    n = len(errs)
    print("fast vs exact over %d lines (%d boards): fast is an upper bound" % (n, len(files)))
    one.sort()
    print("  pure one-pass: exact match %.1f%%, mean over-estimate %.2f%%, p90 %.1f%%, max %.1f%%" % (
        100.0 * sum(1 for e in one if e == 0) / n, sum(one) / 10.0 / n, one[n * 9 // 10] / 10.0,
        one[-1] / 10.0))
    print("  4-pass fast: exact match: %.1f%%" % (100.0 * sum(1 for e in errs if e == 0) / n))
    print("  mean over-estimate %.2f%%, median %.1f%%, p90 %.1f%%, p99 %.1f%%, max %.1f%%" % (
        sum(errs) / 10.0 / n, errs[n // 2] / 10.0, errs[n * 9 // 10] / 10.0,
        errs[n * 99 // 100] / 10.0, errs[-1] / 10.0))
    return errs


if __name__ == "__main__":
    _main()
