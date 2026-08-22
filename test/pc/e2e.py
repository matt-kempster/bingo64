#!/usr/bin/env python3
"""E2E presence rig: in-process relay + RefClient host 'quate' + the real
game as 'matt'. quate starts the race, roams BOB, claims two cells; we
screenshot the claim toast and the L-screen roster. Then quate claims a
full row and reports a finish (the client judges its own bingo; the relay
only assigns places): screenshot the persistent verdict banner, tap L
once (dismiss hint appears), tap L again (banner gone).

Run under Xvfb (see test/pc/README.md); screenshots land in $E2E_OUT
(default: cwd). The game binary comes from $BL (default ~/b64-refresh),
built by test/pc/linuxbuild.sh.
"""
import asyncio
import os
import subprocess
import sys
import time

WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SP = os.environ.get("E2E_OUT", os.getcwd())
BL = os.environ.get("BL", os.path.expanduser("~/b64-refresh")) + "/build/us_pc"
sys.path.insert(0, WT + "/server")
from test_relay import start_relay, RefClient  # noqa: E402


def xenv():
    env = dict(os.environ, DISPLAY=":99", SDL_AUDIODRIVER="dummy")
    env.pop("WAYLAND_DISPLAY", None)  # else WSLg pops a real window
    return env


def shot(wid, name):
    subprocess.run(["import", "-window", wid, "%s/%s.png" % (SP, name)],
                   env=xenv(), check=False)
    print("shot", name, flush=True)


def tap(wid, key):
    subprocess.run(["xdotool", "keydown", "--window", wid, key], env=xenv())
    time.sleep(0.15)  # a few frames of buttonDown, one buttonPressed
    subprocess.run(["xdotool", "keyup", "--window", wid, key], env=xenv())


async def main():
    tcp, udp, _endpoint, shutdown = await start_relay()
    print("relay tcp=%d udp=%d" % (tcp, udp), flush=True)

    quate = RefClient(udp)
    await quate.start()
    quate.join("demo", "quate", color=5)
    quate.send("R 1")
    await asyncio.sleep(0.5)

    game = subprocess.Popen(
        [BL + "/sm64.us.f3dex2e", "--skip-intro", "--skip-title",
         "--net-server", "127.0.0.1:%d" % tcp, "--net-room", "demo",
         "--net-name", "matt", "--net-color", "1"],
        cwd=BL, env=xenv(),
        stdout=open(SP + "/e2e_game.log", "w"), stderr=subprocess.STDOUT)
    try:
        for _ in range(300):
            await asyncio.sleep(0.1)
            if any(l.startswith("N ") and "matt" in l for l in quate.lines):
                break
        else:
            print("FAIL: game never joined; tail:", quate.lines[-8:],
                  flush=True)
            return
        print("game joined", flush=True)

        # SDL restores a saved window position that can sit outside the
        # Xvfb root (black captures); move it to 0,0 and capture by id.
        wid = None
        for _ in range(30):
            await asyncio.sleep(0.5)
            out = subprocess.run(["xdotool", "search", "--name",
                                  "Super Mario"], env=xenv(),
                                 capture_output=True, text=True).stdout
            if out.split():
                wid = out.split()[0]
                break
        if wid is None:
            print("FAIL: game window never appeared", flush=True)
            return
        subprocess.run(["xdotool", "windowmove", wid, "0", "0"], env=xenv())

        quate.send("X")
        print("race started", flush=True)

        t0 = asyncio.get_event_loop().time()
        did = set()
        while True:
            el = asyncio.get_event_loop().time() - t0
            if el > 42:
                break
            quate.send("G 9 1 0.0 500.0 0.0 0 0 0")
            await asyncio.sleep(0.1)
            if el > 14 and "c1" not in did:
                did.add("c1")
                quate.send("C 7")
                print("claimed cell 7", flush=True)
            if el > 16 and "s1" not in did:
                did.add("s1")
                shot(wid, "e2e_toast")
            if el > 18 and "c2" not in did:
                did.add("c2")
                quate.send("C 12")
            if el > 21 and "s2" not in did:
                did.add("s2")
                subprocess.run(["xdotool", "keydown", "--window", wid, "q"],
                               env=xenv())
                await asyncio.sleep(0.8)
                shot(wid, "e2e_board")
                subprocess.run(["xdotool", "keyup", "--window", wid, "q"],
                               env=xenv())
            if el > 24 and "win" not in did:
                did.add("win")
                # bottom row of the board: a bingo line -> quate wins.
                for c in (0, 1, 2, 3, 4):
                    quate.send("C %d" % c)
                quate.send("F")
                print("quate claimed a full row and finished", flush=True)
            if el > 28 and "s3" not in did:
                did.add("s3")
                shot(wid, "e2e_verdict")
            if el > 30 and "l1" not in did:
                did.add("l1")
                tap(wid, "q")
                print("tapped L once", flush=True)
            if el > 33 and "s4" not in did:
                did.add("s4")
                shot(wid, "e2e_verdict_hint")
            if el > 35 and "l2" not in did:
                did.add("l2")
                tap(wid, "q")
                print("tapped L twice", flush=True)
            if el > 38 and "s5" not in did:
                did.add("s5")
                shot(wid, "e2e_verdict_gone")
        print("done", flush=True)
    finally:
        game.kill()
        await shutdown()


asyncio.run(main())
