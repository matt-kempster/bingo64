#!/usr/bin/env python3
"""Bingo64 relay server.

A small room server for online bingo (plans/online-bingo.md Part D).
Clients speak a line-based text protocol over TCP; the server groups them
into rooms, hands every member the room's shared seed, relays ghost-Mario
state, and arbitrates cell claims.

Protocol (one message per line, space-separated fields):

  client -> server
    J <room> <name> [team] [seed]
                             join a room; team is an integer (0 = no team,
                             for future group battles); the first member's
                             seed proposal (or a random one) becomes the
                             room seed
    G <level> <area> <x> <y> <z> <yaw> <animID> <animFrame>
                             ghost state, sent ~30 times per second
    C <cell>                 claim board cell 0..24

  server -> client
    W <id> <seed> <lockout>  welcome: your player id and the room seed
    N <id> <name> <team>     a peer joined (also sent for existing peers)
    G <id> <level> <area> <x> <y> <z> <yaw> <animID> <animFrame>
    C <cell> <id>            cell claim accepted (relayed to whole room,
                             including the claimer)
    B <id>                   peer left

In co-op mode (default) every claim is accepted and shared: the room plays
one board together. With --lockout only the first claim of a cell counts;
later claims of the same cell are silently dropped, so each cell has one
owner (the <id> in the C message).

Run:  python3 server/relay.py [--port 64064] [--lockout]
No dependencies beyond the Python standard library.
"""

import argparse
import asyncio
import random
import time

MAX_ROOM = 15  # ghost slots in the client are limited


class Client:
    __slots__ = ("reader", "writer", "id", "name", "team", "room", "alive", "ghost_updates")

    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self.id = None
        self.name = "?"
        self.team = 0
        self.room = None
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
    def __init__(self, name, seed, lockout):
        self.name = name
        self.seed = seed
        self.lockout = lockout
        self.members = {}  # id -> Client
        self.claims = {}   # cell -> claiming id
        self.next_id = 1

    def broadcast(self, line, skip=None):
        for c in self.members.values():
            if c.id != skip:
                c.send(line)


class Relay:
    def __init__(self, lockout):
        self.rooms = {}
        self.lockout = lockout

    def room_for(self, name, proposed_seed):
        room = self.rooms.get(name)
        if room is None:
            seed = proposed_seed if proposed_seed else random.randrange(1, 999999999)
            room = Room(name, seed, self.lockout)
            self.rooms[name] = room
            print("[%s] room '%s' created, seed %d" % (ts(), name, seed))
        return room

    async def handle(self, reader, writer):
        client = Client(reader, writer)
        peer = writer.get_extra_info("peername")
        try:
            while True:
                line = await reader.readline()
                if not line:
                    break
                parts = line.decode(errors="replace").split()
                if not parts:
                    continue
                cmd = parts[0]
                if cmd == "J" and client.room is None and len(parts) >= 3:
                    team = 0
                    seed = 0
                    if len(parts) >= 4:
                        try:
                            team = int(parts[3])
                        except ValueError:
                            team = 0
                    if len(parts) >= 5:
                        try:
                            seed = int(parts[4]) % 999999999
                        except ValueError:
                            seed = 0
                    room = self.room_for(parts[1], seed)
                    if len(room.members) >= MAX_ROOM:
                        client.send("E room_full")
                        break
                    client.room = room
                    client.name = parts[2][:15]
                    client.team = team
                    client.id = room.next_id
                    room.next_id += 1
                    room.members[client.id] = client
                    client.send("W %d %d %d" % (client.id, room.seed, 1 if room.lockout else 0))
                    # Replay standing claims so late joiners catch up.
                    for cell, cid in sorted(room.claims.items()):
                        client.send("C %d %d" % (cell, cid))
                    # Introduce everyone to everyone.
                    for other in room.members.values():
                        if other.id != client.id:
                            other.send("N %d %s %d" % (client.id, client.name, client.team))
                            client.send("N %d %s %d" % (other.id, other.name, other.team))
                    print("[%s] %s joined room '%s' as #%d (%d members)"
                          % (ts(), client.name, room.name, client.id, len(room.members)))
                elif cmd == "G" and client.room and len(parts) == 9:
                    client.ghost_updates += 1
                    client.room.broadcast(
                        "G %d %s" % (client.id, " ".join(parts[1:])), skip=client.id)
                elif cmd == "C" and client.room and len(parts) == 2:
                    try:
                        cell = int(parts[1])
                    except ValueError:
                        continue
                    if not 0 <= cell <= 24:
                        continue
                    room = client.room
                    if cell in room.claims:
                        continue  # already claimed (lockout or duplicate)
                    room.claims[cell] = client.id
                    room.broadcast("C %d %d" % (cell, client.id))
                    print("[%s] room '%s': cell %d claimed by #%d %s"
                          % (ts(), room.name, cell, client.id, client.name))
        except (ConnectionResetError, asyncio.IncompleteReadError):
            pass
        finally:
            client.alive = False
            room = client.room
            if room and client.id in room.members:
                del room.members[client.id]
                room.broadcast("B %d" % client.id)
                print("[%s] %s left room '%s' (%d members, %d ghost updates relayed)"
                      % (ts(), client.name, room.name, len(room.members), client.ghost_updates))
                if not room.members:
                    del self.rooms[room.name]
                    print("[%s] room '%s' closed" % (ts(), room.name))
            writer.close()


def ts():
    return time.strftime("%H:%M:%S")


async def main():
    ap = argparse.ArgumentParser(description="Bingo64 relay server")
    ap.add_argument("--port", type=int, default=64064)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--lockout", action="store_true",
                    help="first claim of a cell wins; later claims are dropped")
    args = ap.parse_args()

    relay = Relay(args.lockout)
    server = await asyncio.start_server(relay.handle, args.host, args.port)
    print("[%s] bingo64 relay listening on %s:%d (%s mode)"
          % (ts(), args.host, args.port, "lockout" if args.lockout else "co-op"))
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
