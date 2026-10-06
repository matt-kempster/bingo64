"""Static game facts for the cost model (not priors -- those live in params.json).

Sources:
  * enum order, weight tables, N ranges: master's src/game/bingo.h,
    bingo_board_setup.c, bingo_objective_init.c (snapshot 2026-09-23)
  * star times, 1-ups, floor counts: src/game/bingo_const.c
  * supply tables: test/board_gen_bias.py SUPPLY (from sm64.sql) plus the
    wave-1 enemies (KILL_WHOMPS ... CRUSHED) added by hand
  * coin totals per course: community figures, APPROXIMATE (only used for
    the "last coins are expensive" tiers)
"""

MAIN = ["BOB", "WF", "JRB", "CCM", "BBH", "HMC", "LLL", "SSL", "DDD", "SL",
        "WDW", "TTM", "THI", "TTC", "RR"]
SPECIAL = ["BitDW", "BitFS", "BitS", "PSS", "CotMC", "TotWC", "VCutM", "WMotR", "SA"]
COURSES = MAIN + SPECIAL          # course number = index + 1
CASTLE = "Castle"                 # pseudo-course, always "visited" (V = 0)
ALL_PLACES = COURSES + [CASTLE]
COURSE_NUM = {c: i + 1 for i, c in enumerate(COURSES)}
NUM_COURSE = {i + 1: c for i, c in enumerate(COURSES)}

# enum BingoObjectiveType order on master (dump "type=NN")
TYPES = [
    "STAR", "STAR_TIMED", "STAR_TTC_RANDOM", "STAR_A_BUTTON_CHALLENGE",
    "STAR_B_BUTTON_CHALLENGE", "STAR_Z_BUTTON_CHALLENGE", "STAR_CLICK_GAME",
    "STAR_REVERSE_JOYSTICK", "STAR_GREEN_DEMON", "STAR_DAREDEVIL",
    "RANDOM_STARS", "COIN", "1UPS_IN_LEVEL", "STARS_IN_LEVEL", "RANDOM_RED_COINS",
    "SPLATOON", "DANGEROUS_WALL_KICKS", "BOWSER", "ROOF_WITHOUT_CANNON",
    "RACING_STARS", "SECRETS_STARS", "LIVES", "CANNON_STARS",
    "MULTICOIN", "MULTISTAR", "STARS_MULTIPLE_LEVELS", "BLJ", "LOSE_MARIO_HAT",
    "SIGNPOST", "POLES", "SHOOT_CANNONS", "RED_COIN", "EXCLAMATION_MARK_BOX",
    "WING_CAP_BOX", "VANISH_CAP_BOX", "METAL_CAP_BOX", "AMPS", "KILL_GOOMBAS",
    "KILL_BOBOMBS", "KILL_SPINDRIFTS", "KILL_MR_IS", "KILL_SCUTTLEBUGS",
    "KILL_BULLIES", "KILL_CHUCKYAS", "KILL_WHOMPS", "KILL_BOOS", "KILL_SNUFITS",
    "HURT_BY_CLAMS", "KILL_FLY_GUYS", "KILL_MR_BLIZZARDS", "KILL_SKEETERS",
    "KILL_KOOPAS", "CRUSHED", "UNIQUE_DEATHS", "BLUE_COIN", "RED_COIN_STARS",
]
TYPE_NUM = {t: i for i, t in enumerate(TYPES)}

STAR_TYPES = TYPES[0:10]          # single-star tiles (STAR_MIN..STAR_MAX)
MODIFIED_STAR_TYPES = STAR_TYPES[1:]
COURSE_LOCAL_TYPES = ["COIN", "1UPS_IN_LEVEL", "STARS_IN_LEVEL", "RANDOM_RED_COINS",
                      "SPLATOON", "RANDOM_STARS"]

