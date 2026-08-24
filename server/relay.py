#!/usr/bin/env python3
"""Bingo64 relay server, protocol v7.

A small room server for online bingo (plans/online-bingo.md Part D).
Clients speak a line-based text protocol; the server groups them into
rooms, relays ghost-Mario state, arbitrates cell claims, runs the
ready-up flow (the board seed is withheld until every member is ready,
then broadcast with a synchronized countdown), assigns finish placements
with authoritative times, decides lockout races, and lets a dropped
client reconnect mid-race with a token.

The same line protocol rides one of two transports, both on one port:

  TCP   one message per newline-terminated line, as always. Right for
        LAN, direct IPs and localhost testing.
  UDP   for tunnels that only carry UDP (playit.gg free tier). Each
        datagram is a 10-byte header + one line (no newline). Control
        messages ride a reliable in-order channel; ghost state rides an
        unreliable-sequenced channel where stale packets are dropped
        instead of retransmitted (better than TCP for position data:
        one lost packet cannot stall the ones behind it).

UDP header (big-endian):
    u8  magic 0xB6
    u8  type        0 ghost / 1 reliable / 2 ack / 3 keepalive
    u32 conn_id     random nonzero id chosen by the client; identifies
                    the session in both directions (never the address:
                    NATs and proxies remap addresses mid-session, and a
                    datagram with a known conn_id from a new address
                    simply updates the session's address)
    u32 seq         ghost: sender's ghost counter, receiver drops stale
                    reliable: 1-based message sequence
                    ack: highest reliable seq received in order
                    keepalive: 0

Reliability: the sender keeps unacked reliable messages queued and
resends them every 250 ms until the peer's cumulative ack covers them;
the receiver processes seq == expected+1 only (dups and gap-jumpers are
dropped) and answers every reliable/keepalive datagram with an ack.
Clients send a keepalive every few seconds so NAT mappings stay open;
a session silent for 30 s is treated as a disconnect (mid-race that
holds the seat for a token reconnect, exactly like a TCP drop).

Game modes (shared enum with the client, see src/game/bingo.h):
  0/1/2  line race   first to 1/2/3 bingos; claims are per-player, the
                     client announces its finish with F and the server
                     assigns the placement + time
  3      blackout    co-op: the first claim of a cell completes it for
                     the whole room
  4      lockout     exclusive claims; the server decides the winner
                     (uncatchable lead, or board exhausted)

Line protocol (space-separated fields):

  client -> server
    J 7 <room> <name> <color> <flags> [token]
                             join a room. The protocol version (7) comes
                             first; older clients are refused with
                             "E version". color is a palette index 0..7.
                             flags apply only when this join creates the
                             room: bit 0 = public. A nonzero token
                             resumes the session it was issued for: same
                             player id, and the server replays roster,
                             claims, start and results.
    G <level> <area> <x> <y> <z> <yaw> <animID> <animFrame>
                             ghost state, sent ~15 times per second
    C <cell>                 claim board cell 0..24
    R <0|1>                  ready toggle (a roster signal only; the race
                             starts when the host sends X)
    O <mode> <unlock> <maskhex> <seed> <claimvis> <where> <timeout>
                             room settings (host only, before the start):
                             game mode 0..4, full-game unlock,
                             disabled-objective bitmask as hex, the
                             seed proposal (0 = random at start), the
                             claim-visibility tier 0..3 (open/progress/
                             bingos/hidden; invalid tier+mode pairs are
                             coerced, see coerce_claimvis), whether
                             player whereabouts are shared (0/1), and
                             the race timeout in minutes (0 = off,
                             else one of TIMEOUT_CHOICES).
    X                        the host starts the race (allowed even if
                             not everyone is ready)
    K                        the host ends the race: back to the lobby.
                             Clears seed/claims/results/ready, releases
                             seats held for mid-race reconnects (those
                             players hear B like any lobby-phase drop),
                             and broadcasts K + a fresh roster. A
                             rematch is simply K followed by X.
    F                        local win condition met (line modes; the
                             server ignores it in lockout)
    L                        list public rooms
    ?                        occupancy probe (no join needed): one
                             "? rooms=<n> members=<m> racing=<r>" reply,
                             then the connection closes. deploy/update.sh
                             asks this before restarting the service.
    Q                        leaving on purpose (UDP: a courtesy so the
                             room hears B/D now instead of after the
                             silence timeout; TCP just closes)

  server -> client
    W <id> <public> <mode> <token> <claimvis> <where> <timeout>
                             welcome: player id, room mode/visibility
                             settings and the reconnect token. NO seed
                             here: it arrives in S at start.
    N <id> <name> <color> <ready> <connected>
                             roster entry (sent for yourself too, and
                             re-sent when someone's state changes)
    R <id> <0|1>             ready change
    O <mode> <claimvis> <where> <timeout>
                             the host changed the room's settings
    H <id>                   who the host is (on join, and when the host
                             role passes to someone else — the role now
                             passes in every phase, not just the lobby,
                             so a started room is never left hostless)
    K                        the room went back to the lobby: forget the
                             race (seed, claims, results, ready marks)
                             and return to the lobby screen. Followed by
                             a full roster re-send with everyone unready.
    S <seed> <delta> <mode> <unlock> <maskhex> <claimvis> <where> <timeout>
                             the race starts: shared seed, room options,
                             and delta = frames (30/s) until GO. Negative
                             delta means the race started -delta frames
                             ago (late joiner / reconnect).
    G <id> <level> <area> <x> <y> <z> <yaw> <animID> <animFrame>
    C <cell> <id>            cell claim accepted (relayed to whole room)
    F <id> <place> <frames>  a racer finished (authoritative time)
    V <id> <frames>          lockout decided: winner and time
    T <id> <frames> <tiebreak>
                             the room's timeout expired: the race is
                             over. id = the winner (0 = dead-even draw),
                             tiebreak = 1 when the standings comparison
                             decided it (see timeout_rank), 0 when a
                             regular finisher already held first place.
                             Line modes: F placements for everyone still
                             racing are broadcast immediately before.
    D <id>                   peer disconnected mid-race (may return)
    B <id>                   peer left for good
    P <name> <members> <started>
                             one public room (response to L), list ends
                             with "P ."
    E <reason>               refused (version, room_full, ...)

Run:  python3 server/relay.py [--port 64064]
No dependencies beyond the Python standard library.
"""

