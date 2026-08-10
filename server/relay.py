#!/usr/bin/env python3
"""Bingo64 relay server, protocol v2.

A small room server for online bingo (plans/online-bingo.md Part D).
Clients speak a line-based text protocol over TCP; the server groups them
into rooms, relays ghost-Mario state, arbitrates cell claims, and runs the
ready-up flow: the board seed is withheld until every member is ready, then
broadcast with a synchronized countdown.

Protocol v2 (one message per line, space-separated fields):

  client -> server
    J 2 <room> <name> <color> <flags> [seed]
                             join a room. The protocol version (2) comes
                             first; older clients are refused with
                             "E version". color is a palette index 0..7.
                             flags apply only when this join creates the
                             room: bit 0 = lockout, bit 1 = public.
                             The optional seed proposal (or a random one)
                             becomes the room seed.
    G <level> <area> <x> <y> <z> <yaw> <animID> <animFrame>
                             ghost state, sent ~15 times per second
    C <cell>                 claim board cell 0..24
    R <0|1>                  ready toggle (ignored once the room started)
    O <target> <unlock> <maskhex>
                             room bingo options (creator only): bingo
                             target (1/2/3/12), full-game unlock (0/1),
                             disabled-objective bitmask as hex
    L                        list public rooms

  server -> client
    W <id> <lockout> <public> welcome: your player id and room mode.
                             NO seed here: it arrives in S at start.
    N <id> <name> <color> <ready>
                             roster entry (sent for yourself too, and
                             re-sent when someone's state changes)
    R <id> <0|1>             ready change
    S <seed> <delta> <target> <unlock> <maskhex>
                             the race starts: shared seed, room options,
                             and delta = frames (30/s) until GO. Negative
                             delta means the race started -delta frames
                             ago (late joiner). Fires when all ready.
    G <id> <level> <area> <x> <y> <z> <yaw> <animID> <animFrame>
    C <cell> <id>            cell claim accepted (relayed to whole room)
    B <id>                   peer left
    P <name> <members> <started>
                             one public room (response to L), list ends
                             with "P ."
    E <reason>               refused (version, room_full, ...)

Lockout: only the first claim of a cell counts. Co-op (default): every
claim is accepted and shared.

Run:  python3 server/relay.py [--port 64064]
No dependencies beyond the Python standard library.
"""

import argparse
import asyncio
import random
import time

PROTOCOL_VERSION = 2
MAX_ROOM = 15          # ghost slots in the client are limited
COUNTDOWN_FRAMES = 90  # 3 seconds at 30 fps
FPS = 30


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
    def __init__(self, name, seed, lockout, public):
        self.name = name
        self.seed = seed
        self.lockout = lockout
        self.public = public
        self.members = {}   # id -> Client
        self.claims = {}    # cell -> claiming id
        self.next_id = 1
        self.creator_id = 1
        self.started_at = None  # time.monotonic() of GO, once counting down
        # Bingo options, set by the creator via O.
        self.target = 1
        self.unlock = 0
        self.mask = 0

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

    def start_line(self):
        return "S %d %d %d %d %x" % (self.seed, self.start_delta_frames(),
                                     self.target, self.unlock, self.mask)


class Relay:
    def __init__(self):
        self.rooms = {}

    def room_for(self, name, proposed_seed, lockout, public):
        room = self.rooms.get(name)
        if room is None:
            seed = proposed_seed if proposed_seed else random.randrange(1, 999999999)
            room = Room(name, seed, lockout, public)
            self.rooms[name] = room
            print("[%s] room '%s' created, seed %d%s%s"
                  % (ts(), name, seed,
                     " [lockout]" if lockout else "",
                     " [public]" if public else ""))
        return room

    def maybe_start(self, room):
        if room.started or not room.members:
            return
        if all(c.ready for c in room.members.values()):
            room.started_at = time.monotonic() + COUNTDOWN_FRAMES / FPS
            room.broadcast(room.start_line())
            print("[%s] room '%s' starting: %d players, seed %d"
                  % (ts(), room.name, len(room.members), room.seed))

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
                    seed = 0
                    if len(parts) >= 7:
                        seed = clamped_int(parts[6], 0, 999999998)
                    room = self.room_for(parts[2][:31], seed,
                                         bool(flags & 1), bool(flags & 2))
                    if len(room.members) >= MAX_ROOM:
                        client.send("E room_full")
                        break
                    client.room = room
                    client.name = sanitize_name(parts[3])
                    client.color = color
                    client.id = room.next_id
                    room.next_id += 1
                    room.members[client.id] = client
                    client.send("W %d %d %d" % (client.id,
                                                1 if room.lockout else 0,
                                                1 if room.public else 0))
                    # Full roster to the newcomer (including themselves),
                    # newcomer to everyone else.
                    for other in room.members.values():
                        client.send("N %d %s %d %d" % (other.id, other.name,
                                                       other.color,
                                                       1 if other.ready else 0))
                        if other.id != client.id:
                            other.send("N %d %s %d %d"
                                       % (client.id, client.name, client.color, 0))
                    # Replay standing claims so late joiners catch up.
                    for cell, cid in sorted(room.claims.items()):
                        client.send("C %d %d" % (cell, cid))
                    if room.started:
                        client.send(room.start_line())
                    print("[%s] %s joined room '%s' as #%d (%d members)"
                          % (ts(), client.name, room.name, client.id,
                             len(room.members)))
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
                    if cell in room.claims:
                        continue  # already claimed (lockout or duplicate)
                    room.claims[cell] = client.id
                    room.broadcast("C %d %d" % (cell, client.id))
                    print("[%s] room '%s': cell %d claimed by #%d %s"
                          % (ts(), room.name, cell, client.id, client.name))
                elif cmd == "R" and client.room and len(parts) == 2:
                    room = client.room
                    if room.started:
                        continue
                    client.ready = parts[1] == "1"
                    room.broadcast("R %d %d" % (client.id,
                                                1 if client.ready else 0))
                    self.maybe_start(room)
                elif cmd == "O" and client.room and len(parts) == 4:
                    room = client.room
                    if client.id != room.creator_id or room.started:
                        continue
                    room.target = clamped_int(parts[1], 1, 12)
                    room.unlock = clamped_int(parts[2], 0, 1)
                    try:
                        room.mask = int(parts[3], 16) & (2 ** 64 - 1)
                    except ValueError:
                        room.mask = 0
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
            if room and client.id in room.members:
                del room.members[client.id]
                room.broadcast("B %d" % client.id)
                print("[%s] %s left room '%s' (%d members, %d ghost updates relayed)"
                      % (ts(), client.name, room.name, len(room.members),
                         client.ghost_updates))
                if not room.members:
                    del self.rooms[room.name]
                    print("[%s] room '%s' closed" % (ts(), room.name))
                elif not room.started:
                    # The creator role passes on; a departure may leave
                    # everyone else ready.
                    if client.id == room.creator_id:
                        room.creator_id = min(room.members)
                    self.maybe_start(room)
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
