#!/usr/bin/env python3
"""Exercises server/relay.py with scripted clients.

Covers: room creation, shared seed, peer introductions with teams, ghost
state relay (A->B and B->A), claim arbitration (first claim wins, duplicates
dropped), late-joiner claim replay, and leave notification.

Run: python3 test/net/protocol_test.py
"""

import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
RELAY = os.path.join(HERE, "..", "..", "server", "relay.py")
PORT = 64123

failures = []


def check(cond, message):
    if not cond:
        failures.append(message)
        print("  check failed: %s" % message)


class FakeClient:
    def __init__(self, name, team=0):
        self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=5)
        self.sock.settimeout(0.5)
        self.buf = b""
        self.name = name
        self.team = team
        self.id = None
        self.seed = None
        self.lockout = None
        self.pending = []

    def send(self, line):
        self.sock.sendall((line + "\n").encode())

    def join(self, room):
        self.send("J %s %s %d" % (room, self.name, self.team))
        w = self.expect("W")
        self.id, self.seed, self.lockout = int(w[1]), int(w[2]), int(w[3])

    def lines(self, wait=0.5):
        """All messages available within `wait` seconds (pending ones first)."""
        end = time.time() + wait
        out = self.pending
        self.pending = []
        while time.time() < end:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                break
            if not data:
                break
            self.buf += data
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                out.append(line.decode().split())
        return out

    def expect(self, kind, wait=2.0):
        """Next message of `kind`; anything else read meanwhile is kept."""
        end = time.time() + wait
        while time.time() < end:
            for i, parts in enumerate(self.pending):
                if parts and parts[0] == kind:
                    return self.pending.pop(i)
            got = self.lines(0.2)
            self.pending.extend(got)
        raise AssertionError("%s: no %r message arrived" % (self.name, kind))

    def close(self):
        self.sock.close()


def main():
    server = subprocess.Popen(
        [sys.executable, "-u", RELAY, "--port", str(PORT)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(0.8)
    try:
        a = FakeClient("alpha", team=1)
        a.join("proto")
        check(a.id == 1, "alpha id %s != 1" % a.id)
        check(1 <= a.seed <= 999999999, "bad seed %s" % a.seed)

        b = FakeClient("bravo", team=2)
        b.join("proto")
        check(b.seed == a.seed, "seeds differ: %s vs %s" % (a.seed, b.seed))

        # Introductions both ways, with teams.
        n = a.expect("N")
        check(n[1:] == [str(b.id), "bravo", "2"], "alpha got intro %s" % n)
        n = b.expect("N")
        check(n[1:] == [str(a.id), "alpha", "1"], "bravo got intro %s" % n)

        # Ghost relay in both directions; the sender must not get an echo.
        a.send("G 9 1 100.0 200.0 300.0 4096 72 10")
        g = b.expect("G")
        check(g[1:] == [str(a.id), "9", "1", "100.0", "200.0", "300.0", "4096", "72", "10"],
              "bravo got ghost %s" % g)
        b.send("G 9 1 -50.5 0.0 25.0 -4096 12 3")
        g = a.expect("G")
        check(g[1] == str(b.id) and g[4] == "-50.5", "alpha got ghost %s" % g)
        check(not [p for p in a.lines(0.3) if p and p[0] == "G" and p[1] == str(a.id)],
              "alpha received an echo of its own ghost state")

        # Claims: first wins, both hear it, duplicate is dropped.
        a.send("C 7")
        ca = a.expect("C")
        cb = b.expect("C")
        check(ca == ["C", "7", str(a.id)], "alpha claim ack %s" % ca)
        check(cb == ["C", "7", str(a.id)], "bravo claim relay %s" % cb)
        b.send("C 7")  # duplicate: dropped silently
        check(not [p for p in a.lines(0.5) if p and p[0] == "C"],
              "duplicate claim was re-broadcast")

        # Late joiner gets standing claims replayed.
        c = FakeClient("charlie")
        c.join("proto")
        check(c.seed == a.seed, "late joiner seed differs")
        replay = c.expect("C")
        check(replay == ["C", "7", str(a.id)], "late joiner claim replay %s" % replay)

        # Leaving is announced.
        b.close()
        bye = a.expect("B")
        check(bye[1] == str(b.id), "alpha got bye %s" % bye)

        a.close()
        c.close()
    finally:
        server.terminate()
        server.wait()

    if failures:
        print("PROTOCOL TEST FAILED (%d problems)" % len(failures))
        return 1
    print("protocol test ok (rooms, seed, ghosts, claims, lockout dedupe)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