import argparse
import asyncio
import functools
import json
import os
import random
import struct
import time

# Under systemd, stdout is a pipe and Python block-buffers it: journal
# lines otherwise sit unflushed for days on a quiet relay.
print = functools.partial(print, flush=True)

# Bumped on every wire change while the protocol is under active
# development; mismatched peers are refused ("E version"), not served.
PROTOCOL_VERSION = 7
MAX_ROOM = 15          # ghost slots in the client are limited
COUNTDOWN_FRAMES = 90  # 3 seconds at 30 fps
FPS = 30

MODE_LINE_1 = 0
MODE_LINE_2 = 1
MODE_LINE_3 = 2
MODE_BLACKOUT = 3
MODE_LOCKOUT = 4
MODE_LINE_MODES = (MODE_LINE_1, MODE_LINE_2, MODE_LINE_3)

# Claim-visibility tiers (v6 room setting, mirrored in network.h).
CLAIMVIS_OPEN = 0      # chips + toasts + counts: everything (default)
CLAIMVIS_PROGRESS = 1  # counts only, not which squares
CLAIMVIS_BINGOS = 2    # bingo milestones only (2/3-bingo modes)
CLAIMVIS_HIDDEN = 3    # nothing until finishes

# Race timeouts the menu offers, in minutes (v7 room setting; 0 = off).
TIMEOUT_CHOICES = (0, 5, 15, 30, 45, 60)


def coerce_claimvis(vis, mode):
    """Invalid tier/mode pairs are unrepresentable: lockout is ABOUT the
    squares and blackout is co-op on a shared board (both always open),
    and the bingo-count tier only means something in the 2/3-bingo modes
    (mirrored in the C client)."""
    vis = max(CLAIMVIS_OPEN, min(CLAIMVIS_HIDDEN, vis))
    if mode in (MODE_BLACKOUT, MODE_LOCKOUT):
        return CLAIMVIS_OPEN
    if vis == CLAIMVIS_BINGOS and mode not in (MODE_LINE_2, MODE_LINE_3):
        return CLAIMVIS_PROGRESS
    return vis


# The 12 winnable lines of the 5x5 board, as cell indices. The board is
# stored column-major on the client, but lines are symmetric so only the
# set of lines matters here.
BOARD_LINES = tuple(
    [tuple(range(r * 5, r * 5 + 5)) for r in range(5)]
    + [tuple(range(c, 25, 5)) for c in range(5)]
    + [tuple(range(0, 25, 6)), tuple(range(4, 21, 4))]
)


def timeout_progress(mode, my_claims):
    """One player's standing when the clock runs out. my_claims is their
    ordered [(frames, cell)]. Returns (metric, reach_frame): metric is a
    tuple comparing DESCENDING (bigger = better); reach_frame is when the
    final metric value was first attained, comparing ASCENDING ("first to
    get that number" breaks the tie). Lockout and blackout rank by squares
    held; line modes by bingos, then the longest partial line."""
    if mode not in MODE_LINE_MODES:
        n = len(my_claims)
        return (n,), (my_claims[-1][0] if n else 0)
    cells = set()
    best = (0, 0)
    reach = 0
    for frames, cell in my_claims:
        cells.add(cell)
        bingos = sum(1 for line in BOARD_LINES
                     if all(c in cells for c in line))
        longest = max(sum(1 for c in line if c in cells)
                      for line in BOARD_LINES)
        cur = (bingos, longest)
        if cur > best:
            best = cur
            reach = frames
    return best, reach


