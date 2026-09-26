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