# Per-course supply of globally-counted, unique units. "Castle" = hub.
SUPPLY = {
    "KILL_GOOMBAS": {"BOB": 11, "JRB": 3, "SSL": 12, "SL": 3, "THI": 21, "TTM": 9, "RR": 1,
                     "BitDW": 6, "BitFS": 3, "BitS": 8},
    "KILL_BOBOMBS": {"BOB": 12, "TTM": 5, "RR": 4, "BitS": 4, "TTC": 2, "SSL": 2, "BitFS": 1},
    "KILL_SPINDRIFTS": {"SL": 14, "CCM": 5},
    "KILL_MR_IS": {"BBH": 4, "LLL": 2, "HMC": 2},
    "KILL_SCUTTLEBUGS": {"HMC": 6, "BBH": 3},
    "KILL_BULLIES": {"LLL": 12, "BitFS": 4, "SL": 1},
    "KILL_CHUCKYAS": {"WDW": 1, "TTM": 1, "THI": 1, "RR": 1, "BitS": 1},
    "KILL_WHOMPS": {"WF": 3, "BitS": 1},
    "KILL_BOOS": {"BBH": 13, CASTLE: 10},
    "KILL_SNUFITS": {"HMC": 4, "CotMC": 4},
    "HURT_BY_CLAMS": {"JRB": 5, "DDD": 4},
    "KILL_FLY_GUYS": {"THI": 3, "SSL": 3, "TTM": 1, "SL": 1, "RR": 1},
    "KILL_MR_BLIZZARDS": {"SL": 4, "CCM": 1},
    "KILL_SKEETERS": {"WDW": 4},
    "KILL_KOOPAS": {"BOB": 1, "THI": 2},
    "CRUSHED": {"WF": 5, "TTC": 1, "SSL": 7, "BitS": 1},
    "AMPS": {"WDW": 5, "BitDW": 5, "BitS": 3, "SSL": 3, "TTC": 2, "RR": 2, "BitFS": 2,
             "VCutM": 1, "SL": 1},
    "SIGNPOST": {"HMC": 14, "BOB": 13, "WF": 8, CASTLE: 8, "CCM": 7, "SSL": 5, "JRB": 5,
                 "SL": 4, "BBH": 4, "TTM": 3, "THI": 3, "LLL": 3, "WDW": 2, "PSS": 1,
                 "DDD": 1, "CotMC": 1, "BitDW": 1},
    "POLES": {"WMotR": 6, "RR": 6, "LLL": 6, "JRB": 4, "SSL": 2, "HMC": 2, "BitS": 2, "WF": 2,
              "WDW": 1, "TTC": 1, "BitFS": 5, "DDD": 9},
    "SHOOT_CANNONS": {"BOB": 6, "CCM": 3, "WMotR": 2, "WF": 1, "WDW": 1, "TTM": 1, "THI": 1,
                      "SSL": 1, "SL": 1, "RR": 1, "JRB": 1, CASTLE: 1},
    "RED_COIN": {c: 8 for c in COURSES if c != "PSS"},
    "BLUE_COIN": {"BBH": 18, "THI": 13, "HMC": 9, "TTC": 7, "WF": 7, "SSL": 7, "DDD": 6,
                  "JRB": 6, "RR": 6, "WDW": 6, "PSS": 6, "TTM": 3, "CCM": 3, "LLL": 2},
    "EXCLAMATION_MARK_BOX": {"TTC": 13, "WDW": 8, "HMC": 2, "SSL": 4, "WMotR": 1, "BBH": 2,
                             "JRB": 3, "THI": 5, "SL": 5, "BOB": 1, "RR": 4, "BitDW": 3,
                             "BitFS": 4, "VCutM": 2, "CCM": 3, "CotMC": 1, "BitS": 1,
                             "LLL": 1, "PSS": 1, "TTM": 1},
    "WING_CAP_BOX": {"BOB": 3, "LLL": 1, CASTLE: 1, "SSL": 3, "TotWC": 1, "WMotR": 6},
    "VANISH_CAP_BOX": {"BBH": 3, "DDD": 1, "SL": 1, "VCutM": 2, "WDW": 2},
    "METAL_CAP_BOX": {"BitDW": 1, "CotMC": 2, "DDD": 1, "HMC": 5, "JRB": 3, "WDW": 1, "WF": 1},
    # 4 hat-loss ways in the tracker: Klepto (SSL), SL wind, TTM wind, Ukiki (TTM)
    "LOSE_MARIO_HAT": {"SSL": 1, "SL": 1, "TTM": 2},
    # GUESS: stars that can be hit mid-flight from a cannon (not audited)
    "CANNON_STARS": {"WF": 1, "BOB": 1, "WDW": 1, "TTM": 1, "THI": 1, "RR": 1, "SSL": 1,
                     "CCM": 1, "JRB": 1, "WMotR": 1},
    # 13 death kinds; the 3 generic ones (standing/back/stomach) are put on
    # the hub as "available anywhere". Each specific kind is assigned to the
    # one course where it is easiest (APPROXIMATE -- a kind is counted once).
    "UNIQUE_DEATHS": {CASTLE: 3, "JRB": 2, "LLL": 1, "SSL": 1, "HMC": 1, "WDW": 1, "DDD": 1,
                      "WF": 1, "RR": 1, "TTM": 1},
}
COUNTER_TYPES = sorted(SUPPLY.keys())