def timeout_rank(mode, claim_seq, racer_ids):
    """Rank racers for a timeout verdict. claim_seq is the room's ordered
    [(frames, id, cell)]. Returns [(id, metric, reach_frame)] best first;
    an exact tie (same metric, and claims are server-sequenced so equal
    reach frames mean equal order) falls back to the lower id, which can
    only happen between players with no progress at all."""
    per = {i: [] for i in racer_ids}
    for frames, cid, cell in claim_seq:
        if cid in per:
            per[cid].append((frames, cell))
    ranked = []
    for cid in racer_ids:
        metric, reach = timeout_progress(mode, per[cid])
        ranked.append((cid, metric, reach))
    ranked.sort(key=lambda r: (tuple(-v for v in r[1]), r[2], r[0]))
    return ranked

# UDP transport constants (mirrored in src/pc/network/network.c).
UDP_MAGIC = 0xB6
UDP_GHOST = 0
UDP_RELIABLE = 1
UDP_ACK = 2
UDP_KEEPALIVE = 3
UDP_HEADER = struct.Struct("!BBII")
UDP_MAX_PAYLOAD = 512
UDP_RESEND_S = 0.25         # retransmit cadence for unacked messages
UDP_RESEND_WINDOW = 8       # head-of-queue messages per retransmit tick
UDP_TIMEOUT_S = 30.0        # silence after which a session is dropped
UDP_CLOSE_LINGER_S = 5.0    # refused sessions stick around to ack the E
UDP_MAX_CONNS = 512         # new-session cap (random-conn-id flood guard)


class Client:
    """A room member; subclasses supply the transport."""
    __slots__ = ("id", "name", "color", "room", "ready", "alive",
                 "ghost_updates")

    def __init__(self):
        self.id = None
        self.name = "?"
        self.color = 0
        self.room = None
        self.ready = False
        self.alive = True
        self.ghost_updates = 0

    def send(self, line, lossy=False):
        raise NotImplementedError

    def kick(self):
        """Sever the transport (the seat was taken over by a reconnect)."""
        raise NotImplementedError


class TcpClient(Client):
    __slots__ = ("writer",)

    def __init__(self, writer):
        super().__init__()
        self.writer = writer

    def send(self, line, lossy=False):
        if not self.alive:
            return
        try:
            self.writer.write((line + "\n").encode())
        except Exception:
            self.alive = False

    def kick(self):
        self.alive = False
        try:
            self.writer.close()
        except Exception:
            pass


class UdpConn:
    """One UDP session: reliability state plus the room-member Client."""
    __slots__ = ("cid", "addr", "endpoint", "client", "out", "next_seq",
                 "in_expected", "ghost_in", "ghost_out", "last_recv",
                 "close_at")

    def __init__(self, cid, addr, endpoint):
        self.cid = cid
        self.addr = addr
        self.endpoint = endpoint
        self.client = UdpClient(self)
        self.out = []           # [(seq, datagram bytes)] awaiting ack
        self.next_seq = 1
        self.in_expected = 0    # highest reliable seq processed in order
        self.ghost_in = 0
        self.ghost_out = 0
        self.last_recv = time.monotonic()
        self.close_at = None    # linger deadline once we decided to close

    def _dgram(self, type_, seq, payload=b""):
        return UDP_HEADER.pack(UDP_MAGIC, type_, self.cid, seq) + payload

    def send_line(self, line, lossy):
        if lossy:
            self.ghost_out += 1
            self.endpoint.transmit(self._dgram(UDP_GHOST, self.ghost_out,
                                               line.encode()), self.addr)
            return
        seq = self.next_seq
        self.next_seq += 1
        dgram = self._dgram(UDP_RELIABLE, seq, line.encode())
        self.out.append((seq, dgram))
        self.endpoint.transmit(dgram, self.addr)

    def send_ack(self):
        self.endpoint.transmit(self._dgram(UDP_ACK, self.in_expected),
                               self.addr)

    def on_ack(self, seq):
        while self.out and self.out[0][0] <= seq:
            self.out.pop(0)

    def begin_close(self):
        """Stop the session, lingering briefly so a queued E gets acked."""
        if self.close_at is None:
            self.close_at = time.monotonic() + UDP_CLOSE_LINGER_S


class UdpClient(Client):
    __slots__ = ("conn",)

    def __init__(self, conn):
        super().__init__()
        self.conn = conn

    def send(self, line, lossy=False):
        if self.alive:
            self.conn.send_line(line, lossy)

    def kick(self):
        self.alive = False
        self.conn.endpoint.forget(self.conn)


