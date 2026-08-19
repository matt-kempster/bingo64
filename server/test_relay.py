#!/usr/bin/env python3
"""Tests for the relay's UDP transport (and TCP interop).

The RefClient here is the reference implementation of the client side of
the UDP transport: src/pc/network/network.c mirrors its behavior exactly
(reliable out-queue with cumulative acks and periodic resend, in-order
dedup of inbound reliable messages, stale-drop for ghost datagrams,
keepalives). If a transport question comes up in the C code, the answer
should match what this file does.

The LossyProxy simulates the internet: seeded random loss, duplication
and delay-jitter (which reorders) in both directions.

Run:  python3 server/test_relay.py -v
Stdlib only; no pytest needed.
"""

import asyncio
import json
import os
import random
import struct
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import relay as relaymod
from relay import (Relay, UdpEndpoint, UDP_HEADER, UDP_MAGIC, UDP_GHOST,
                   UDP_RELIABLE, UDP_ACK, UDP_KEEPALIVE, PROTOCOL_VERSION)

LOOPBACK = "127.0.0.1"


# ---------------------------------------------------------------- harness

class _Proto(asyncio.DatagramProtocol):
    """Datagram protocol that hands packets to a callback."""

    def __init__(self, on_dgram):
        self.on_dgram = on_dgram
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        self.on_dgram(data, addr)


async def start_relay():
    """In-process relay on ephemeral ports. Returns (tcp_port, udp_port,
    endpoint, shutdown)."""
    loop = asyncio.get_running_loop()
    relay = Relay()
    server = await asyncio.start_server(relay.handle_tcp, LOOPBACK, 0)
    tcp_port = server.sockets[0].getsockname()[1]
    transport, endpoint = await loop.create_datagram_endpoint(
        lambda: UdpEndpoint(relay), local_addr=(LOOPBACK, 0))
    udp_port = transport.get_extra_info("sockname")[1]
    tick = asyncio.ensure_future(endpoint.tick_loop())

    async def shutdown():
        tick.cancel()
        transport.close()
        server.close()
        await server.wait_closed()

    return tcp_port, udp_port, endpoint, shutdown


class LossyProxy:
    """UDP proxy applying seeded loss/dup/jitter in both directions."""

    def __init__(self, server_port, rng, loss=0.0, dup=0.0, jitter=0.0):
        self.server_addr = (LOOPBACK, server_port)
        self.rng = rng
        self.loss = loss
        self.dup = dup
        self.jitter = jitter
        self.client_addr = None
        self.port = None
        self.closed = False
        self._listen = None
        self._up = None

    async def start(self):
        loop = asyncio.get_running_loop()
        self._listen, _ = await loop.create_datagram_endpoint(
            lambda: _Proto(self._from_client), local_addr=(LOOPBACK, 0))
        self.port = self._listen.get_extra_info("sockname")[1]
        self._up, _ = await loop.create_datagram_endpoint(
            lambda: _Proto(self._from_server), remote_addr=self.server_addr)

    def _mangle(self, send):
        if self.rng.random() < self.loss:
            return
        guarded = lambda: self.closed or send()
        loop = asyncio.get_event_loop()
        loop.call_later(self.rng.random() * self.jitter, guarded)
        if self.rng.random() < self.dup:
            loop.call_later(self.rng.random() * self.jitter, guarded)

    def _from_client(self, data, addr):
        self.client_addr = addr
        self._mangle(lambda: self._up.sendto(data))

    def _from_server(self, data, addr):
        if self.client_addr is not None:
            dest = self.client_addr
            self._mangle(lambda: self._listen.sendto(data, dest))

    def close(self):
        self.closed = True
        self._listen.close()
        self._up.close()