# Stars (0-based) that give all 8 red coins, per course (sRedCoinStars).
RED_STAR = {"BOB": 3, "WF": 3, "JRB": 3, "CCM": 3, "BBH": 3, "HMC": 1, "LLL": 2, "SSL": 4,
            "DDD": 2, "SL": 4, "WDW": 4, "TTM": 2, "THI": 4, "TTC": 5, "RR": 2,
            "BitDW": 0, "BitFS": 0, "BitS": 0, "CotMC": 0, "VCutM": 0, "TotWC": 0,
            "WMotR": 0, "SA": 0}
RACING = [("BOB", 1), ("CCM", 2), ("THI", 2)]
SECRETS = [("BOB", 4), ("SSL", 5), ("WDW", 2), ("THI", 3)]
BOWSER_COURSE = {1: "BitDW", 2: "BitFS", 3: "BitS"}
BOWSER_LEVEL = {30: 1, 33: 2, 34: 3}        # LEVEL_BOWSER_n numbers in dumps
BOWSER_STAR = 9                             # pseudo star index for the fight

# Stars per course (0-based indices). Specials: index 0 (+1 for PSS).
def course_stars(c):
    if c in MAIN:
        return list(range(7))
    if c == "PSS":
        return [0, 1]
    return [0]

# starTimes[] from bingo_const.c: seconds per (course, star 0..5), RTA-ish
STAR_TIMES = {
    "BOB": [66, 127, 29, 69, 46, 19], "WF": [45, 21, 14, 39, 17, 19],
    "JRB": [63, 50, 56, 80, 16, 29], "CCM": [37, 28, 69, 56, 55, 13],
    "BBH": [78, 50, 17, 78, 39, 40], "HMC": [23, 68, 37, 50, 17, 23],
    "LLL": [28, 37, 21, 17, 26, 28], "SSL": [25, 10, 120, 160, 125, 240],
    "DDD": [49, 45, 150, 53, 21, 63], "SL": [14, 19, 8, 20, 46, 25],
    "WDW": [20, 22, 49, 28, 87, 62], "TTM": [35, 52, 35, 11, 29, 16],
    "THI": [35, 30, 71, 28, 41, 63], "TTC": [20, 25, 20, 65, 30, 40],
    "RR": [28, 170, 33, 20, 19, 70],
}

# course_1ups[] (bingo_const.c). Castle grounds: 7 outside + 2 inside.
ONEUPS = {"BOB": 3, "WF": 3, "JRB": 2, "CCM": 5, "BBH": 2, "HMC": 2, "LLL": 8, "SSL": 9,
          "DDD": 1, "SL": 4, "WDW": 4, "TTM": 8, "THI": 7, "TTC": 4, "RR": 9,
          "BitDW": 6, "BitFS": 6, "BitS": 6, "PSS": 2, "CotMC": 2, "TotWC": 0, "VCutM": 4,
          "WMotR": 4, "SA": 1, CASTLE: 9}