class UdpEndpoint(asyncio.DatagramProtocol):
    def __init__(self, relay):
        self.relay = relay
        self.transport = None
        self.conns = {}  # conn_id -> UdpConn

    def connection_made(self, transport):
        self.transport = transport

    def transmit(self, dgram, addr):
        if self.transport is not None:
            self.transport.sendto(dgram, addr)

    def forget(self, conn):
        self.conns.pop(conn.cid, None)

    def datagram_received(self, data, addr):
        if len(data) < UDP_HEADER.size or len(data) > UDP_HEADER.size + UDP_MAX_PAYLOAD:
            return
        magic, type_, cid, seq = UDP_HEADER.unpack_from(data)
        if magic != UDP_MAGIC or cid == 0:
            return
        conn = self.conns.get(cid)
        if conn is None:
            if len(self.conns) >= UDP_MAX_CONNS:
                return
            conn = UdpConn(cid, addr, self)
            self.conns[cid] = conn
        conn.addr = addr  # NAT rebind / proxy flow change: follow the sender
        conn.last_recv = time.monotonic()
        if conn.close_at is not None:
            if type_ == UDP_ACK:
                conn.on_ack(seq)
            return  # closing: accept acks for the pending E, nothing else
        payload = data[UDP_HEADER.size:]
        if type_ == UDP_ACK:
            conn.on_ack(seq)
        elif type_ == UDP_KEEPALIVE:
            conn.send_ack()
        elif type_ == UDP_GHOST:
            if seq > conn.ghost_in:
                conn.ghost_in = seq
                self._process(conn, payload)
        elif type_ == UDP_RELIABLE:
            if seq == conn.in_expected + 1:
                conn.in_expected = seq
                conn.send_ack()
                self._process(conn, payload)
            else:
                conn.send_ack()  # dup or gap: re-state what we have

    def _process(self, conn, payload):
        parts = payload.decode(errors="replace").split()
        if not parts:
            return
        if not self.relay.process_line(conn.client, parts):
            conn.begin_close()

    async def tick_loop(self):
        while True:
            await asyncio.sleep(UDP_RESEND_S)
            now = time.monotonic()
            for conn in list(self.conns.values()):
                if conn.close_at is not None and (now >= conn.close_at
                                                  or not conn.out):
                    self.forget(conn)
                    continue
                if now - conn.last_recv > UDP_TIMEOUT_S:
                    conn.client.alive = False
                    self.forget(conn)
                    self.relay.drop(conn.client, "timed out")
                    continue
                for _, dgram in conn.out[:UDP_RESEND_WINDOW]:
                    self.transmit(dgram, conn.addr)


class Room:
    def __init__(self, name, public):
        self.name = name
        self.seed = 0           # decided at start (host proposal or random)
        self.seed_proposal = 0  # 0 = random
        self.public = public
        self.members = {}       # id -> Client (connected)
        self.disconnected = {}  # id -> (name, color): mid-race dropouts
        self.tokens = {}        # id -> reconnect token
        self.claims = {}        # cell -> [claiming ids, in order]
        self.next_id = 1
        self.creator_id = 1
        self.started_at = None  # time.monotonic() of GO, once counting down
        # Bingo options, set by the creator via O.
        self.mode = 0
        self.unlock = 0
        self.mask = 0
        self.claimvis = CLAIMVIS_OPEN
        self.whereabouts = 1
        self.timeout_min = 0    # race timeout in minutes (0 = off)
        # Results.
        self.finishers = []     # (id, place, frames)
        self.winner = None      # (id, frames), lockout
        self.claim_seq = []     # (frames, id, cell) in acceptance order
        self.timeout_result = None  # (winner_id, frames, tiebreak) once
                                    # the race ended on the timeout

    @property
    def started(self):
        return self.started_at is not None

    def broadcast(self, line, skip=None, lossy=False):
        for c in self.members.values():
            if c.id != skip:
                c.send(line, lossy)

    def start_delta_frames(self):
        """Frames until GO (positive: still counting down)."""
        return int(round((self.started_at - time.monotonic()) * FPS))

    def elapsed_frames(self):
        """Frames of racing since GO (clamped to 0 during the countdown)."""
        return max(0, -self.start_delta_frames())

    def start_line(self):
        return "S %d %d %d %d %x %d %d %d" % (self.seed,
                                              self.start_delta_frames(),
                                              self.mode, self.unlock,
                                              self.mask, self.claimvis,
                                              self.whereabouts,
                                              self.timeout_min)

    def roster_line(self, id_, name, color, ready, connected):
        return "N %d %s %d %d %d" % (id_, name, color,
                                     1 if ready else 0, 1 if connected else 0)

    def racer_ids(self):
        return set(self.members) | set(self.disconnected)

    def claim_counts(self):
        counts = {}
        for ids in self.claims.values():
            for cid in ids:
                counts[cid] = counts.get(cid, 0) + 1
        return counts


