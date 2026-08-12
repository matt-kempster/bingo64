#!/usr/bin/env python3
"""Bingo64 relay server, protocol v3.

A small room server for online bingo (plans/online-bingo.md Part D).
Clients speak a line-based text protocol over TCP; the server groups them
into rooms, relays ghost-Mario state, arbitrates cell claims, runs the
ready-up flow (the board seed is withheld until every member is ready,
then broadcast with a synchronized countdown), assigns finish placements
with authoritative times, decides lockout races, and lets a dropped
client reconnect mid-race with a token.

Game modes (shared enum with the client, see src/game/bingo.h):
  0/1/2  line race   first to 1/2/3 bingos; claims are per-player, the
                     client announces its finish with F and the server
                     assigns the placement + time
  3      blackout    co-op: the first claim of a cell completes it for
                     the whole room
  4      lockout     exclusive claims; the server decides the winner
                     (uncatchable lead, or board exhausted)

Protocol v3 (one message per line, space-separated fields):

  client -> server
    J 4 <room> <name> <color> <flags> [token]
                             join a room. The protocol version (4) comes
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
    O <mode> <unlock> <maskhex> <seed>
                             room settings (host only, before the start):
                             game mode 0..4, full-game unlock,
                             disabled-objective bitmask as hex, and the
                             seed proposal (0 = random at start).
    X                        the host starts the race (allowed even if
                             not everyone is ready)
    F                        local win condition met (line modes; the
                             server ignores it in lockout)
    L                        list public rooms

  server -> client
    W <id> <public> <mode> <token>
                             welcome: player id, room mode and the
                             reconnect token. NO seed here: it arrives in
                             S at start.
    N <id> <name> <color> <ready> <connected>
                             roster entry (sent for yourself too, and
                             re-sent when someone's state changes)
    R <id> <0|1>             ready change
    O <mode>                 the host changed the room's game mode
    H <id>                   who the host is (on join, and when the host
                             role passes to someone else)
    S <seed> <delta> <mode> <unlock> <maskhex>
                             the race starts: shared seed, room options,
                             and delta = frames (30/s) until GO. Negative
                             delta means the race started -delta frames
                             ago (late joiner / reconnect).
    G <id> <level> <area> <x> <y> <z> <yaw> <animID> <animFrame>
    C <cell> <id>            cell claim accepted (relayed to whole room)
    F <id> <place> <frames>  a racer finished (authoritative time)
    V <id> <frames>          lockout decided: winner and time
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
import random
import time

PROTOCOL_VERSION = 4
MAX_ROOM = 15          # ghost slots in the client are limited
COUNTDOWN_FRAMES = 90  # 3 seconds at 30 fps
FPS = 30

MODE_BLACKOUT = 3
MODE_LOCKOUT = 4


class Client:
    __slots__ = ("reader", "writer", "id", "name", "color", "room", "ready",
                 "alive", "ghost_updates")

    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self.id = None
        self.name = "?"
        self.color = 0
        self.room = None
        self.ready = False
        self.alive = True
        self.ghost_updates = 0

    def send(self, line):
        if not self.alive:
            return
        try:
            self.writer.write((line + "\n").encode())
        except Exception:
            self.alive = False


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
        # Results.
        self.finishers = []     # (id, place, frames)
        self.winner = None      # (id, frames), lockout

    @property
    def started(self):
        return self.started_at is not None

    def broadcast(self, line, skip=None):
        for c in self.members.values():
            if c.id != skip:
                c.send(line)

    def start_delta_frames(self):
        """Frames until GO (positive: still counting down)."""
        return int(round((self.started_at - time.monotonic()) * FPS))

    def elapsed_frames(self):
        """Frames of racing since GO (clamped to 0 during the countdown)."""
        return max(0, -self.start_delta_frames())

    def start_line(self):
        return "S %d %d %d %d %x" % (self.seed, self.start_delta_frames(),
                                     self.mode, self.unlock, self.mask)

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


class Relay:
    def __init__(self):
        self.rooms = {}

    def room_for(self, name, public):
        room = self.rooms.get(name)
        if room is None:
            room = Room(name, public)
            self.rooms[name] = room
            print("[%s] room '%s' created%s"
                  % (ts(), name, " [public]" if public else ""))
        return room

    def start_race(self, room):
        """The host pressed START RACE (ready marks are advisory only)."""
        if room.started or not room.members:
            return
        room.seed = (room.seed_proposal
                     if room.seed_proposal else random.randrange(1, 999999999))
        room.started_at = time.monotonic() + COUNTDOWN_FRAMES / FPS
        room.broadcast(room.start_line())
        print("[%s] room '%s' starting: %d players, seed %d, mode %d"
              % (ts(), room.name, len(room.members), room.seed, room.mode))

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

    def adjudicate_lockout(self, room):
        """Decide a lockout race: uncatchable lead or exhausted board."""
        if room.winner is not None or not room.started:
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

    def resume_session(self, room, client, token, name, color):
        """Find the member this token was issued to; -1 if unknown."""
        if not token:
            return -1
        for id_, tok in room.tokens.items():
            if tok == token:
                old = room.members.pop(id_, None)
                if old is not None:
                    # A half-dead socket still holds the seat; replace it.
                    old.alive = False
                    try:
                        old.writer.close()
                    except Exception:
                        pass
                room.disconnected.pop(id_, None)
                client.id = id_
                client.name = name
                client.color = color
                room.members[id_] = client
                return id_
        return -1

    async def handle(self, reader, writer):
        client = Client(reader, writer)
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
                cmd = parts[0]
                if cmd == "J" and client.room is None and len(parts) >= 3:
                    if parts[1] != str(PROTOCOL_VERSION):
                        client.send("E version %d" % PROTOCOL_VERSION)
                        break
                    if len(parts) < 6:
                        client.send("E bad_join")
                        break
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
                            break
                        client.name = name
                        client.color = color
                        client.id = room.next_id
                        room.next_id += 1
                        room.members[client.id] = client
                        room.tokens[client.id] = random.randrange(1, 2 ** 32)
                    client.room = room
                    client.send("W %d %d %d %d"
                                % (client.id, 1 if room.public else 0,
                                   room.mode, room.tokens[client.id]))
                    self.send_room_snapshot(room, client)
                    # Announce the (re)arrival to everyone else.
                    room.broadcast(room.roster_line(client.id, client.name,
                                                    client.color, client.ready,
                                                    True), skip=client.id)
                    print("[%s] %s %s room '%s' as #%d (%d members)"
                          % (ts(), client.name,
                             "rejoined" if resumed >= 0 else "joined",
                             room.name, client.id, len(room.members)))
                elif cmd == "G" and client.room and len(parts) == 9:
                    client.ghost_updates += 1
                    client.room.broadcast(
                        "G %d %s" % (client.id, " ".join(parts[1:])),
                        skip=client.id)
                elif cmd == "C" and client.room and len(parts) == 2:
                    cell = clamped_int(parts[1], -1, 24)
                    if cell < 0:
                        continue
                    room = client.room
                    ids = room.claims.setdefault(cell, [])
                    if room.mode in (MODE_BLACKOUT, MODE_LOCKOUT):
                        if ids:
                            continue  # exclusive/co-op: first claim only
                    elif client.id in ids:
                        continue  # race: once per player
                    ids.append(client.id)
                    room.broadcast("C %d %d" % (cell, client.id))
                    print("[%s] room '%s': cell %d claimed by #%d %s"
                          % (ts(), room.name, cell, client.id, client.name))
                    if room.mode == MODE_LOCKOUT:
                        self.adjudicate_lockout(room)
                elif cmd == "F" and client.room:
                    room = client.room
                    if (not room.started or room.mode == MODE_LOCKOUT
                            or any(f[0] == client.id for f in room.finishers)):
                        continue
                    place = len(room.finishers) + 1
                    frames = room.elapsed_frames()
                    room.finishers.append((client.id, place, frames))
                    room.broadcast("F %d %d %d" % (client.id, place, frames))
                    print("[%s] room '%s': #%d %s finished, place %d (%d frames)"
                          % (ts(), room.name, client.id, client.name,
                             place, frames))
                elif cmd == "R" and client.room and len(parts) == 2:
                    room = client.room
                    if room.started:
                        continue
                    client.ready = parts[1] == "1"
                    room.broadcast("R %d %d" % (client.id,
                                                1 if client.ready else 0))
                elif cmd == "O" and client.room and len(parts) >= 4:
                    room = client.room
                    if client.id != room.creator_id or room.started:
                        continue
                    room.mode = clamped_int(parts[1], 0, 4)
                    room.unlock = clamped_int(parts[2], 0, 1)
                    try:
                        room.mask = int(parts[3], 16) & (2 ** 64 - 1)
                    except ValueError:
                        room.mask = 0
                    if len(parts) >= 5:
                        room.seed_proposal = clamped_int(parts[4], 0, 999999998)
                    room.broadcast("O %d" % room.mode, skip=client.id)
                elif cmd == "X" and client.room:
                    room = client.room
                    if client.id != room.creator_id:
                        continue
                    self.start_race(room)
                elif cmd == "L":
                    for room in self.rooms.values():
                        if room.public:
                            client.send("P %s %d %d"
                                        % (room.name, len(room.members),
                                           1 if room.started else 0))
                    client.send("P .")
        except (ConnectionResetError, asyncio.IncompleteReadError):
            pass
        finally:
            client.alive = False
            room = client.room
            if room and room.members.get(client.id) is client:
                del room.members[client.id]
                if room.started and room.members:
                    # Mid-race drop: hold the seat for a reconnect.
                    room.disconnected[client.id] = (client.name, client.color)
                    room.broadcast("D %d" % client.id)
                    print("[%s] %s disconnected from room '%s' "
                          "(%d members, may return)"
                          % (ts(), client.name, room.name, len(room.members)))
                else:
                    room.broadcast("B %d" % client.id)
                    room.tokens.pop(client.id, None)
                    print("[%s] %s left room '%s' (%d members, %d ghost updates relayed)"
                          % (ts(), client.name, room.name, len(room.members),
                             client.ghost_updates))
                if not room.members:
                    del self.rooms[room.name]
                    print("[%s] room '%s' closed" % (ts(), room.name))
                elif not room.started:
                    # The host role passes on.
                    if client.id == room.creator_id:
                        room.creator_id = min(room.members)
                        room.broadcast("H %d" % room.creator_id)
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
    args = ap.parse_args()

    relay = Relay()
    server = await asyncio.start_server(relay.handle, args.host, args.port)
    print("[%s] bingo64 relay listening on %s:%d (protocol v%d)"
          % (ts(), args.host, args.port, PROTOCOL_VERSION))
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
