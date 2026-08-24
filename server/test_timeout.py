#!/usr/bin/env python3
"""Unit tests for the timeout tiebreak ranking (relay.timeout_rank).

Run directly: python3 server/test_timeout.py
"""

import sys

from relay import (timeout_rank, timeout_progress, BOARD_LINES,
                   MODE_LINE_1, MODE_LINE_2, MODE_BLACKOUT, MODE_LOCKOUT)

FAILED = 0


def check(cond, what):
    global FAILED
    if cond:
        print("ok    %s" % what)
    else:
        FAILED += 1
        print("FAIL  %s" % what)


def seq(*claims):
    """claims: (frames, id, cell) tuples."""
    return list(claims)


def order(ranked):
    return [cid for cid, _m, _r in ranked]


def test_board_lines():
    check(len(BOARD_LINES) == 12, "12 board lines")
    check(all(len(l) == 5 for l in BOARD_LINES), "every line has 5 cells")
    check((0, 6, 12, 18, 24) in BOARD_LINES, "main diagonal present")
    check((4, 8, 12, 16, 20) in BOARD_LINES, "anti-diagonal present")


def test_lockout_most_squares():
    s = seq((10, 1, 0), (20, 2, 1), (30, 1, 2), (40, 1, 3))
    ranked = timeout_rank(MODE_LOCKOUT, s, [1, 2])
    check(order(ranked) == [1, 2], "lockout: most squares wins")


def test_lockout_tie_first_to_reach():
    # Both end on 2 squares; player 2 got their 2nd square first.
    s = seq((10, 1, 0), (20, 2, 1), (30, 2, 2), (40, 1, 3))
    ranked = timeout_rank(MODE_LOCKOUT, s, [1, 2])
    check(order(ranked) == [2, 1], "lockout tie: first to the count wins")
    check(ranked[0][2] == 30 and ranked[1][2] == 40,
          "lockout tie: reach frames recorded")


def test_lockout_no_claims_rank_last():
    s = seq((10, 2, 0))
    ranked = timeout_rank(MODE_LOCKOUT, s, [1, 2, 3])
    check(order(ranked)[0] == 2, "lockout: only claimer leads")
    check(set(order(ranked)[1:]) == {1, 3}, "lockout: idle players trail")


def test_blackout_most():
    s = seq((10, 1, 0), (20, 2, 1), (30, 2, 2))
    ranked = timeout_rank(MODE_BLACKOUT, s, [1, 2])
    check(order(ranked) == [2, 1], "blackout: most squares wins")


def test_lines_bingos_beat_cells():
    # Player 1: a full row (5 cells = 1 bingo). Player 2: 6 scattered
    # cells, no bingo. Fewer cells, but the bingo wins.
    row = BOARD_LINES[0]
    scattered = [1, 7, 13, 19, 21, 10]  # no complete line
    s = ([(i + 1, 1, c) for i, c in enumerate(row)]
         + [(i + 1, 2, c) for i, c in enumerate(scattered)])
    ranked = timeout_rank(MODE_LINE_2, s, [1, 2])
    check(order(ranked) == [1, 2], "lines: a bingo beats more cells")


def test_lines_longest_row_breaks_bingo_tie():
    # Nobody has a bingo; player 2 has 4 in a row, player 1 has 3.
    s = ([(1, 1, 0), (2, 1, 1), (3, 1, 2)]          # row 0: 3 long
         + [(4, 2, 5), (5, 2, 6), (6, 2, 7), (7, 2, 8)])  # row 1: 4 long
    ranked = timeout_rank(MODE_LINE_1, s, [1, 2])
    check(order(ranked) == [2, 1], "lines: longest partial line breaks tie")


def test_lines_first_to_reach_breaks_exact_tie():
    # Both have 3-in-a-row on different rows; player 1 got there first.
    s = ([(1, 1, 0), (2, 1, 1), (3, 1, 2)]
         + [(4, 2, 5), (5, 2, 6), (6, 2, 7)])
    ranked = timeout_rank(MODE_LINE_1, s, [1, 2])
    check(order(ranked) == [1, 2], "lines: first to the pair wins")
    check(ranked[0][2] == 3 and ranked[1][2] == 6,
          "lines: reach frame is when the pair was attained")


def test_lines_progress_metric():
    # A column counts as "in a row" too (cells 0,5,10,15 = column 0).
    metric, reach = timeout_progress(MODE_LINE_1,
                                     [(1, 0), (2, 5), (3, 10), (4, 15)])
    check(metric == (0, 4) and reach == 4, "columns count as lines")
    # A completed line scores a bingo.
    metric, _ = timeout_progress(MODE_LINE_1,
                                 [(i, c) for i, c in
                                  enumerate((0, 6, 12, 18, 24))])
    check(metric == (1, 5), "diagonal completes a bingo")


def test_zero_progress_is_zero_metric():
    metric, reach = timeout_progress(MODE_LOCKOUT, [])
    check(metric == (0,) and reach == 0, "lockout: empty progress is zero")
    metric, reach = timeout_progress(MODE_LINE_1, [])
    check(metric == (0, 0) and reach == 0, "lines: empty progress is zero")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    print("%s" % ("ALL OK" if FAILED == 0 else "%d FAILED" % FAILED))
    sys.exit(1 if FAILED else 0)