STATS_INTERVAL_S = 24 * 3600  # one journal line a day proves liveness

# History must never be what fills the disk: below this much free space
# the match log pauses itself (the relay keeps running regardless).
MATCHLOG_MIN_FREE = 1 << 30


class MatchLog:
    """Append-only JSONL history of semantic room events, one file per
    month (<dir>/2026-08.jsonl). Only interpreted events are recorded —
    never the ghost stream — so a whole race is a few KB and "every
    match ever" stays a rounding error on the disk. Fail-open: any I/O
    trouble or a nearly-full disk pauses logging, never the relay."""

    def __init__(self, dirpath):
        self.dir = dirpath or None  # None/"" = disabled
        self.paused = None          # reason string while not writing
        self.written = 0            # events since the last daily summary

    def event(self, ev, room=None, **fields):
        if self.dir is None:
            return
        rec = {"t": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
               "ev": ev}
        if room is not None:
            rec["room"] = room
        rec.update(fields)
        try:
            os.makedirs(self.dir, exist_ok=True)
            st = os.statvfs(self.dir)
            if st.f_bavail * st.f_frsize < MATCHLOG_MIN_FREE:
                self._pause("disk nearly full")
                return
            path = os.path.join(
                self.dir, time.strftime("%Y-%m", time.gmtime()) + ".jsonl")
            with open(path, "a", encoding="utf-8") as f:
                f.write(json.dumps(rec, separators=(",", ":"),
                                   sort_keys=True) + "\n")
        except OSError as exc:
            self._pause(str(exc))
            return
        if self.paused is not None:
            print("[%s] matchlog: resumed" % ts())
            self.paused = None
        self.written += 1

    def _pause(self, why):
        if self.paused != why:  # one journal line per distinct trouble
            print("[%s] matchlog: paused (%s)" % (ts(), why))
            self.paused = why


