# Netplay release checklist

Status: created 2026-08-14, right after the first successful end-to-end
race over the live deployment (GCP e2-micro + playit.gg UDP tunnel).
The transport works; this list is everything between "works for us" and
"a stranger can download it, host a room, and nothing surprises them."

Legend: items marked **(missing)** need code, **(untested)** exist but
have never been exercised, **(verify)** are probably fine but should be
confirmed once. Unmarked items are design decisions to make.

## 0. Opinionated priority order

Everything below is the full list; this is the order I would do it in.

1. ~~**Rematch / return to lobby**, and pass the host role after a race
   ends (§1).~~ DONE 2026-08-16 (game-end-flow branch): protocol K.
2. ~~**In-game leave**: a pause-menu LEAVE RACE, and defined
   save-and-quit behavior (§1).~~ DONE 2026-08-16: pause + R → ONLINE
   submenu; save-and-quit turned out to be already unreachable and is
   now deleted (§1).
3. **The live 3+ player test evening** (§7). Do it right after 1–2;
   it will reorder the rest of this list better than any reasoning.
4. **In-game UDP reconnect** (§3). The feature exists and is the one
   most likely to have a real bug nobody has seen.
5. **Whereabouts + privacy toggle** (§2). The headline visible feature
   of the release.
6. **Error-message and version-mismatch UX pass** (§5, §3). Cheap, and
   it is the difference between "it's broken" and "oh, I need to
   update" messages from friends.
7. **Window-drag starvation check on Windows** (§3). Five minutes to
   test; a disconnect-by-title-bar would be embarrassing.
8. **Server ops drills**: VM reboot, playit-as-systemd, billing alert
   (§4). One ssh session.
9. **Release mechanics** (§6): goldens, full sweep, zip, friend-facing
   setup doc, tag.
10. **Ghost polish** (§2): despawn on leave, fade stale ghosts,
    visibility toggle. Nice-to-have; nothing breaks without it.

## 1. Room lifecycle — the bread-and-butter host-server flows

These are the flows every "game server with a host" has. Most gaps in
this section are confirmed by reading `server/relay.py`.

- [x] **Rematch / return to lobby.** DONE 2026-08-16: protocol message
      `K` (host only, started rooms). The server clears seed/claims/
      results/ready, releases held mid-race seats (those players hear
      B), and broadcasts K + a fresh all-unready roster. The client
      resets its race state, warps to the file select via a new -10
      special warp (straight to `level_main_menu_entry_1`, no Mario
      head), and the lobby is live again; rematch = K then X. The host
      triggers it from pause + R → ONLINE → BACK TO LOBBY. Covered by
      `server/test_relay.py` (test_back_to_lobby_and_rematch) and the
      in-game `test/net/rematch_e2e.py`.
- [x] **Host leaves mid-race or post-race.** DONE 2026-08-16: the host
      role passes to the lowest-id *connected* member in every phase
      (`Relay.pass_host`, called from `drop`). A mid-race host dropout
      still gets their seat held, but returns as a regular member.
      Covered by test_host_passes_mid_race.
- [x] **Leave the room from in-game.** DONE 2026-08-16: pause + R →
      ONLINE → LEAVE RACE (everyone; courtesy quit + disconnect, then
      keep playing offline). The ONLINE submenu only appears while
      connected. Not yet play-tested with a real second player watching
      the departure.
- [x] **Exit to title / save-and-quit mid-race.** RESOLVED 2026-08-16:
      it turns out save-and-quit was already unreachable in bingo64 —
      the post-star save menu is never rendered (the replaced
      `render_course_complete_screen` auto-returns CONTINUE_DONT_SAVE),
      so nothing could feed SAVE & QUIT to `handle_save_menu`. The dead
      branch (and its -2 Mario-head warp) is now deleted outright, per
      Matt: mid-race quitting is the back-to-lobby flow's job. EXIT
      COURSE (castle) and SAVE & EXIT (closes the game) still exist.
- [ ] **Joining a room whose race already started (untested in-game).**
      The protocol supports it (S with negative delta, replayed claims)
      and the relay-level test covers it, but nobody has done it from
      the real game against the live server. Also decide the UX: does a
      late joiner race (their timer already behind) or spectate?
- [ ] **DNF / abandoned race (missing).** If nobody finishes and
      everyone drifts away, mid-race drops hold seats ("may return")
      and the room object lingers until every connection times out.
      Verify rooms are actually garbage-collected in that case
      (30s UDP silence → drop → `del self.rooms[...]` when empty), and
      add a hard room-age cap as a backstop.
