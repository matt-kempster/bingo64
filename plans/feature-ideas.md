# Feature ideas backlog (2026-09-26)

Matt: "we'll end up wanting all of them." Not started unless noted.
Rough size: S = an evening, M = a few days, L = a real project.

## Small
1. **Daily board (S).** Seed from the UTC date; everyone plays the same
   board that day, solo or online. No server needed. Later: a daily
   leaderboard mined from the match log (plans/match-history.md).
2. **Hide board until GO (S).** Online players can study the board during
   the countdown today. Bingosync etiquette defaults this ON.
3. **Race recap screen (S).** After a race, a timeline of who claimed what
   when, from claims the client already holds. Story + screenshot bait.

## Medium
4. **Exploration mode / fog of war (M).** Only the center cell is visible
   at the start; claiming a cell reveals its neighbours; win = a line as
   usual. Reuses the Call and Response live-cell mask (every client
   derives visibility from the claims). Works solo, online, and lockout.
5. **Spectator join (M).** Join a room as a watcher: board, ghosts,
   claims, no Mario. Matters once races get streamed/cast.
6. **Draft / veto (M).** Before GO each player bans a row or column; the
   race plays on what's left.

## Large
7. **Live tiles** (plans/synergy-objectives.md). Cells whose target
   changes mid-race ("most X"); online only, forks seed generation.
8. **Teams (L).** 2v2 lockout: claims belong to a team colour.

## Deferred on purpose
- **Per-tile time estimates** (C&R endurance clock, fast+slow call
  pairing, balanced boards). Matt 2026-09-26: log durably now, datamine
  later. Logging: plans/match-history.md. Model: test/cost_model/.

## Featured courses ("forced overlap") -- PARKED, Matt skeptical (2026-09-26)

Idea: each board picks 4-5 featured courses and course-pinned tiles favour
them (~60%), spread across different lines so one visit advances several
lines instead of handing out a free bingo. Parked: Matt worries a steering
generator reads as weird or biased ("why is it always LLL"), while pure
random is beyond suspicion. Family caps (a ceiling per tile family) were
done instead, since they change the rule mix without steering where you go.

Family caps were tried the same day (at most 4 of each non-star family):
averages barely moved and the histograms piled up at exactly 4, which read
as weird. Dropped. Decision: change nothing until playtests; match history
already logs boards, so tile skips can be mined later. If more stars are
wanted, scale the specific-star weights instead (smooth, no spike).