class Relay:
    def __init__(self, matchlog=None):
        self.rooms = {}
        self.stat_joins = 0    # joins since the last daily summary
        self.stat_races = 0    # races started since the last summary
        self.log = matchlog if matchlog is not None else MatchLog(None)

    async def stats_loop(self):
        """A daily heartbeat in the journal, so "is it alive?" never
        needs an ssh session: journalctl -u bingo64-relay | tail."""
        while True:
            await asyncio.sleep(STATS_INTERVAL_S)
            print("[%s] daily: %d rooms open, %d joins, %d races started,"
                  " %d history events"
                  % (ts(), len(self.rooms), self.stat_joins,
                     self.stat_races, self.log.written))
            self.stat_joins = 0
            self.stat_races = 0
            self.log.written = 0

    def room_for(self, name, public):
        room = self.rooms.get(name)
        if room is None:
            room = Room(name, public)
            self.rooms[name] = room
            print("[%s] room '%s' created%s"
                  % (ts(), name, " [public]" if public else ""))
            self.log.event("room_created", room=name, public=bool(public))
        return room

    def start_race(self, room):
        """The host pressed START RACE (ready marks are advisory only)."""
        if room.started or not room.members:
            return
        room.seed = (room.seed_proposal
                     if room.seed_proposal else random.randrange(1, 999999999))
        room.started_at = time.monotonic() + COUNTDOWN_FRAMES / FPS
        self.stat_races += 1
        room.broadcast(room.start_line())
        print("[%s] room '%s' starting: %d players, seed %d, mode %d"
              % (ts(), room.name, len(room.members), room.seed, room.mode))
        self.log.event("start", room=room.name, seed=room.seed,
                       mode=room.mode, unlock=room.unlock,
                       mask="%x" % room.mask,
                       players=[{"id": c.id, "name": c.name,
                                 "color": c.color}
                                for c in room.members.values()])

    def reset_room(self, room):
        """The host ended the race: back to the lobby, roster intact."""
        if not room.started:
            return
        # Seats held for mid-race reconnects are now plain lobby-phase
        # departures: the race they could have rejoined no longer exists.
        for id_ in sorted(room.disconnected):
            room.broadcast("B %d" % id_)
            room.tokens.pop(id_, None)
        room.disconnected.clear()
        room.started_at = None
        room.seed = 0
        room.claims.clear()
        room.finishers = []
        room.winner = None
        room.claim_seq = []
        room.timeout_result = None
        room.broadcast("K")
        for c in room.members.values():
            c.ready = False
            room.broadcast(room.roster_line(c.id, c.name, c.color,
                                            False, True))
        print("[%s] room '%s' back to lobby (%d members)"
              % (ts(), room.name, len(room.members)))
        self.log.event("lobby_reset", room=room.name)

    def pass_host(self, room, leaving_id):
        """Keep the host role on a connected member in every phase."""
        if leaving_id == room.creator_id and room.members:
            room.creator_id = min(room.members)
            room.broadcast("H %d" % room.creator_id)
            self.log.event("host", room=room.name, id=room.creator_id)

    def send_room_snapshot(self, room, client):
        """Roster, claims and race status, for a joiner or reconnector."""
        client.send("H %d" % room.creator_id)
        for other in room.members.values():
            client.send(room.roster_line(other.id, other.name, other.color,
                                         other.ready, True))
        for id_, (name, color) in sorted(room.disconnected.items()):
            client.send(room.roster_line(id_, name, color, False, False))
        for cell, ids in sorted(room.claims.items()):
            for cid in ids:
                client.send("C %d %d" % (cell, cid))
        if room.started:
            client.send(room.start_line())
        for id_, place, frames in room.finishers:
            client.send("F %d %d %d" % (id_, place, frames))
        if room.winner is not None:
            client.send("V %d %d" % room.winner)
        if room.timeout_result is not None:
            client.send("T %d %d %d" % room.timeout_result)

    def adjudicate_lockout(self, room):
        """Decide a lockout race: uncatchable lead or exhausted board."""
        if (room.winner is not None or not room.started
                or room.timeout_result is not None):
            return
        counts = room.claim_counts()
        if not counts:
            return
        for id_ in room.racer_ids():
            counts.setdefault(id_, 0)
        ranked = sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))
        leader_id, leader_count = ranked[0]
        second_count = ranked[1][1] if len(ranked) > 1 else 0
        remaining = 25 - sum(counts.values())
        # With 2 racers this reduces to first-to-13; solo it is 13 of 25.
        if leader_count > second_count + remaining or remaining == 0:
            room.winner = (leader_id, room.elapsed_frames())
            room.broadcast("V %d %d" % room.winner)
            print("[%s] room '%s': lockout decided, #%d wins with %d squares"
                  % (ts(), room.name, leader_id, leader_count))
            self.log.event("lockout_win", room=room.name, id=leader_id,
                           squares=leader_count, frames=room.winner[1])


    def resolve_timeout(self, room):
        """The room's timeout expired: end the race and break the ties.
        Line modes: everyone still racing gets a place (after the real
        finishers) by timeout_rank; the winner is the existing 1st-place
        finisher if there is one, else the standings leader. Lockout and
        blackout: the standings leader wins outright. A room with zero
        progress ends in a draw (winner 0)."""
        if (room.timeout_result is not None or not room.started
                or room.timeout_min <= 0 or room.winner is not None):
            return
        limit = room.timeout_min * 60 * FPS
        if room.elapsed_frames() < limit:
            return
        ids = sorted(room.racer_ids())
        ranked = timeout_rank(room.mode, room.claim_seq, ids)
        tiebreak = 1
        winner = 0
        if room.mode in MODE_LINE_MODES:
            pre = list(room.finishers)
            finished = {f[0] for f in pre}
            place = len(pre)
            for cid, _metric, _reach in ranked:
                if cid in finished:
                    continue
                place += 1
                room.finishers.append((cid, place, limit))
                room.broadcast("F %d %d %d" % (cid, place, limit))
            for cid, pl, _frames in pre:
                if pl == 1:
                    winner = cid       # a regular finish already won it
                    tiebreak = 0
            if winner == 0 and ranked and any(v for v in ranked[0][1]):
                winner = ranked[0][0]
        else:
            if ranked and any(v for v in ranked[0][1]):
                winner = ranked[0][0]
        room.timeout_result = (winner, limit, tiebreak)
        room.broadcast("T %d %d %d" % room.timeout_result)
        print("[%s] room '%s': timed out after %dm, winner #%d%s"
              % (ts(), room.name, room.timeout_min, winner,
                 " (tiebreak)" if winner and tiebreak else ""))
        self.log.event("timeout", room=room.name, winner=winner,
                       tiebreak=bool(winner and tiebreak),
                       minutes=room.timeout_min,
                       standings=[{"id": cid, "metric": list(metric),
                                   "reach": reach}
                                  for cid, metric, reach in ranked])

    async def timeout_loop(self):
        """Once a second: end any race whose timeout has expired."""
        while True:
            await asyncio.sleep(1)
            for room in list(self.rooms.values()):
                self.resolve_timeout(room)

    def resume_session(self, room, client, token, name, color):
        """Find the member this token was issued to; -1 if unknown."""
        if not token:
            return -1
        for id_, tok in room.tokens.items():
            if tok == token:
                old = room.members.pop(id_, None)
                if old is not None:
                    # A half-dead transport still holds the seat; replace it.
                    old.kick()
                room.disconnected.pop(id_, None)
                client.id = id_
                client.name = name
                client.color = color
                room.members[id_] = client
                return id_
        return -1

    def process_line(self, client, parts):
        """Handle one message. Returns False when the connection is done
        (refused): the caller severs the transport, without drop()."""
        cmd = parts[0]
        if cmd == "?":
            # Occupancy probe, no join needed: deploys ask this before
            # restarting (a restart drops everyone in a race). One line,
            # then the connection is done.
            occupied = [r for r in self.rooms.values() if r.members]
            client.send("? rooms=%d members=%d racing=%d"
                        % (len(occupied),
                           sum(len(r.members) for r in occupied),
                           sum(1 for r in occupied if r.started)))
            return False
        if cmd == "J" and client.room is None and len(parts) >= 3:
            if parts[1] != str(PROTOCOL_VERSION):
                client.send("E version %d" % PROTOCOL_VERSION)
                return False
            if len(parts) < 6:
                client.send("E bad_join")
                return False
            color = clamped_int(parts[4], 0, 7)
            flags = clamped_int(parts[5], 0, 3)
            token = 0
            if len(parts) >= 7:
                token = clamped_int(parts[6], 0, 2 ** 32 - 1)
            room = self.room_for(parts[2][:31], bool(flags & 1))
            name = sanitize_name(parts[3])
            resumed = self.resume_session(room, client, token, name, color)
            if resumed < 0:
                if len(room.members) >= MAX_ROOM:
                    client.send("E room_full")
                    return False
                # Two "mario"s in one room: ghosts and standings key off
                # ids, but humans key off names — suffix the newcomer.
                taken = ({c.name for c in room.members.values()}
                         | {nm for nm, _ in room.disconnected.values()})
                suffix = 2
                while name in taken:
                    name = "%.13s%d" % (sanitize_name(parts[3]), suffix)
                    suffix += 1
                client.name = name
                client.color = color
                client.id = room.next_id
                room.next_id += 1
                room.members[client.id] = client
                room.tokens[client.id] = random.randrange(1, 2 ** 32)
            client.room = room
            self.stat_joins += 1
            client.send("W %d %d %d %d %d %d %d"
                        % (client.id, 1 if room.public else 0,
                           room.mode, room.tokens[client.id],
                           room.claimvis, room.whereabouts,
                           room.timeout_min))
            self.send_room_snapshot(room, client)
            # Announce the (re)arrival to everyone else.
            room.broadcast(room.roster_line(client.id, client.name,
                                            client.color, client.ready,
                                            True), skip=client.id)
            print("[%s] %s %s room '%s' as #%d (%d members)"
                  % (ts(), client.name,
                     "rejoined" if resumed >= 0 else "joined",
                     room.name, client.id, len(room.members)))
            self.log.event("rejoin" if resumed >= 0 else "join",
                           room=room.name, id=client.id, name=client.name,
                           color=client.color, members=len(room.members))
        elif cmd == "G" and client.room and len(parts) == 9:
            client.ghost_updates += 1
            client.room.broadcast(
                "G %d %s" % (client.id, " ".join(parts[1:])),
                skip=client.id, lossy=True)
        elif cmd == "C" and client.room and len(parts) == 2:
            cell = clamped_int(parts[1], -1, 24)
            if cell < 0:
                return True
            room = client.room
            if room.timeout_result is not None:
                return True  # the race ended on the timeout
            ids = room.claims.setdefault(cell, [])
            if room.mode in (MODE_BLACKOUT, MODE_LOCKOUT):
                if ids:
                    return True  # exclusive/co-op: first claim only
            elif client.id in ids:
                return True  # race: once per player
            ids.append(client.id)
            if room.started:
                room.claim_seq.append((room.elapsed_frames(), client.id,
                                       cell))
            room.broadcast("C %d %d" % (cell, client.id))
            print("[%s] room '%s': cell %d claimed by #%d %s"
                  % (ts(), room.name, cell, client.id, client.name))
            self.log.event("claim", room=room.name, id=client.id,
                           cell=cell,
                           frames=room.elapsed_frames() if room.started
                           else -1)
            if room.mode == MODE_LOCKOUT:
                self.adjudicate_lockout(room)
        elif cmd == "F" and client.room:
            room = client.room
            if (not room.started or room.mode == MODE_LOCKOUT
                    or room.timeout_result is not None
                    or any(f[0] == client.id for f in room.finishers)):
                return True
            place = len(room.finishers) + 1
            frames = room.elapsed_frames()
            room.finishers.append((client.id, place, frames))
            room.broadcast("F %d %d %d" % (client.id, place, frames))
            print("[%s] room '%s': #%d %s finished, place %d (%d frames)"
                  % (ts(), room.name, client.id, client.name,
                     place, frames))
            self.log.event("finish", room=room.name, id=client.id,
                           place=place, frames=frames)
        elif cmd == "R" and client.room and len(parts) == 2:
            room = client.room
            if room.started:
                return True
            client.ready = parts[1] == "1"
            room.broadcast("R %d %d" % (client.id,
                                        1 if client.ready else 0))
        elif cmd == "O" and client.room and len(parts) >= 4:
            room = client.room
            if client.id != room.creator_id or room.started:
                return True
            room.mode = clamped_int(parts[1], 0, 4)
            room.unlock = clamped_int(parts[2], 0, 1)
            try:
                room.mask = int(parts[3], 16) & (2 ** 64 - 1)
            except ValueError:
                room.mask = 0
            if len(parts) >= 5:
                room.seed_proposal = clamped_int(parts[4], 0, 999999998)
            if len(parts) >= 7:
                room.claimvis = clamped_int(parts[5], 0, 3)
                room.whereabouts = clamped_int(parts[6], 0, 1)
            # The mode may have changed out from under the tier.
            if len(parts) >= 8:
                t = clamped_int(parts[7], 0, 60)
                room.timeout_min = t if t in TIMEOUT_CHOICES else 0
            room.claimvis = coerce_claimvis(room.claimvis, room.mode)
            room.broadcast("O %d %d %d %d" % (room.mode, room.claimvis,
                                              room.whereabouts,
                                              room.timeout_min),
                           skip=client.id)
        elif cmd == "X" and client.room:
            room = client.room
            if client.id != room.creator_id:
                return True
            self.start_race(room)
        elif cmd == "K" and client.room:
            room = client.room
            if client.id != room.creator_id:
                return True
            self.reset_room(room)
        elif cmd == "Q":
            self.drop(client, "left")
            return False
        elif cmd == "L":
            for room in self.rooms.values():
                if room.public:
                    client.send("P %s %d %d"
                                % (room.name, len(room.members),
                                   1 if room.started else 0))
            client.send("P .")
        return True

    def drop(self, client, why="left"):
        """The transport died: free the seat (or hold it mid-race)."""
        client.alive = False
        room = client.room
        if not room or room.members.get(client.id) is not client:
            return
        del room.members[client.id]
        if room.started and room.members:
            # Mid-race drop: hold the seat for a reconnect.
            room.disconnected[client.id] = (client.name, client.color)
            room.broadcast("D %d" % client.id)
            print("[%s] %s %s room '%s' (%d members, may return)"
                  % (ts(), client.name, why, room.name, len(room.members)))
            self.log.event("drop", room=room.name, id=client.id,
                           name=client.name, held=True)
        else:
            room.broadcast("B %d" % client.id)
            room.tokens.pop(client.id, None)
            print("[%s] %s left room '%s' (%d members, %d ghost updates relayed)"
                  % (ts(), client.name, room.name, len(room.members),
                     client.ghost_updates))
            self.log.event("leave", room=room.name, id=client.id,
                           name=client.name)
        if not room.members:
            del self.rooms[room.name]
            print("[%s] room '%s' closed" % (ts(), room.name))
            self.log.event("room_closed", room=room.name)
        else:
            # The host role passes on to a connected member in every
            # phase (a mid-race dropout may return, but not as host:
            # someone present must be able to end or restart the race).
            self.pass_host(room, client.id)

    async def handle_tcp(self, reader, writer):
        client = TcpClient(writer)
        try:
            while True:
                line = await reader.readline()
                if not line:
                    break
                if len(line) > 512:
                    break  # no legitimate message is this long
                parts = line.decode(errors="replace").split()
                if not parts:
                    continue
                if not self.process_line(client, parts):
                    break
        except (ConnectionResetError, asyncio.IncompleteReadError):
            pass
        finally:
            self.drop(client, "disconnected from")
            writer.close()