- [ ] **Finisher experience (design).** After you finish: you're
      frozen-timer, watching standings. Can you keep playing casually?
      Can you leave without killing your place? Say what places others
      got in an obvious way (board screen shows standings — is it
      enough?). Partly covered 2026-08-16: in-game toast notices
      (bingo_notice, top-left HUD stack, 5s) announce peers' finishes
      with places, joins/leaves/disconnects/reconnects, host changes,
      and "the host ended the race" (which also takes over the lobby
      status line for 5s so the reason for the warp-out is visible).
      Still open: whether the standings themselves need more.
- [ ] **Ties (verify).** Two `F` messages in the same server tick —
      places are assigned by arrival order on the server clock. Decide
      whether frame-identical times should share a place; at minimum
      confirm nothing breaks.
- [ ] **Ready-state edge cases (verify).** Un-ready during countdown;
      host force-start with 0 ready; joiner arriving during the 3s
      countdown (relay refuses `J` on started rooms only — countdown
      rooms are started; confirm the refusal message is friendly).
- [ ] **Room capacity UX (verify).** MAX_ROOM=15 relay-side; the
      "E full" refusal is tested at transport level — check the lobby
      shows a readable message, not a generic error.
- [ ] **Name/color collisions (verify).** Two players named "mario"
      with the same color: everything keys off ids, but can you tell
      the ghosts/standings apart? Consider a join-time dedup suffix.
- [ ] **Change name/color/mode while connected (missing, known).** No
      protocol message; today you must disconnect, edit, reconnect.
      Fine for MVP? At minimum gray the fields out while connected so
      edits aren't silently ignored.

## 2. Presence and whereabouts

- [ ] **Show where each player is (missing — Matt's ask).** Roster
      and/or board screen shows each racer's current course ("BobOmb
      Battlefield", "Castle", star select...). Data is nearly free: a
      small addition to the ghost payload or a low-rate reliable
      message on level change. Show on the bingo board screen next to
      the color chips.
- [ ] **Privacy toggle for whereabouts.** A lobby/options setting,
      OFF = the client doesn't broadcast location (not merely hides it
      locally). Decide the default (suggest: ON, it's a race).
- [ ] **Ghost visibility toggle (design).** Some players find ghosts
      distracting or spoilery (they reveal routes). Local setting:
      ghosts ON / OFF / same-course-only. Purely client-side.
- [ ] **Ghost lifecycle on leave (verify).** When a player quits or
      times out mid-race, their ghost should despawn on `B`/`D`, not
      freeze mid-air. Check the in-game handling of both messages.
- [ ] **Ghost staleness (verify).** A player idling on the pause menu
      still broadcasts (network pumps every frame — confirmed in
      `produce_one_frame`). A player whose connection silently died
      shows a frozen ghost for up to 30s until the server drops them.
      Acceptable? Consider fading a ghost that hasn't updated in ~2s.
- [ ] **Live standings on the board screen (verify).** Claims, chips,
      finishers render — confirmed in E2E. Check the first-time-user
      readability: whose color is whose (roster shows it, but in-game?).

## 3. Failure, recovery, and hostile networks

- [ ] **In-game UDP reconnect (untested).** Reconnect logic is tested
      at the relay level (new conn_id + token) and the TCP-era in-game
      E2E passed, but the full in-game UDP path — kill wifi mid-race,
      watch the HUD indicator, get the replayed state — has not been
      run. Test with the live tunnel.
- [ ] **PC sleep / laptop lid (untested).** Sleep >30s = server drops
      you (seat held if racing); on wake the client should hit its own
      15s silence timeout and auto-redial. Verify it actually does and
      doesn't wedge in a half-open state.
- [ ] **Server restart mid-race (missing, decide).** relay.py holds all
      state in memory; a restart orphans every client into redial loops
      against an empty server ("unknown token"). MVP-acceptable, but:
      confirm the client surfaces a sane error rather than retrying
      forever, and note room-state persistence as a post-MVP item.
- [ ] **Window drag on Windows (verify — likely real).** Classic Win32
      modal move/size loops block the message pump; if DXGI's loop
      stops calling `produce_one_frame` during a drag, a >15s title-bar
      hold disconnects you and any drag stutters your ghost. Test;
      if real, either pump network from a timer during modal loops or
      raise the client timeout.
- [ ] **Two clients behind one NAT (untested).** Same house, two PCs,
      one router, both through the tunnel. conn_id identity should make
      this trivial — confirm once.
- [ ] **Long-session soak (untested).** One lobby idling for an hour+
      through playit: NAT rebinds mid-session (address migration is
      handled and unit-tested — confirm live), tunnel hiccups, GCP
      egress. Leave two clients connected overnight; check journalctl.
- [ ] **Garbage and abuse (verify).** Internet background radiation
      hits the tunnel port: bad magic drops silently (good). Check a
      malformed-but-valid-magic flood can't grow state (UDP_MAX_CONNS
      caps conns; confirm unknown-conn packets don't allocate). Room
      creation spam: 512-conn cap is the only limit — fine for MVP,
      note rate limiting as post-MVP.
- [ ] **Version-mismatch UX (verify).** Old exe vs new server → `E
      version`. Confirm the lobby message tells the user to update, not
      just "refused".

## 4. Server operations

- [ ] **playit agent as a service (verify).** The relay runs under
      systemd (`bingo64-relay.service`, Restart=always) — confirm the
      playit agent is also a systemd unit (not a tmux/ssh session) and
      both come back after a `sudo reboot` test.
- [ ] **VM reboot drill (untested).** Reboot the e2-micro; confirm
      relay + tunnel return with no hands and the tunnel address is
      unchanged.
- [ ] **Log hygiene (verify).** journalctl works (python3 -u). Confirm
      journald caps disk usage (default is fine, but MemoryMax=256M on
      the service + unbounded logs on a 30GB disk — check
      SystemMaxUse).
- [ ] **Uptime visibility (design, cheap).** A once-a-day "N rooms, N
      conns served" summary line in the log, or an HTTP /status on a
      second port, so Matt can tell it's alive without ssh.
- [ ] **Free-tier guardrails (verify).** e2-micro free tier: confirm
      the VM is in a free region with pd-standard disk and no static
      IP reserved; set a billing alert at $1 so surprises are loud.

## 5. Client UX polish

- [ ] **Seed entry in the lobby (design agreed, missing).** Matt wants
      the seed as a lobby text field, nixing the numpad page on PC
      (N64 keeps the numpad). Pending design review.
- [ ] **Public-room browser (missing, post-MVP?).** Protocol supports
      L/P; no UI. Decide if v1 ships private-rooms-only (fine — friends
      share a room name out of band).
- [ ] **Error-message pass (verify).** Read every path that sets
      NET_STATE_ERROR (refusals, timeouts, dial failures, "network
      backlog") on the actual screen. No raw internal strings; every
      message suggests an action.
- [ ] **Connection indicator (design).** Racing HUD shows reconnecting;
      consider a subtle connected/ping indicator in the lobby so "is it
      working?" never needs asking. Ping display is free (measure ack
      RTT on keepalives).
- [ ] **Typeable-vs-renderable characters (verify).** Server addresses
      and names come from real keyboards; HUD font is limited (0x9E
      space; 0xFE segfaults EXTERNAL_DATA builds). Fuzz the text fields
      with punctuation/unicode; ensure unrenderable chars are filtered
      at input time, not at draw time.
- [ ] **Default server address (decide).** Ship the release with
      `net_server` defaulting to the public tunnel address so friends
      only type a name and room? Or keep it blank to avoid strangers
      landing on Matt's relay? (Room names are effectively passwords —
      probably fine to default it.)

