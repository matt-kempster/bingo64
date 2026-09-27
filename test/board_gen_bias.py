#!/usr/bin/env python3
"""Cell-based course bias of the bingo64 board generator.

Simulates setup_bingo_objectives() faithfully (class grid, mutation,
harder/easier coin flip, weighted draws with budget drain including the
want_sum==0 edge), then samples each objective's course the way its init
function does. Global-counter objectives (kills, amps, signs, poles,
cannons, red coins, boxes) are attributed to courses proportional to the
unique-kill supply counted from sm64.sql (verified against the tracker's
MAX_* constants). Dedup pass is NOT simulated (small effect; see notes).

The objective list and the four weight tables are read straight from
src/game/bingo.h and src/game/bingo_board_setup.c, so they can't go stale.
What this file still knows by hand is where each type's cells land
(course_vector). `--check` fails if any enum type is missing from that
knowledge; test/host's `make test` runs it, so a new tile can't be
forgotten here.

  python3 test/board_gen_bias.py            # full bias report (slow-ish)
  python3 test/board_gen_bias.py --check    # completeness check (fast)
"""
import os
import random
import re
import sys
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINGO_H = os.path.join(REPO, "src", "game", "bingo.h")
BOARD_SETUP_C = os.path.join(REPO, "src", "game", "bingo_board_setup.c")
PREFIX = "BINGO_OBJECTIVE_"