def clamped_int(text, lo, hi):
    try:
        return max(lo, min(hi, int(text)))
    except ValueError:
        return lo


def sanitize_name(name):
    return "".join(ch for ch in name[:15] if ch.isprintable()) or "?"


def ts():
    return time.strftime("%H:%M:%S")


async def main():
    ap = argparse.ArgumentParser(description="Bingo64 relay server")
    ap.add_argument("--port", type=int, default=64064)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--matchlog",
                    default=os.path.join(
                        os.path.dirname(os.path.abspath(__file__)),
                        "matchlog"),
                    help="dir for the JSONL match history"
                         " (empty string disables)")
    args = ap.parse_args()

    relay = Relay(matchlog=MatchLog(args.matchlog))
    server = await asyncio.start_server(relay.handle_tcp, args.host, args.port)
    loop = asyncio.get_running_loop()
    _, endpoint = await loop.create_datagram_endpoint(
        lambda: UdpEndpoint(relay), local_addr=(args.host, args.port))
    asyncio.ensure_future(endpoint.tick_loop())
    asyncio.ensure_future(relay.stats_loop())
    asyncio.ensure_future(relay.timeout_loop())
    print("[%s] bingo64 relay listening on %s:%d tcp+udp (protocol v%d)"
          % (ts(), args.host, args.port, PROTOCOL_VERSION))
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