# course_floors[] (splatoon denominator), main courses only
FLOORS = {"BOB": 621, "WF": 241, "JRB": 564, "CCM": 1003, "BBH": 693, "HMC": 748,
          "LLL": 865, "SSL": 1297, "DDD": 301, "SL": 472, "WDW": 522, "TTM": 1501,
          "THI": 601, "TTC": 233, "RR": 409}

# Coins per course. APPROXIMATE for mains (community numbers; JRB 104 and
# the specials' caps are from bingo_objective_init.c comments).
COIN_TOTAL = {"BOB": 151, "WF": 152, "JRB": 104, "CCM": 147, "BBH": 151, "HMC": 152,
              "LLL": 133, "SSL": 154, "DDD": 106, "SL": 127, "WDW": 153, "TTM": 139,
              "THI": 184, "TTC": 125, "RR": 146, "BitDW": 80, "BitFS": 80, "BitS": 76,
              "PSS": 80, "CotMC": 47, "TotWC": 63, "VCutM": 27, "WMotR": 56, "SA": 56}

# Master weight tables: (type, weight, uses; -1 = unlimited)
NL = -1
WEIGHTS = {
    "EASY": [("COIN", 12, 1), ("SPLATOON", 8, 1), ("STAR", 12, NL), ("LOSE_MARIO_HAT", 12, 1),
             ("UNIQUE_DEATHS", 8, 1), ("BLJ", 12, 1), ("RACING_STARS", 6, 1),
             ("MULTISTAR", 6, 1), ("STARS_MULTIPLE_LEVELS", 4, 1)],
    "MEDIUM": [("COIN", 12, 1), ("SPLATOON", 8, 2), ("STAR", 20, NL), ("KILL_GOOMBAS", 6, 2),
               ("KILL_BOBOMBS", 6, 2), ("KILL_SPINDRIFTS", 6, 1), ("KILL_MR_IS", 6, 1),
               ("KILL_SCUTTLEBUGS", 6, 1), ("KILL_BULLIES", 6, 1), ("KILL_CHUCKYAS", 6, 1),
               ("KILL_WHOMPS", 6, 1), ("KILL_BOOS", 8, 2), ("KILL_SNUFITS", 6, 1),
               ("HURT_BY_CLAMS", 8, 1), ("KILL_FLY_GUYS", 6, 1), ("KILL_MR_BLIZZARDS", 6, 1),
               ("KILL_SKEETERS", 4, 1), ("KILL_KOOPAS", 4, 1), ("CRUSHED", 6, 1), ("AMPS", 6, 1),
               ("STAR_TIMED", 12, 3), ("STAR_TTC_RANDOM", 8, 2),
               ("STAR_B_BUTTON_CHALLENGE", 3, 3), ("STAR_Z_BUTTON_CHALLENGE", 3, 3),
               ("STAR_DAREDEVIL", 12, 3), ("STAR_REVERSE_JOYSTICK", 8, 2),
               ("STAR_CLICK_GAME", 8, 2), ("RANDOM_RED_COINS", 12, 3), ("RANDOM_STARS", 8, NL),
               ("1UPS_IN_LEVEL", 12, NL), ("STARS_IN_LEVEL", 8, 2), ("LIVES", 8, 1),
               ("UNIQUE_DEATHS", 8, 1), ("SIGNPOST", 12, 2), ("SHOOT_CANNONS", 12, 2),
               ("RED_COIN", 12, 2), ("BLUE_COIN", 12, 2), ("EXCLAMATION_MARK_BOX", 8, 2),
               ("SECRETS_STARS", 8, 2), ("CANNON_STARS", 6, 1), ("RED_COIN_STARS", 6, 1),
               ("RACING_STARS", 4, 1), ("WING_CAP_BOX", 4, 2), ("VANISH_CAP_BOX", 4, 2),
               ("METAL_CAP_BOX", 4, 2), ("DANGEROUS_WALL_KICKS", 12, 1), ("MULTISTAR", 6, 2),
               ("STARS_MULTIPLE_LEVELS", 4, 1), ("BOWSER", 6, 1), ("ROOF_WITHOUT_CANNON", 4, 1)],
    "HARD": [("STAR", 16, NL), ("STAR_TIMED", 12, 1), ("SPLATOON", 8, 2),
             ("STAR_A_BUTTON_CHALLENGE", 12, 2), ("1UPS_IN_LEVEL", 12, 1),
             ("STARS_IN_LEVEL", 16, NL), ("MULTICOIN", 8, NL), ("STAR_REVERSE_JOYSTICK", 16, NL),
             ("STAR_CLICK_GAME", 8, NL), ("STAR_GREEN_DEMON", 12, NL), ("STAR_DAREDEVIL", 8, 3),
             ("DANGEROUS_WALL_KICKS", 12, 1), ("CANNON_STARS", 4, 1), ("RED_COIN_STARS", 4, 1),
             ("POLES", 12, 2), ("SHOOT_CANNONS", 12, 1), ("RED_COIN", 12, 1), ("BLUE_COIN", 12, 1),
             ("AMPS", 6, 1), ("KILL_BULLIES", 6, 1), ("KILL_CHUCKYAS", 6, 1), ("KILL_BOOS", 6, 1),
             ("HURT_BY_CLAMS", 6, 1), ("CRUSHED", 6, 1), ("SIGNPOST", 12, 1), ("MULTISTAR", 6, 1),
             ("STARS_MULTIPLE_LEVELS", 4, 1), ("LIVES", 8, 1)],
    "CENTER": [("COIN", 8, NL), ("KILL_GOOMBAS", 6, 1), ("KILL_BOBOMBS", 6, 1),
               ("MULTICOIN", 12, NL), ("MULTISTAR", 6, 1), ("STARS_MULTIPLE_LEVELS", 6, 1),
               ("POLES", 3, 1), ("SHOOT_CANNONS", 3, 1), ("AMPS", 3, 1), ("BOWSER", 3, 1)],
}

