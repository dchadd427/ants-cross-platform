#!/usr/bin/env python3
"""End-to-end smoke test for the C++17 20 Hz playable 3D match bridge (tools/ants_3d_bridge.cpp).

Pure Python 3 standard library: implements a minimal RFC 6455 binary-frame WebSocket client
so it runs anywhere without third-party packages.
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
import re
import signal
import socket
import struct
import subprocess
import sys
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def find_bridge_binary() -> str | None:
    env_bin = os.environ.get("ANTS_3D_BRIDGE_BIN")
    if env_bin and os.path.isfile(env_bin):
        return env_bin
    candidates = [
        os.path.join(REPO, "build", "ants_3d_bridge"),
        os.path.join(REPO, "build", "ants_3d_bridge.exe"),
        os.path.join(REPO, "build", "Release", "ants_3d_bridge.exe"),
        os.path.join(REPO, "build", "Debug", "ants_3d_bridge.exe"),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


class MiniBinaryWsClient:
    """Minimal RFC 6455 client exchanging UTF-8 JSON inside binary frames (opcode 0x2)."""

    def __init__(self, host: str, port: int, timeout: float = 5.0) -> None:
        self._sock = socket.create_connection((host, port), timeout=timeout)
        self._sock.settimeout(timeout)
        self._buf = bytearray()
        self._handshake(host, port)

    def _recv_exact(self, n: int) -> bytes:
        while len(self._buf) < n:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise ConnectionError("socket closed while reading WebSocket frame")
            self._buf.extend(chunk)
        out = bytes(self._buf[:n])
        del self._buf[:n]
        return out

    def _handshake(self, host: str, port: int) -> None:
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        req = (
            f"GET /ws HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii")
        self._sock.sendall(req)
        while b"\r\n\r\n" not in self._buf:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise ConnectionError("socket closed during WebSocket handshake")
            self._buf.extend(chunk)
        sep = self._buf.index(b"\r\n\r\n") + 4
        head = bytes(self._buf[:sep]).decode("latin1")
        del self._buf[:sep]
        if not head.startswith("HTTP/1.1 101"):
            raise RuntimeError(f"unexpected WS handshake response: {head.splitlines()[0]}")
        expected = base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest()).decode("ascii")
        if expected not in head:
            raise RuntimeError("missing or invalid Sec-WebSocket-Accept header")

    def _send_frame(self, opcode: int, payload: bytes) -> None:
        mask = os.urandom(4)
        n = len(payload)
        header = bytearray([0x80 | (opcode & 0x0F)])
        if n < 126:
            header.append(0x80 | n)
        elif n <= 0xFFFF:
            header.append(0x80 | 126)
            header.extend(struct.pack("!H", n))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack("!Q", n))
        header.extend(mask)
        masked = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
        self._sock.sendall(bytes(header) + masked)

    def send_json(self, obj: dict) -> None:
        raw = json.dumps(obj, separators=(",", ":")).encode("utf-8")
        self._send_frame(0x2, raw)

    def recv_json(self, timeout: float = 5.0) -> dict:
        self._sock.settimeout(timeout)
        while True:
            b0, b1 = self._recv_exact(2)
            opcode = b0 & 0x0F
            masked = (b1 & 0x80) != 0
            length = b1 & 0x7F
            if length == 126:
                (length,) = struct.unpack("!H", self._recv_exact(2))
            elif length == 127:
                (length,) = struct.unpack("!Q", self._recv_exact(8))
            mask_key = self._recv_exact(4) if masked else b""
            payload = self._recv_exact(length)
            if masked:
                payload = bytes(b ^ mask_key[i & 3] for i, b in enumerate(payload))
            if opcode == 0x9:  # Ping -> Pong
                self._send_frame(0xA, payload)
                continue
            if opcode == 0xA:  # Pong
                continue
            if opcode == 0x8:  # Close
                raise ConnectionError("peer sent WebSocket close frame")
            if opcode != 0x2:
                raise RuntimeError(f"expected binary frame opcode 0x2, got {opcode:#x}")
            return json.loads(payload.decode("utf-8"))

    def close(self) -> None:
        try:
            self._send_frame(0x8, struct.pack("!H", 1000))
        except OSError:
            pass
        try:
            self._sock.close()
        except OSError:
            pass


def recv_until(ws: MiniBinaryWsClient, predicate, timeout: float = 5.0) -> dict:
    deadline = time.monotonic() + timeout
    while True:
        rem = deadline - time.monotonic()
        if rem <= 0:
            raise TimeoutError("timed out waiting for matching WS message")
        msg = ws.recv_json(timeout=rem)
        if predicate(msg):
            return msg


BRIDGE_BIN = find_bridge_binary()


@unittest.skipUnless(BRIDGE_BIN is not None, "ants_3d_bridge binary not built (build with --target ants_3d_bridge)")
class Test3DBridge(unittest.TestCase):
    def setUp(self) -> None:
        assert BRIDGE_BIN is not None
        self.proc = subprocess.Popen(
            [
                BRIDGE_BIN,
                "--ws-port", "0",
                "--map", "Original-Ants/Maps/TREASURE.LVL",
                "--bots", "medium",
                "--seat", "0",
                "--fog", "0",
                "--seed", "1",
                "--starting-score", "250",
            ],
            cwd=REPO,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        assert self.proc.stdout is not None
        line = self.proc.stdout.readline()
        match = re.search(r"ws://0\.0\.0\.0:(\d+)", line)
        if not match:
            stderr_out = self.proc.stderr.read() if self.proc.stderr else ""
            self.fail(f"bridge failed to report listening port: stdout={line!r}, stderr={stderr_out!r}")
        self.port = int(match.group(1))
        self.clients: list[MiniBinaryWsClient] = []

    def tearDown(self) -> None:
        for ws in self.clients:
            ws.close()
        self.clients.clear()
        if self.proc.poll() is None:
            sig = signal.SIGINT if os.name != "nt" else signal.SIGTERM
            self.proc.send_signal(sig)
            try:
                rc = self.proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5.0)
                self.fail("bridge did not exit cleanly on signal")
            if os.name != "nt":
                self.assertEqual(rc, 0, f"expected clean exit code 0, got {rc}")
        if self.proc.stdout:
            self.proc.stdout.close()
        if self.proc.stderr:
            self.proc.stderr.close()

    def _connect(self) -> MiniBinaryWsClient:
        ws = MiniBinaryWsClient("127.0.0.1", self.port)
        self.clients.append(ws)
        return ws

    def test_end_to_end_match_protocol(self) -> None:
        ws = self._connect()

        # 1. Initial "init" payload on connect
        init_msg = recv_until(ws, lambda m: m.get("type") == "init")
        map_ids = [m["id"] for m in init_msg["maps"]]
        for expected_id in ("treasure", "gauntlet", "medium", "small", "tiny", "islands"):
            self.assertIn(expected_id, map_ids)
        m = init_msg["map"]
        self.assertEqual(m["id"], "treasure")
        self.assertEqual(m["file"], "TREASURE.LVL")
        self.assertEqual(m["w"], 60)
        self.assertEqual(m["h"], 60)
        self.assertEqual(len(m["tiles"]), 3600)
        self.assertTrue(all(0 <= t <= 238 for t in m["tiles"]))
        self.assertGreater(len(m["static_decor"]), 20)
        self.assertGreaterEqual(len(m["plants"]), 5)
        hill_by_team = {h["team"]: h for h in m["hills"]}
        self.assertEqual(set(hill_by_team.keys()), {0, 1, 2, 3})
        self.assertEqual(hill_by_team[0]["id"], "GREENHILL")
        self.assertEqual((hill_by_team[0]["col"], hill_by_team[0]["row"]), (37, 21))
        self.assertEqual(hill_by_team[3]["id"], "BLACKHILL")
        self.assertEqual((hill_by_team[3]["col"], hill_by_team[3]["row"]), (22, 21))
        self.assertEqual(m["seat"], 0)
        self.assertEqual(m["bots"], "medium")
        self.assertFalse(m["fog"])
        self.assertFalse(m["paused"])

        # 2. Verify live 20 Hz ticks advance
        t0 = recv_until(ws, lambda msg: msg.get("type") == "tick")
        self.assertEqual(len(t0["scores"]), 4)
        self.assertEqual(len(t0["eggs"]), 4)
        self.assertEqual(len(t0["hatching"]), 4)
        self.assertGreater(len(t0["ants"]), 0)
        self.assertEqual(len(t0["ants"][0]), 19)
        self.assertGreater(len(t0["food"]), 0)
        self.assertEqual(t0["fog_hex"], "")

        t1 = recv_until(ws, lambda msg: msg.get("type") == "tick" and msg["tick"] >= t0["tick"] + 4)
        self.assertGreater(t1["tick"], t0["tick"])
        self.assertLess(t1["rem_ms"], t0["rem_ms"])

        # 3. Order a Green ant to move and verify it actually moves
        green_ants = [a for a in t1["ants"] if a[1] == 0]
        self.assertGreater(len(green_ants), 0)
        ant = green_ants[0]
        ant_id, start_px, start_py = ant[0], ant[3], ant[4]
        start_tx, start_ty = start_px // 32, start_py // 32
        target_tx = max(2, min(57, start_tx + 3))
        target_ty = max(2, min(57, start_ty))
        ws.send_json({"type": "move", "ants": [ant_id], "tx": target_tx, "ty": target_ty})

        moved = recv_until(
            ws,
            lambda msg: msg.get("type") == "tick"
            and any(a[0] == ant_id and (a[3] != start_px or a[4] != start_py) for a in msg["ants"]),
            timeout=4.0,
        )
        moved_ant = [a for a in moved["ants"] if a[0] == ant_id][0]
        self.assertTrue(moved_ant[3] != start_px or moved_ant[4] != start_py)

        # Exercise attack, stop, and special commands cleanly
        enemy_ants = [a for a in moved["ants"] if a[1] != 0]
        self.assertGreater(len(enemy_ants), 0)
        ws.send_json({"type": "attack", "ants": [ant_id], "target_id": enemy_ants[0][0]})
        ws.send_json({"type": "stop", "ants": [ant_id]})
        ws.send_json({"type": "special", "ants": [ant_id], "tx": target_tx, "ty": target_ty})

        # Verify bots on non-human teams are actively controlling ants
        bot_start = {a[0]: (a[3], a[4]) for a in t0["ants"] if a[1] != 0}
        bot_moved = recv_until(
            ws,
            lambda msg: msg.get("type") == "tick"
            and any(a[0] in bot_start and (a[3], a[4]) != bot_start[a[0]] for a in msg["ants"]),
            timeout=5.0,
        )
        self.assertGreater(bot_moved["tick"], t0["tick"])

        # 4. Send hatch command and verify egg decrements & incubation starts
        eggs_before = moved["eggs"][0]
        self.assertGreater(eggs_before, 0)
        ws.send_json({"type": "hatch"})
        hatched = recv_until(
            ws,
            lambda msg: msg.get("type") == "tick" and msg["eggs"][0] == eggs_before - 1,
            timeout=3.0,
        )
        self.assertEqual(hatched["hatching"][0], 1)
        self.assertEqual(hatched["scores"][0], moved["scores"][0] - 200)

        # 5. Pause and resume
        ws.send_json({"type": "pause", "paused": True})
        paused_msg = recv_until(ws, lambda msg: msg.get("type") == "tick" and msg["paused"] is True)
        paused_tick = paused_msg["tick"]
        time.sleep(0.18)
        ws.send_json({"type": "pause", "paused": False})
        resumed_msg = recv_until(
            ws,
            lambda msg: msg.get("type") == "tick" and msg["paused"] is False and msg["tick"] > paused_tick,
        )
        self.assertGreater(resumed_msg["tick"], paused_tick)

        # 6. Second client connects mid-match and gets immediate "init"
        ws2 = self._connect()
        init2 = recv_until(ws2, lambda msg: msg.get("type") == "init")
        self.assertEqual(init2["map"]["file"], "TREASURE.LVL")

        # 7. Switch map via new_match to ISLANDS.LVL and verify fresh init on both clients
        ws.send_json({
            "type": "new_match",
            "map": "ISLANDS.LVL",
            "bots": "easy",
            "fog": False,
            "seat": 0,
            "seed": 2,
        })
        islands_init = recv_until(
            ws,
            lambda msg: msg.get("type") == "init" and msg["map"]["file"] == "ISLANDS.LVL",
        )
        self.assertEqual(islands_init["map"]["id"], "islands")
        self.assertEqual(islands_init["map"]["bots"], "easy")
        self.assertEqual(islands_init["map"]["w"], 60)
        self.assertEqual(islands_init["map"]["h"], 60)
        self.assertEqual(len(islands_init["map"]["tiles"]), 3600)

        islands_init2 = recv_until(
            ws2,
            lambda msg: msg.get("type") == "init" and msg["map"]["file"] == "ISLANDS.LVL",
        )
        self.assertEqual(islands_init2["map"]["id"], "islands")

        # 8. Switch to Fog of War match and verify bit-packed fog_hex
        ws.send_json({
            "type": "new_match",
            "map": "TINY.LVL",
            "bots": "off",
            "fog": True,
            "seat": 0,
            "seed": 3,
        })
        fog_init = recv_until(
            ws,
            lambda msg: msg.get("type") == "init" and msg["map"]["file"] == "TINY.LVL",
        )
        self.assertTrue(fog_init["map"]["fog"])
        fog_tick = recv_until(ws, lambda msg: msg.get("type") == "tick" and len(msg["fog_hex"]) > 0)
        expected_hex_len = ((fog_init["map"]["w"] * fog_init["map"]["h"] + 7) // 8) * 2
        self.assertEqual(len(fog_tick["fog_hex"]), expected_hex_len)

        # 9. Load a community map with pre-placed bombs and fire walls if Community-Maps/ exists
        popcorn = os.path.join(REPO, "Community-Maps", "POPcOrN.lvl")
        if os.path.isfile(popcorn):
            ws.send_json({
                "type": "new_match",
                "map": "POPcOrN.lvl",
                "bots": "off",
                "fog": False,
                "seat": 0,
                "seed": 4,
            })
            pop_init = recv_until(ws, lambda msg: msg.get("type") == "init" and msg["map"]["file"] == "POPcOrN.lvl")
            self.assertEqual(pop_init["map"]["id"], "popcorn")
            pop_tick = recv_until(ws, lambda msg: msg.get("type") == "tick" and len(msg["bombs"]) > 0)
            self.assertGreater(len(pop_tick["bombs"]), 50)
            self.assertGreater(len(pop_tick["fires"]), 10)


if __name__ == "__main__":
    if BRIDGE_BIN is None:
        print("ERROR: ants_3d_bridge binary not found; build with --target ants_3d_bridge", file=sys.stderr)
        sys.exit(1)
    unittest.main()