## 6. Release mechanics

- [ ] **Re-bless emu goldens.** The master merge (0d8083e4c) brought in
      the new skull icon; `test/emu/golden/board_screen.png` kept the
      alo-port version and may not match a fresh render. Run the emu
      tests, re-bless if needed.
- [ ] **Full test sweep on the merged master.** relay tests
      (`python3 server/test_relay.py -v`), test/host, emu tests, plus a
      clean Linux and Windows build from scratch.
- [ ] **Release zip with UDP build.** bingo64-extract flow re-verified
      (Win + Linux) with the new exe; version bump; confirm protocol
      v4 exe refuses v3 servers cleanly and vice versa.
- [ ] **Friend-facing setup doc.** One page: download, extract with
      your ROM, either edit `%APPDATA%\sm64ex\sm64config.txt` (game
      closed!) or use a provided `race.bat` with
      `--net-server udp:... --net-room ...`. Include the "config is
      rewritten on exit" gotcha.
- [ ] **Tag a GitHub release** once the sweep is green.

## 7. Live test matrix (humans required)

The single most valuable pre-release activity: one evening, 3+ real
players (not two — every E2E so far is effectively 2-player), live
tunnel, deliberately doing rude things:

- [ ] 3–4 players, full race to completion, all five modes at least
      once (line 1/2/3, blackout co-op, lockout adjudication).
- [ ] Mixed transports in one room (one player on `udp:`, one TCP
      direct to the VM IP) — relay-tested, never in-game.
- [ ] Someone alt-F4s mid-race. Someone save-and-quits. Someone's
      wifi drops and comes back. Someone joins late. The host quits.
- [ ] Someone drags the window around; someone tabs out for 5 minutes;
      someone pauses for the whole race.
- [ ] Race twice in a row (this is what forces the rematch flow).
- [ ] One player far away geographically, if available — tunnel adds a
      hop; feel the ghost latency.
- [ ] Afterward: read the server journal end to end for anything
      surprising (unknown messages, refusals, zombie conns).