# Sane N range per counter-ish type: union of master's class ranges.
N_RANGE = {
    "KILL_GOOMBAS": (5, 19), "KILL_BOBOMBS": (5, 19), "KILL_SPINDRIFTS": (9, 16),
    "KILL_MR_IS": (3, 6), "KILL_SCUTTLEBUGS": (4, 9), "KILL_BULLIES": (6, 16),
    "KILL_CHUCKYAS": (2, 5), "KILL_WHOMPS": (2, 3), "KILL_BOOS": (4, 12),
    "KILL_SNUFITS": (3, 6), "HURT_BY_CLAMS": (3, 6), "KILL_FLY_GUYS": (3, 6),
    "KILL_MR_BLIZZARDS": (3, 4), "KILL_SKEETERS": (2, 4), "KILL_KOOPAS": (2, 3),
    "CRUSHED": (3, 8), "AMPS": (5, 16), "SIGNPOST": (7, 20), "POLES": (10, 27),
    "SHOOT_CANNONS": (6, 18), "RED_COIN": (12, 29), "BLUE_COIN": (14, 32),
    "EXCLAMATION_MARK_BOX": (6, 14), "WING_CAP_BOX": (5, 9), "VANISH_CAP_BOX": (4, 7),
    "METAL_CAP_BOX": (5, 9), "MULTICOIN": (200, 350), "MULTISTAR": (3, 12),
    "LIVES": (8, 15), "UNIQUE_DEATHS": (4, 6), "BLJ": (3, 6), "LOSE_MARIO_HAT": (3, 4),
    "CANNON_STARS": (1, 4), "RED_COIN_STARS": (3, 7), "STARS_IN_LEVEL": (3, 7),
    "SPLATOON": (10, 55),   # percent of floors
}
# Dial step for large ranges (keeps candidate lists short)
N_STEP = {"MULTICOIN": 25, "SPLATOON": 5}