class RefClient:
    """Reference UDP transport client (the model for network.c)."""

    def __init__(self, port, resend=0.25, keepalive=1.0):
        self.addr = (LOOPBACK, port)
        self.cid = random.randrange(1, 2 ** 32)
        self.resend = resend
        self.keepalive = keepalive
        self.out = []            # [(seq, dgram)] awaiting server ack
        self.next_seq = 1
        self.in_expected = 0
        self.ghost_in = 0
        self.ghost_out = 0
        self.lines = []          # every line delivered, in arrival order
        self.last_from_server = time.monotonic()
        self._transport = None
        self._tick = None

    async def start(self):
        loop = asyncio.get_running_loop()
        self._transport, _ = await loop.create_datagram_endpoint(
            lambda: _Proto(self._on_dgram), remote_addr=self.addr)
        self._tick = asyncio.ensure_future(self._tick_loop())

    async def migrate(self):
        """Rebind to a fresh local socket (NAT rebind), same session."""
        self._transport.close()
        loop = asyncio.get_running_loop()
        self._transport, _ = await loop.create_datagram_endpoint(
            lambda: _Proto(self._on_dgram), remote_addr=self.addr)

    def _dgram(self, type_, seq, payload=b""):
        return UDP_HEADER.pack(UDP_MAGIC, type_, self.cid, seq) + payload

    def _sendto(self, dgram):
        if self._transport is not None and not self._transport.is_closing():
            self._transport.sendto(dgram)

    def send(self, line):
        if line.startswith("G "):
            self.ghost_out += 1
            self._sendto(self._dgram(UDP_GHOST, self.ghost_out, line.encode()))
            return None
        seq = self.next_seq
        self.next_seq += 1
        dgram = self._dgram(UDP_RELIABLE, seq, line.encode())
        self.out.append((seq, dgram))
        self._sendto(dgram)
        return seq

    def resend_raw(self, seq):
        """Deliberately re-send an already-sent reliable datagram (to test
        the server's transport-level dedup)."""
        for s, dgram in self.out:
            if s == seq:
                self._sendto(dgram)

    def join(self, room, name, color=0, flags=0, token=0,
             version=PROTOCOL_VERSION):
        self.send("J %d %s %s %d %d %d" % (version, room, name, color,
                                           flags, token))

    def _on_dgram(self, data, addr):
        if len(data) < UDP_HEADER.size:
            return
        magic, type_, cid, seq = UDP_HEADER.unpack_from(data)
        if magic != UDP_MAGIC or cid != self.cid:
            return
        self.last_from_server = time.monotonic()
        payload = data[UDP_HEADER.size:]
        if type_ == UDP_ACK:
            while self.out and self.out[0][0] <= seq:
                self.out.pop(0)
        elif type_ == UDP_GHOST:
            if seq > self.ghost_in:
                self.ghost_in = seq
                self.lines.append(payload.decode())
        elif type_ == UDP_RELIABLE:
            if seq == self.in_expected + 1:
                self.in_expected = seq
                self.lines.append(payload.decode())
            self._sendto(self._dgram(UDP_ACK, self.in_expected))
        elif type_ == UDP_KEEPALIVE:
            self._sendto(self._dgram(UDP_ACK, self.in_expected))

    async def _tick_loop(self):
        last_keepalive = 0.0
        while True:
            await asyncio.sleep(self.resend)
            for _, dgram in self.out[:8]:
                self._sendto(dgram)
            now = time.monotonic()
            if now - last_keepalive >= self.keepalive:
                last_keepalive = now
                self._sendto(self._dgram(UDP_KEEPALIVE, 0))

    async def wait_line(self, prefix, timeout=5.0, skip=0):
        """Wait until a line starting with prefix arrives; returns it."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            hits = [ln for ln in self.lines if ln.startswith(prefix)]
            if len(hits) > skip:
                return hits[skip]
            await asyncio.sleep(0.01)
        raise AssertionError("no line %r within %.1fs (got %r)"
                             % (prefix, timeout, self.lines[-20:]))

    def count(self, prefix):
        return sum(1 for ln in self.lines if ln.startswith(prefix))

    def close(self):
        if self._tick is not None:
            self._tick.cancel()
        if self._transport is not None:
            self._transport.close()


class TcpRef:
    """Minimal TCP client for mixed-transport tests."""

    def __init__(self, port):
        self.port = port
        self.lines = []
        self.reader = None
        self.writer = None
        self._pump = None

    async def start(self):
        self.reader, self.writer = await asyncio.open_connection(
            LOOPBACK, self.port)
        self._pump = asyncio.ensure_future(self._read_loop())

    async def _read_loop(self):
        while True:
            line = await self.reader.readline()
            if not line:
                return
            self.lines.append(line.decode().rstrip("\n"))

    def send(self, line):
        self.writer.write((line + "\n").encode())

    wait_line = RefClient.wait_line
    count = RefClient.count

    def close(self):
        if self._pump is not None:
            self._pump.cancel()
        if self.writer is not None:
            self.writer.close()


# ------------------------------------------------------------------ tests

class RelayUdpTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        # Fast cadences so lossy tests converge quickly.
        self._saved = (relaymod.UDP_RESEND_S, relaymod.UDP_TIMEOUT_S)
        relaymod.UDP_RESEND_S = 0.05
        self.tcp_port, self.udp_port, self.endpoint, self._shutdown = \
            await start_relay()
        self._cleanup = []

    async def asyncTearDown(self):
        for c in self._cleanup:
            c.close()
        await self._shutdown()
        relaymod.UDP_RESEND_S, relaymod.UDP_TIMEOUT_S = self._saved

    def track(self, client):
        self._cleanup.append(client)
        return client

    async def two_joined(self, port=None, **kw):
        a = self.track(RefClient(port or self.udp_port, **kw))
        b = self.track(RefClient(port or self.udp_port, **kw))
        await a.start()
        await b.start()
        a.join("testroom", "alice")
        await a.wait_line("W ")
        b.join("testroom", "bob")
        await b.wait_line("W ")
        return a, b

    async def test_basic_two_player_flow(self):
        a, b = await self.two_joined()
        w_a = (await a.wait_line("W ")).split()
        w_b = (await b.wait_line("W ")).split()
        self.assertEqual(w_a[1], "1")
        self.assertEqual(w_b[1], "2")
        await a.wait_line("N 2 bob")       # roster broadcast reached A
        await b.wait_line("N 1 alice")     # snapshot reached B
        b.send("R 1")
        await a.wait_line("R 2 1")
        a.send("X")                        # alice created the room: host
        s_a = (await a.wait_line("S ")).split()
        s_b = (await b.wait_line("S ")).split()
        self.assertEqual(s_a[1], s_b[1])   # same seed for everyone
        a.send("C 7")
        await b.wait_line("C 7 1")
        b.send("F")
        f = (await a.wait_line("F ")).split()
        self.assertEqual(f[1], "2")        # bob...
        self.assertEqual(f[2], "1")        # ...finished first

    async def test_ghost_flow_and_stale_drop(self):
        a, b = await self.two_joined()
        for i in range(10):
            a.send("G 16 1 %d.0 100.0 200.0 0 0 0" % i)
            await asyncio.sleep(0.005)
        await b.wait_line("G 1 16")
        # Ghosts arrive tagged with alice's id and are never duplicated
        # beyond what was sent.
        self.assertLessEqual(b.count("G 1 "), 10)

    async def test_lossy_network_converges(self):
        rng = random.Random(0xB1460)
        proxy_a = LossyProxy(self.udp_port, rng, loss=0.12, dup=0.05,
                             jitter=0.08)
        proxy_b = LossyProxy(self.udp_port, rng, loss=0.12, dup=0.05,
                             jitter=0.08)
        await proxy_a.start()
        await proxy_b.start()
        try:
            a = self.track(RefClient(proxy_a.port, resend=0.05))
            b = self.track(RefClient(proxy_b.port, resend=0.05))
            await a.start()
            await b.start()
            a.join("lossy", "alice")
            await a.wait_line("W ", timeout=10)
            b.join("lossy", "bob")
            await b.wait_line("W ", timeout=10)
            await a.wait_line("N 2 bob", timeout=10)
            a.send("X")
            await a.wait_line("S ", timeout=10)
            await b.wait_line("S ", timeout=10)
            for cell in (3, 11, 24):
                a.send("C %d" % cell)
            for cell in (5, 19):
                b.send("C %d" % cell)
            for cell in (3, 11, 24, 5, 19):
                await a.wait_line("C %d " % cell, timeout=10)
                await b.wait_line("C %d " % cell, timeout=10)
            # Reliable state converged identically despite loss/dup/reorder.
            self.assertEqual(sorted(ln for ln in a.lines if ln.startswith("C ")),
                             sorted(ln for ln in b.lines if ln.startswith("C ")))
            # The lossy path still carries some ghost traffic.
            for i in range(40):
                a.send("G 16 1 %d.0 0.0 0.0 0 0 0" % i)
                await asyncio.sleep(0.005)
            await b.wait_line("G 1 ", timeout=10)
        finally:
            proxy_a.close()
            proxy_b.close()

    async def test_transport_dedup(self):
        a, b = await self.two_joined()
        seq = a.send("R 1")
        await b.wait_line("R 1 1")
        a.resend_raw(seq)            # duplicated datagram, same seq
        a.send("O 4 0 0 0")          # anything later, to flush ordering
        await b.wait_line("O 4")
        self.assertEqual(b.count("R 1 1"), 1)  # dup was dropped, not re-run

    async def test_reconnect_with_token(self):
        relaymod.UDP_TIMEOUT_S = 1.0
        a, b = await self.two_joined(keepalive=0.3)
        token_b = int((await b.wait_line("W ")).split()[4])
        a.send("X")
        seed_b = (await b.wait_line("S ")).split()[1]
        b.send("C 12")
        await a.wait_line("C 12 2")
        b.close()                      # vanish mid-race, no goodbye
        self._cleanup.remove(b)
        await a.wait_line("D 2", timeout=5)   # keepalives stopped: seat held
        b2 = self.track(RefClient(self.udp_port, keepalive=0.3))
        await b2.start()
        b2.join("testroom", "bob", token=token_b)
        w = (await b2.wait_line("W ")).split()
        self.assertEqual(w[1], "2")           # same seat
        s = (await b2.wait_line("S ")).split()
        self.assertEqual(s[1], seed_b)        # same race, same seed
        await b2.wait_line("C 12 2")          # own claim replayed

    async def test_address_migration(self):
        a, b = await self.two_joined()
        await a.migrate()
        a.send("R 1")
        await b.wait_line("R 1 1")     # server followed the new source addr

    async def test_mixed_tcp_udp_room(self):
        u = self.track(RefClient(self.udp_port))
        await u.start()
        t = self.track(TcpRef(self.tcp_port))
        await t.start()
        u.join("mixed", "udpguy")
        await u.wait_line("W ")
        t.send("J %d mixed tcpguy 1 0" % PROTOCOL_VERSION)
        await t.wait_line("W ")
        await u.wait_line("N 2 tcpguy")
        u.send("X")
        await t.wait_line("S ")
        await u.wait_line("S ")
        t.send("C 4")
        await u.wait_line("C 4 2")
        u.send("C 9")
        await t.wait_line("C 9 1")

    async def test_refusal_is_reliable_under_loss(self):
        rng = random.Random(7)
        proxy = LossyProxy(self.udp_port, rng, loss=0.3, jitter=0.05)
        await proxy.start()
        try:
            c = self.track(RefClient(proxy.port, resend=0.05))
            await c.start()
            c.join("x", "old", version=1)
            line = await c.wait_line("E version", timeout=10)
            self.assertIn(str(PROTOCOL_VERSION), line)
        finally:
            proxy.close()

    async def test_udp_quit_is_immediate(self):
        a, b = await self.two_joined()
        b.send("Q")
        await a.wait_line("B 2", timeout=2)   # no 30s timeout wait

    async def test_back_to_lobby_and_rematch(self):
        a, b = await self.two_joined()
        a.send("K")                    # lobby phase: nothing to reset
        b.send("R 1")
        await a.wait_line("R 2 1")
        a.send("X")
        await b.wait_line("S ")
        a.send("C 7")
        await b.wait_line("C 7 1")
        b.send("F")
        await a.wait_line("F 2 1 ")
        b.send("K")                    # not the host: ignored
        a.send("K")
        await a.wait_line("K")
        await b.wait_line("K")
        self.assertEqual(b.count("K"), 1)
        await a.wait_line("N 2 bob 0 0 1", skip=1)  # roster re-sent, unready
        # The room is reusable: the same host starts a fresh race and the
        # claim board really was cleared (a re-claim of cell 7 would be
        # dropped as a duplicate otherwise).
        a.send("X")
        await b.wait_line("S ", skip=1)
        a.send("C 7")
        await b.wait_line("C 7 1", skip=1)

    async def test_host_passes_mid_race(self):
        relaymod.UDP_TIMEOUT_S = 1.0
        a, b = await self.two_joined(keepalive=0.3)
        a.send("X")
        await b.wait_line("S ")
        a.close()                          # the host vanishes mid-race
        self._cleanup.remove(a)
        await b.wait_line("D 1", timeout=5)  # seat held for a reconnect...
        await b.wait_line("H 2", timeout=5)  # ...but the host role moved on
        b.send("K")                        # so the new host can end the race
        await b.wait_line("B 1")           # held seat released for good
        await b.wait_line("K")
        b.send("X")
        await b.wait_line("S ", skip=1)    # and start a fresh one

    async def test_tied_finishes_get_distinct_places(self):
        # Two F's in the same server tick: places follow arrival order,
        # even when the authoritative frame counts are identical.
        a, b = await self.two_joined()
        a.send("X")
        await a.wait_line("S ")
        await b.wait_line("S ")
        a.send("F")
        b.send("F")
        fa = (await a.wait_line("F 1 ")).split()
        fb = (await a.wait_line("F 2 ")).split()
        self.assertEqual(fa[2], "1")
        self.assertEqual(fb[2], "2")   # no shared place
        # B sees the identical standings.
        self.assertEqual(await b.wait_line("F 1 "), " ".join(fa))
        self.assertEqual(await b.wait_line("F 2 "), " ".join(fb))

    async def test_abandoned_race_room_is_collected(self):
        # Everyone silently vanishes mid-race: the held seats must not
        # keep the room object alive once the last transport times out.
        relaymod.UDP_TIMEOUT_S = 0.6
        a, b = await self.two_joined(keepalive=0.2)
        a.send("X")
        await b.wait_line("S ")
        a.close()
        b.close()
        self._cleanup.remove(a)
        self._cleanup.remove(b)
        await asyncio.sleep(2.0)       # both silence timeouts elapse
        c = self.track(RefClient(self.udp_port, keepalive=0.2))
        await c.start()
        c.join("testroom", "carol")
        w = (await c.wait_line("W ")).split()
        self.assertEqual(w[1], "1")    # fresh room, not seat #3
        self.assertEqual(c.count("S "), 0)  # and no stale race replayed

    async def test_join_during_countdown(self):
        # The relay lets a joiner in mid-countdown; the snapshot's S
        # carries the frames still to go so their clock lines up.
        a = self.track(RefClient(self.udp_port))
        await a.start()
        a.join("testroom", "alice")
        await a.wait_line("W ")
        a.send("X")
        await a.wait_line("S ")
        b = self.track(RefClient(self.udp_port))
        await b.start()
        b.join("testroom", "bob")
        await b.wait_line("W ")
        delta = int((await b.wait_line("S ")).split()[2])
        self.assertGreater(delta, 0)
        self.assertLessEqual(delta, relaymod.COUNTDOWN_FRAMES)

    async def test_duplicate_names_get_suffixed(self):
        a = self.track(RefClient(self.udp_port))
        b = self.track(RefClient(self.udp_port))
        await a.start()
        await b.start()
        a.join("testroom", "mario")
        await a.wait_line("W ")
        b.join("testroom", "mario")
        await b.wait_line("W ")
        await a.wait_line("N 2 mario2")     # the newcomer was renamed
        await b.wait_line("N 1 mario ")     # the original kept the name

    async def test_force_start_with_nobody_ready(self):
        # Ready marks are advisory: the host may start a race where not
        # a single member (including the host) pressed READY.
        a, b = await self.two_joined()
        a.send("X")
        await a.wait_line("S ")
        await b.wait_line("S ")

    async def test_ready_ignored_once_started(self):
        a, b = await self.two_joined()
        a.send("X")
        await b.wait_line("S ")
        b.send("R 0")                       # un-ready during the countdown
        b.send("C 3")                       # flush ordering
        await a.wait_line("C 3 2")
        self.assertEqual(a.count("R 2 "), 0)  # never echoed to the room

    async def test_unknown_conn_flood_is_capped(self):
        # Background-radiation guard: valid-magic garbage from random
        # conn_ids must not grow state past UDP_MAX_CONNS.
        saved = relaymod.UDP_MAX_CONNS
        relaymod.UDP_MAX_CONNS = 8
        try:
            baseline = len(self.endpoint.conns)
            flooder = self.track(RefClient(self.udp_port))
            await flooder.start()
            for cid in range(1000, 1030):
                flooder.cid = cid
                flooder._sendto(flooder._dgram(UDP_KEEPALIVE, 0))
            await asyncio.sleep(0.3)
            self.assertLessEqual(len(self.endpoint.conns) - baseline, 8)
        finally:
            relaymod.UDP_MAX_CONNS = saved

    async def test_room_full_refusal(self):
        clients = []
        for i in range(relaymod.MAX_ROOM):
            c = self.track(RefClient(self.udp_port))
            await c.start()
            c.join("full", "p%d" % i)
            await c.wait_line("W ")
            clients.append(c)
        extra = self.track(RefClient(self.udp_port))
        await extra.start()
        extra.join("full", "toolate")
        await extra.wait_line("E room_full")


class VersionAnchorTest(unittest.TestCase):
    """relay.py and the C client must agree on the protocol version —
    it is also the release version's minor number (v0.<protocol>.<patch>),
    so a mismatch here means a mislabeled release."""

    def test_protocol_versions_match(self):
        header = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "..", "src", "pc", "network", "network.h")
        with open(header, encoding="utf-8") as f:
            for line in f:
                if "#define NET_PROTOCOL_VERSION" in line:
                    c_version = int(line.split()[2])
                    break
            else:
                self.fail("NET_PROTOCOL_VERSION not found in network.h")
        self.assertEqual(PROTOCOL_VERSION, c_version)


# ---------------------------------------------------------- match log

class _NullClient(relaymod.Client):
    """Transportless client for driving process_line directly."""

    def __init__(self):
        super().__init__()

    def send(self, line, lossy=False):
        pass

    def kick(self):
        pass


class MatchLogTest(unittest.TestCase):
    """The JSONL history: one parseable event per semantic action, and a
    disk guard that pauses history without disturbing the relay."""

    def _events(self, logdir):
        import glob
        events = []
        for path in sorted(glob.glob(os.path.join(logdir, "*.jsonl"))):
            with open(path, encoding="utf-8") as f:
                for line in f:
                    events.append(json.loads(line))
        return events

    def test_full_race_is_recorded(self):
        with tempfile.TemporaryDirectory() as logdir:
            relay = Relay(matchlog=relaymod.MatchLog(logdir))
            host, other = _NullClient(), _NullClient()
            relay.process_line(host, "J 5 peach matt 2 0".split())
            relay.process_line(other, "J 5 peach luigi 4 0".split())
            relay.process_line(host, "X".split())
            relay.process_line(host, "C 12".split())
            relay.process_line(other, "F".split())
            relay.process_line(other, "Q".split())
            relay.process_line(host, "Q".split())

            events = self._events(logdir)
            by_kind = {}
            for e in events:
                by_kind.setdefault(e["ev"], []).append(e)
            for kind in ("room_created", "join", "start", "claim",
                         "finish", "leave", "room_closed"):
                self.assertIn(kind, by_kind, "missing event: " + kind)
            self.assertEqual(len(by_kind["join"]), 2)
            self.assertEqual(by_kind["join"][0]["name"], "matt")
            start = by_kind["start"][0]
            self.assertEqual(len(start["players"]), 2)
            self.assertGreater(start["seed"], 0)
            self.assertEqual(by_kind["claim"][0]["cell"], 12)
            self.assertEqual(by_kind["finish"][0]["place"], 1)
            for e in events:  # every record is dated and room-stamped
                self.assertIn("t", e)
                self.assertEqual(e["room"], "peach")

    def test_disk_guard_pauses_history_not_relay(self):
        with tempfile.TemporaryDirectory() as logdir:
            relay = Relay(matchlog=relaymod.MatchLog(logdir))
            saved = relaymod.MATCHLOG_MIN_FREE
            relaymod.MATCHLOG_MIN_FREE = 1 << 62  # nothing is this free
            try:
                host = _NullClient()
                relay.process_line(host, "J 5 peach matt 2 0".split())
                self.assertIsNotNone(relay.log.paused)
                self.assertEqual(self._events(logdir), [])
                # The relay itself is unbothered.
                self.assertIn("peach", relay.rooms)
            finally:
                relaymod.MATCHLOG_MIN_FREE = saved
            relay.process_line(host, "X".split())  # resumes on next event
            self.assertIsNone(relay.log.paused)
            self.assertEqual(self._events(logdir)[0]["ev"], "start")


if __name__ == "__main__":
    unittest.main()
