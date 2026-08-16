#!/usr/bin/env python3
"""In-game E2E for the K (back-to-lobby / rematch) flow.

A scripted TCP client acts as the room host; one real game client joins
via --net-server. The host starts a race (X), ends it (K), and starts a
second one (X). Pass criteria:
  - the game reaches the level after each X (its ghost G lines arrive)
  - the game logs "back to the lobby" after K
  - the game logs two "race starts" lines (one per S)
  - the game process is still alive at the end (no crash on the warp)

Needs a display (the game is not headless) — run it where the built exe
runs, e.g. WSLg. Logs land in a temp dir printed at start.

Run: python3 test/net/rematch_e2e.py <repo> <exe>
"""

import os
import socket
import subprocess
import sys
import tempfile
import time

REPO = sys.argv[1]
EXE = sys.argv[2]
PORT = 64222
# Where the GAME dials the relay. Defaults to loopback; a Windows exe
# run from WSL interop needs the WSL adapter IP instead (the relay
# binds 0.0.0.0, but Windows' 127.0.0.1 is not WSL's).
GAME_SERVER = os.environ.get("E2E_GAME_SERVER", "127.0.0.1:%d" % PORT)
SCRATCH = tempfile.mkdtemp(prefix="rematch_e2e_")
print("logs: %s" % SCRATCH)

sys.path.insert(0, os.path.join(REPO, "server"))
from relay import PROTOCOL_VERSION

failures = []


def check(cond, msg):
    print(("  ok: " if cond else "  FAIL: ") + msg)
    if not cond:
        failures.append(msg)


class HostBot:
    def __init__(self):
        self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=5)
        self.sock.settimeout(0.25)
        self.buf = b""

    def send(self, line):
        self.sock.sendall((line + "\n").encode())

    def poll_lines(self):
        try:
            data = self.sock.recv(4096)
            if data:
                self.buf += data
        except socket.timeout:
            pass
        lines = []
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            lines.append(raw.decode())
        return lines

    def wait_for(self, prefix, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for ln in self.poll_lines():
                if ln.startswith(prefix):
                    return ln
        return None


def main():
    relay = subprocess.Popen(
        [sys.executable, "-u", os.path.join(REPO, "server", "relay.py"),
         "--port", str(PORT)],
        stdout=open(os.path.join(SCRATCH, "e2e_relay.log"), "w"),
        stderr=subprocess.STDOUT)
    time.sleep(1.0)

    game_log = os.path.join(SCRATCH, "e2e_game.log")
    game = subprocess.Popen(
        [EXE, "--skip-title",
         "--net-server", GAME_SERVER,
         "--net-room", "e2e", "--net-name", "racer"],
        stdout=open(game_log, "w"), stderr=subprocess.STDOUT,
        cwd=os.path.dirname(EXE))

    try:
        host = HostBot()
        host.send("J %d e2e hostbot 1 0" % PROTOCOL_VERSION)
        check(host.wait_for("W ", 5) is not None, "hostbot welcomed")
        check(host.wait_for("N ", 30) is not None, "game client joined")

        print("race 1: START")
        host.send("X")
        check(host.wait_for("S ", 5) is not None, "S broadcast for race 1")
        check(host.wait_for("G ", 40) is not None,
              "game reached the level for race 1 (ghost traffic)")

        print("host: BACK TO LOBBY (K)")
        host.send("K")
        check(host.wait_for("K", 5) is not None, "K broadcast received")
        # give the game time to warp out to the file select
        time.sleep(8.0)
        check(game.poll() is None, "game alive after the lobby warp")

        print("race 2: START (the rematch)")
        host.send("X")
        check(host.wait_for("S ", 5) is not None, "S broadcast for race 2")
        check(host.wait_for("G ", 40) is not None,
              "game reached the level for race 2 (ghost traffic)")
        check(game.poll() is None, "game alive at the end")
    finally:
        game.terminate()
        try:
            game.wait(timeout=5)
        except subprocess.TimeoutExpired:
            game.kill()
        relay.terminate()

    with open(game_log) as f:
        log = f.read()
    check(log.count("race starts in") == 2, "game handled two S starts")
    check("back to the lobby" in log, "game handled the K reset")

    print("PASS" if not failures else "FAILED: %d checks" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