LINES = ([[r * 5 + c for c in range(5)] for r in range(5)] +
         [[r * 5 + c for r in range(5)] for c in range(5)] +
         [[i * 6 for i in range(5)], [i * 4 + 4 for i in range(5)]])
LINE_NAMES = (["row%d" % r for r in range(5)] + ["col%d" % c for c in range(5)] +
              ["diag\\", "diag/"])

# possibleABC[] (1-based star numbers in the C table)
ABC_STARS = [("BOB", 1), ("BOB", 2), ("WF", 3), ("WF", 4), ("WF", 6), ("JRB", 2), ("HMC", 1),
             ("LLL", 1), ("LLL", 2), ("LLL", 3), ("LLL", 4), ("SSL", 2), ("SL", 2), ("SL", 4),
             ("SL", 5), ("WDW", 1), ("TTM", 3), ("TTM", 4), ("TTM", 6), ("THI", 1), ("THI", 2),
             ("THI", 3), ("THI", 6)]
# clickGameStars[] minus banned: (course, 1-based star) -> (minClicks, maxClicks)
CLICK = {
    ("BOB", 1): (0, 0), ("BOB", 2): (0, 2), ("BOB", 3): (0, 0), ("BOB", 4): (0, 0),
    ("BOB", 5): (0, 0), ("BOB", 6): (0, 0), ("BOB", 7): (3, 5), ("WF", 1): (0, 0),
    ("WF", 2): (0, 1), ("WF", 3): (0, 0), ("WF", 4): (0, 0), ("WF", 5): (0, 0), ("WF", 6): (0, 0),
    ("WF", 7): (0, 0), ("JRB", 1): (1, 1), ("JRB", 2): (1, 4), ("JRB", 3): (2, 4),
    ("JRB", 4): (3, 5), ("JRB", 5): (1, 1), ("JRB", 6): (1, 1), ("CCM", 2): (2, 2),
    ("CCM", 4): (4, 4), ("CCM", 6): (1, 1), ("HMC", 1): (0, 0), ("HMC", 3): (0, 0),
    ("HMC", 4): (5, 5), ("HMC", 5): (0, 0), ("HMC", 6): (0, 1), ("LLL", 1): (0, 0),
    ("LLL", 2): (0, 0), ("LLL", 3): (0, 0), ("LLL", 4): (0, 0), ("LLL", 5): (1, 1),
    ("LLL", 6): (1, 1), ("SSL", 1): (0, 0), ("SSL", 2): (0, 0), ("SSL", 3): (1, 1),
    ("DDD", 1): (1, 1), ("DDD", 2): (1, 1), ("DDD", 3): (2, 2), ("DDD", 4): (1, 1),
    ("DDD", 6): (1, 1), ("SL", 1): (0, 0), ("SL", 2): (1, 1), ("SL", 3): (0, 0), ("SL", 4): (0, 0),
    ("SL", 5): (1, 1), ("SL", 6): (0, 0), ("WDW", 1): (0, 0), ("WDW", 2): (0, 0),
    ("WDW", 3): (0, 0), ("WDW", 4): (0, 0), ("WDW", 5): (0, 0), ("WDW", 6): (0, 0),
    ("WDW", 7): (0, 0), ("TTM", 4): (1, 1), ("TTM", 6): (0, 0), ("THI", 1): (0, 0),
    ("THI", 2): (1, 3), ("THI", 4): (0, 2), ("THI", 6): (2, 4), ("TTC", 1): (0, 2),
    ("TTC", 2): (0, 1), ("TTC", 3): (0, 1), ("TTC", 5): (1, 3), ("TTC", 6): (0, 0),
    ("RR", 1): (3, 3), ("RR", 2): (2, 2), ("RR", 3): (0, 2), ("RR", 4): (1, 2), ("RR", 5): (3, 3),
    ("RR", 6): (3, 3)}