def _strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def parse_objective_enum(path=BINGO_H):
    """enum BingoObjectiveType as [(name, value)] in order, prefix dropped.
    Range markers (*_MIN, *_MAX, TOTAL_AMOUNT) are left out; an entry
    written `X = MARKER` (e.g. MULTICOIN = COLLECTABLE_MIN) is a real type."""
    text = _strip_comments(open(path).read())
    m = re.search(r"enum\s+BingoObjectiveType\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"enum BingoObjectiveType not found in {path}")
    values, out, nxt = {}, [], 0
    for entry in m.group(1).split(","):
        entry = entry.strip()
        if not entry:
            continue
        name, _, rhs = (p.strip() for p in entry.partition("="))
        if rhs:
            rhs = rhs[len(PREFIX):] if rhs.startswith(PREFIX) else rhs
            nxt = values[rhs] if rhs in values else int(rhs, 0)
        name = name[len(PREFIX):]
        values[name] = nxt
        if not re.search(r"(_MIN|_MAX|TOTAL_AMOUNT)$", name):
            out.append((name, nxt))
        nxt += 1
    return out


def parse_weight_tables(path=BOARD_SETUP_C):
    """sWeights{Easy,Medium,Hard,Center}[] as {CLASS: [(type, weight, uses)]}."""
    text = _strip_comments(open(path).read())
    tables = {}
    for cls in ("Easy", "Medium", "Hard", "Center"):
        m = re.search(r"sWeights%s\[\]\s*=\s*\{(.*?)\};" % cls, text, re.S)
        if not m:
            raise SystemExit(f"sWeights{cls} not found in {path}")
        rows = re.findall(r"\{\s*%s(\w+)\s*,\s*(\d+)\s*,\s*(NO_LIMIT|-?\d+)\s*\}" % PREFIX,
                          m.group(1))
        tables[cls.upper()] = [(t, int(w), NL if u == "NO_LIMIT" else int(u))
                               for t, w, u in rows]
    return tables


MAIN = ["BOB","WF","JRB","CCM","BBH","HMC","LLL","SSL","DDD","SL","WDW","TTM","THI","TTC","RR"]
SPECIAL = ["BitDW","BitFS","BitS","PSS","CotMC","TotWC","VCutM","WMotR","SA"]
COURSES = MAIN + SPECIAL
ANY = "ANY(choice)"
CASTLE = "Castle"

# ---------- supply vectors from sm64.sql (unique-kill supply) ----------
SUPPLY = {
 "KILL_GOOMBAS":  {"BOB":11,"JRB":3,"SSL":12,"SL":3,"THI":21,"TTM":9,"RR":1,"BitDW":6,"BitFS":3,"BitS":8},
 "KILL_BOBOMBS":  {"BOB":12,"TTM":5,"RR":4,"BitS":4,"TTC":2,"SSL":2,"BitFS":1},
 "KILL_SPINDRIFTS":{"SL":14,"CCM":5},
 "KILL_MR_IS":    {"BBH":4,"LLL":2,"HMC":2},
 "KILL_SCUTTLEBUGS":{"HMC":6,"BBH":3},
 "KILL_BULLIES":  {"LLL":12,"BitFS":4},
 "KILL_CHUCKYAS": {"WDW":1,"TTM":1,"THI":1,"RR":1,"BitS":1},
 "AMPS":          {"WDW":5,"BitDW":5,"BitS":3,"SSL":3,"TTC":2,"RR":2,"BitFS":2,"VCutM":1,"SL":1},
 "SIGNPOST":      {"HMC":14,"BOB":13,"WF":8,CASTLE:8,"CCM":7,"SSL":5,"JRB":5,"SL":4,"BBH":4,
                   "TTM":3,"THI":3,"LLL":3,"WDW":2,"PSS":1,"DDD":1,"CotMC":1,"BitDW":1},
 "POLES":         {"WMotR":6,"RR":6,"LLL":6,"JRB":4,"SSL":2,"HMC":2,"BitS":2,"WF":2,"WDW":1,
                   "TTC":1,"BitFS":5,"DDD":9},
 "SHOOT_CANNONS": {"BOB":6,"CCM":3,"WMotR":2,"WF":1,"WDW":1,"TTM":1,"THI":1,"SSL":1,"SL":1,
                   "RR":1,"JRB":1,CASTLE:1},
 "RED_COIN":      {c:8 for c in COURSES},
 # switch + moving + enemy-drop blue coins; ground-pounded goombas (BOB, WF,
 # TTM, THI, ...) and BBH boos also drop them but are left out here.
 "BLUE_COIN":     {"HMC":9,"BBH":7,"TTC":7,"WF":7,"SSL":7,"DDD":6,"JRB":6,"RR":6,"WDW":6,
                   "PSS":6,"TTM":3,"CCM":3,"THI":2,"LLL":1},
 "EXCLAMATION_MARK_BOX": {"TTC":13,"WDW":8,"HMC":2,"SSL":4,"WMotR":1,"BBH":2,"JRB":3,"THI":5,
                   "SL":5,"BOB":1,"RR":4,"BitDW":3,"BitFS":4,"VCutM":2,"CCM":3,"CotMC":1,
                   "BitS":1,"LLL":2,CASTLE:1,"PSS":1,"TTM":1},
 "WING_CAP_BOX":  {"BOB":3,"LLL":1,CASTLE:1,"SSL":3,"TotWC":1,"WMotR":6},
 "VANISH_CAP_BOX":{"BBH":3,"DDD":1,"SL":1,"VCutM":2,"WDW":2},
 "METAL_CAP_BOX": {"BitDW":1,"CotMC":2,"DDD":1,"HMC":5,"JRB":3,"WDW":1,"WF":1},
 "RACING_STARS":  {"BOB":1,"CCM":1,"THI":1},
 "SECRETS_STARS": {"BOB":1,"SSL":1,"WDW":1,"THI":1},
 "LOSE_MARIO_HAT":{"SSL":1,"SL":1,"TTM":1},
 "ROOF_WITHOUT_CANNON": {CASTLE:1},
 # --- added 2026-09-27 (wave-1 enemies from test/cost_model/data.py; the
 # rest from the supply comments in bingo_objective_init.c) ---
 "KILL_WHOMPS":   {"WF":3,"BitS":1},
 "KILL_BOOS":     {"BBH":13,CASTLE:11},
 "KILL_SNUFITS":  {"HMC":4,"CotMC":4},
 "HURT_BY_CLAMS": {"JRB":5,"DDD":4},
 "KILL_FLY_GUYS": {"THI":3,"SSL":3,"TTM":1,"SL":1,"RR":1},
 "KILL_MR_BLIZZARDS":{"SL":4,"CCM":3},
 "KILL_SKEETERS": {"WDW":4},
 "KILL_KOOPAS":   {"BOB":1,"THI":2},
 "CRUSHED":       {"WF":5,"TTC":1,"SSL":7,"BitS":1},
 # GUESS (cost model): stars that can be hit mid-flight from a cannon
 "CANNON_STARS":  {"WF":1,"BOB":1,"WDW":1,"TTM":1,"THI":1,"RR":1,"SSL":1,"CCM":1,"JRB":1,"WMotR":1},
 "WARP_PADS":     {"BOB":2,"SSL":2,"WF":1,"CCM":1,"LLL":1,"WDW":1,"TTM":1,"THI":1,"SL":1,"RR":1},
 "KOOPA_SHELLS":  {"LLL":1,"SSL":1,"SL":1,"JRB":1,"DDD":1,"BOB":1,"THI":1},
 "SPIN_HEARTS":   {"BitFS":2,"BitS":2,"RR":2,"TTC":2,"BOB":1,"CCM":1,"HMC":1,"LLL":1,"SSL":1},
 "PURPLE_SWITCHES":{"WDW":3,"BitDW":2,"BOB":1,"HMC":1,"JRB":1,"DDD":1,"TTM":1,"THI":1,"RR":1,"BitS":1},
 "STUCK_IN_GROUND":{"SL":1,"CCM":1,"SSL":1,"WMotR":1},
 "CAPS_WORN":     {"TotWC":1,"CotMC":1,"VCutM":1},
 "OPEN_CANNONS":  {c:1 for c in ["BOB","WF","JRB","CCM","SSL","SL","WDW","TTM","THI","RR"]},
 "TOAD_STARS":    {CASTLE:3},
 "MIPS":          {CASTLE:1},
}
PLAYER_CHOICE = {"MULTICOIN","MULTISTAR","STARS_MULTIPLE_LEVELS","LIVES","UNIQUE_DEATHS",
                 "BLJ","DANGEROUS_WALL_KICKS","COINS_MULTIPLE_LEVELS","1UPS_MULTIPLE_LEVELS"}
# Progression tiles are only dealt with "Unlock full game" OFF; the
# presets below assume the default (unlock ON), so they leave the draw.
PROGRESSION = {"OPEN_CANNONS","TOAD_STARS","MIPS"}

# ---------- star-pool vectors ----------
def star115():
    v = {c:7 for c in MAIN}
    v["PSS"]=2
    for c in ["SA","TotWC","CotMC","VCutM","WMotR","BitDW","BitFS","BitS"]: v[c]=1
    return v
def minus(v, deltas):
    v = dict(v)
    for c,d in deltas.items(): v[c]-=d
    return v
STAR115 = star115()
BBC = minus(STAR115, {"BOB":1,"TTM":1,"CCM":1})
ZBC = minus(STAR115, {"WF":1,"JRB":1,"DDD":1,"TTC":1})
DD_MED = minus(STAR115, {"JRB":7,"DDD":7,"WDW":2})
DD_HARD = {c:1 for c in MAIN if c!="DDD"}
CLICK = {"BOB":7,"WF":7,"JRB":6,"CCM":3,"HMC":5,"LLL":6,"SSL":3,"DDD":5,"SL":6,"WDW":7,
         "TTM":2,"THI":4,"TTC":5,"RR":6}
ABC = {"BOB":2,"WF":3,"JRB":1,"HMC":1,"LLL":4,"SSL":1,"SL":3,"WDW":1,"TTM":3,"THI":4}
MAIN15 = {c:1 for c in MAIN}
COURSE24 = {c:1 for c in COURSES}
REDS23 = {c:1 for c in COURSES if c!="PSS"}
ONEUP_MED = {**{c:2 for c in ["CCM","LLL","SSL","TTM","THI","RR","BitDW","BitFS","BitS"]},
             **{c:1 for c in ["HMC","SL","WDW","TTC","VCutM","WMotR"]}}
ONEUP_HARD = {c:1 for c in MAIN if c!="DDD"}
COIN_V = {**{c:76 for c in MAIN}, **{c:24*15/9/15 for c in SPECIAL}}  # placeholder, set below
# COIN: 76% uniform-15 main, 24% uniform-9 special
COIN_V = {**{c:0.76/15 for c in MAIN}, **{c:0.24/9 for c in SPECIAL}}

def course_vector(objtype, cls):
    if objtype == "STAR" or objtype == "STAR_REVERSE_JOYSTICK": return STAR115
    if objtype == "STAR_TIMED" or objtype == "STAR_GREEN_DEMON": return MAIN15
    if objtype == "STAR_TTC_RANDOM": return {"TTC":1}
    if objtype == "STAR_A_BUTTON_CHALLENGE": return ABC
    if objtype == "STAR_B_BUTTON_CHALLENGE": return BBC
    if objtype == "STAR_Z_BUTTON_CHALLENGE": return ZBC
    if objtype == "STAR_CLICK_GAME": return CLICK
    if objtype == "STAR_DAREDEVIL": return DD_HARD if cls=="HARD" else DD_MED
    if objtype == "STAR_COINLESS": return STAR115          # APPROXIMATE (random_coinless_star)
    if objtype == "RANDOM_STARS": return COURSE24          # uniform COURSE_MIN..COURSE_SA
    if objtype == "RED_COIN_STARS": return REDS23
    if objtype == "HUNDRED_COIN_STARS": return MAIN15
    if objtype == "CASTLE_SECRET_STARS":                   # 10 course stars + Toad x3, MIPS x2
        return {"PSS":2,"SA":1,"TotWC":1,"CotMC":1,"VCutM":1,"WMotR":1,
                "BitDW":1,"BitFS":1,"BitS":1,CASTLE:5}
    if objtype == "RANDOM_RED_COINS": return REDS23
    if objtype == "COIN": return COIN_V
    if objtype == "SPLATOON" or objtype == "STARS_IN_LEVEL": return MAIN15
    if objtype == "1UPS_IN_LEVEL": return ONEUP_HARD if cls=="HARD" else ONEUP_MED
    if objtype == "BOWSER":
        return {"BitS":1} if cls in ("HARD","CENTER") else {"BitDW":1,"BitFS":1}
    if objtype in SUPPLY: return SUPPLY[objtype]
    if objtype in PLAYER_CHOICE: return {ANY:1}
    raise KeyError(objtype)

# ---------- weight tables (read from the C source) ----------
NL = -1
TABLES = parse_weight_tables()
ENUM_TYPES = [t for t, _ in parse_objective_enum()]

ALL_TYPES = ENUM_TYPES  # uniform fallback walks the whole enum

# ---------- presets ----------
SRL_ENABLED = {"STAR","COIN","STARS_IN_LEVEL","BOWSER","ROOF_WITHOUT_CANNON","RACING_STARS",
 "SECRETS_STARS","MULTICOIN","MULTISTAR","STARS_MULTIPLE_LEVELS","RED_COIN","BLUE_COIN"}
PRESETS = {
 "default (all on)": set(PROGRESSION),
 "SRL": {t for t in ALL_TYPES if t not in SRL_ENABLED} | PROGRESSION,
 "Vanilla": PROGRESSION | {"STAR_TIMED","STAR_CLICK_GAME","STAR_REVERSE_JOYSTICK","STAR_GREEN_DEMON",
             "STAR_DAREDEVIL","RANDOM_RED_COINS","SPLATOON"},
 "Casual": PROGRESSION | {"STAR_TIMED","STAR_A_BUTTON_CHALLENGE","STAR_B_BUTTON_CHALLENGE",
            "STAR_Z_BUTTON_CHALLENGE","STAR_CLICK_GAME","STAR_REVERSE_JOYSTICK",
            "STAR_GREEN_DEMON","DANGEROUS_WALL_KICKS","ROOF_WITHOUT_CANNON","BLJ"},
}
# SECRETS_STARS only exists in MEDIUM; RACING in EASY+MEDIUM; fine.

GRID = [[1,0,0,0,2],[0,2,0,1,0],[0,0,3,0,0],[0,1,0,2,0],[2,0,0,0,1]]

def switch_to(exclude, rng):
    if exclude==0: o1,o2 = 1,2
    elif exclude==1: o1,o2 = 0,2
    elif exclude==2: o1,o2 = 1,2
    else: o1,o2 = 3,1
    r = rng.randrange(5)
    return o1 if r<2 else (o2 if r<4 else 3)

def draw_type(table, disabled, rng):
    """get_random_objective_type + get_random_enabled_objective_type, faithful."""
    for _ in range(10):
        total = sum(w for t,w,u in table if u!=0 and t not in disabled)
        if total==0: return None  # uniform fallback (rare) -> treat as None
        want = rng.randrange(total)
        i=-1; s=0
        while True:
            i+=1
            t,w,u = table[i]
            if u!=0 and t not in disabled: s += w
            if s>=want and (s>0 or want==0): break  # do-while(sum < want)
        t,w,u = table[i]
        if t not in disabled:
            if u!=NL: table[i]=(t,w,u-1)   # want==0 can leak an out-of-budget row: u 0 -> -1 (infinite)
            return t
    # uniform over enabled
    en = [t for t in ALL_TYPES if t not in disabled]
    return rng.choice(en) if en else None

def simulate(disabled, boards=60000, seed=1):
    rng = random.Random(seed)
    type_count = defaultdict(float)          # expected cells per board by type
    course_mass = defaultdict(float)         # expected cells per board by course
    course_pinned = defaultdict(float)       # from course-pinned objectives only
    for _ in range(boards):
        tables = {k:[list(e) for e in v] for k,v in TABLES.items()}
        tables = {k:[tuple(e) for e in v] for k,v in tables.items()}
        grid = [row[:] for row in GRID]
        harder = rng.randrange(2)+1
        for _ in range(rng.randrange(3)):
            r,c = rng.randrange(5), rng.randrange(5)
            grid[r][c] = switch_to(grid[r][c], rng)
        cells = list(range(25)); rng.shuffle(cells)
        for idx in cells:
            cl = grid[idx%5][idx//5]
            cls = ("HARD" if cl==harder else "EASY") if cl in (1,2) else ("CENTER" if cl==3 else "MEDIUM")
            t = draw_type(tables[cls], disabled, rng)
            if t is None: continue
            type_count[t] += 1
            v = course_vector(t, cls)
            tot = sum(v.values())
            pinned = t not in SUPPLY and t not in PLAYER_CHOICE
            for c_,w_ in v.items():
                course_mass[c_] += w_/tot
                if pinned: course_pinned[c_] += w_/tot
    n = boards
    return ({k:v/n for k,v in type_count.items()},
            {k:v/n for k,v in course_mass.items()},
            {k:v/n for k,v in course_pinned.items()})

def check():
    """Every enum type must have a course vector for every class, and every
    weight-table row must name a real enum type. Returns problems found."""
    problems = []
    enum = set(ENUM_TYPES)
    for t in ENUM_TYPES:
        for cls in TABLES:
            try:
                v = course_vector(t, cls)
            except KeyError:
                problems.append(f"{t}: no course vector in board_gen_bias.py "
                                f"(add it to SUPPLY, PLAYER_CHOICE or course_vector)")
                break
            if not v or sum(v.values()) <= 0:
                problems.append(f"{t}: empty course vector ({cls})")
                break
    for cls, rows in TABLES.items():
        if not rows:
            problems.append(f"sWeights{cls.title()}: parsed no rows")
        for t, _, _ in rows:
            if t not in enum:
                problems.append(f"sWeights{cls.title()}: {t} is not in enum BingoObjectiveType")
    for name, dis in PRESETS.items():
        for t in dis - enum:
            problems.append(f"preset {name!r} disables unknown type {t}")
    return problems


def main(argv):
    if "--check" in argv:
        problems = check()
        for p in problems:
            print("board_gen_bias: " + p, file=sys.stderr)
        if problems:
            return 1
        # A tiny simulation too, so the draw loop itself is exercised.
        simulate(PRESETS["default (all on)"], boards=200)
        print(f"board_gen_bias: {len(ENUM_TYPES)} objective types, "
              f"{sum(len(r) for r in TABLES.values())} weight rows: all covered")
        return 0
    for name, dis in PRESETS.items():
        types, mass, pinned = simulate(dis)
        print(f"\n=== {name} ===")
        print(f"expected cells/board attributed to each course (25 cells total):")
        for c,v in sorted(mass.items(), key=lambda kv:-kv[1]):
            p = pinned.get(c,0.0)
            print(f"  {c:12s} {v:5.2f}  (course-pinned {p:4.2f}, supply/set-attributed {v-p:4.2f})")
        print("objective types (expected cells/board):")
        for t,v in sorted(types.items(), key=lambda kv:-kv[1]):
            print(f"  {t:28s} {v:5.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
