#!/usr/bin/env python3
"""A replay that carries its map is served end to end: the real ants_server behind the real blocks of docker/nginx.conf (run by ./run_tests.sh; the program nginx and ants_server must be there).

A replay of a match on a test map holds the map's bytes (the chunk `tmap`, docs/REPLAYS.md "The file"): a file of up to 2 MiB and a bit, while the game server's public door answers at most 1 MiB for everything that is
not marked as a file (ctl::HttpResponse::file). The file of a replay used to be an answer like any other, so a replay over 1 MiB was an HTTP 500 for every browser, and a recording that was kept could not be watched.
Here a replay of 1.5 MiB (a map of 1 MiB and a match of half a MiB) is put into the store's folder, the real server lists it, and the file is fetched from the door directly and through nginx: each answer is 200,
byte for byte the file that the store keeps. The file is made here, byte by byte, from the format's own description (not by the program that reads it).
"""
import hashlib
import http.client
import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_nginx_submissions as nginx_rig  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD = os.environ.get("BUILD_DIR", "build")
SERVER = os.path.join(REPO, BUILD, "src", "ants_server", "ants_server")
NAME = "ants-DraftMap-20261008-143209Z.antsrep"
MAP_BYTES = 1024 * 1024                    # kMaxEmbeddedMapBytes
PADDING = 500 * 1000                       # a match part that a skipped chunk makes (the match is limited to 1 MiB; the map is not part of it)


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def varint(n):
    out = bytearray()
    while n >= 0x80:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    out.append(n)
    return bytes(out)


def field(ident, payload):
    return varint(ident) + varint(len(payload)) + payload


def chunk(tag, payload):
    tag = tag.encode()
    return tag + struct.pack("<I", len(payload)) + payload + struct.pack("<I", zlib.crc32(tag + payload) & 0xFFFFFFFF)


def replay_with_map(map_bytes, padding=0, map_name="Draft Map ~test-0badf00d.lvl"):
    """HEAD, tmap, a skipped chunk of `padding` bytes, ENDS: a complete replay of a match that ran 700 turns (the layout of include/ants_replay/replay.hpp)."""
    head = struct.pack("<HH", 1, 21)                                                      # format 1, the protocol's number
    head += field(1, b"v0.0.0") + field(2, b"test") + field(3, map_name.encode()) + field(4, struct.pack("<Q", fnv1a64(map_bytes)))
    head += field(5, struct.pack("<I", 7)) + field(6, b"\x05") + field(7, b"\x00") + field(14, b"network game") + field(15, struct.pack("<I", 100)) + field(16, struct.pack("<H", 5))
    data = b"\x89ARPL\r\n\x1a" + chunk("HEAD", head) + chunk("tmap", map_bytes)
    if padding:
        data += chunk("padd", b"\x00" * padding)
    return data + chunk("ENDS", struct.pack("<IBQ", 700, 1, 77))


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def get(port, path):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
    try:
        c.request("GET", path)
        r = c.getresponse()
        return r.status, dict((k.lower(), v) for k, v in r.getheaders()), r.read()
    finally:
        c.close()


@unittest.skipUnless(os.path.exists(SERVER) and shutil.which("nginx") is not None, "ants_server or nginx is missing: a replay over 1 MiB was NOT served end to end")
class BigReplayThroughNginx(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="ants_big_replays_")
        results = os.path.join(cls.work, "results")
        folder = os.path.join(results, "replays")
        os.makedirs(folder)
        cls.map_bytes = bytes((i * 31 + 7) & 0xFF for i in range(MAP_BYTES))
        cls.file = replay_with_map(cls.map_bytes, PADDING)
        path = os.path.join(folder, NAME)
        with open(path, "wb") as f:
            f.write(cls.file)
        aged = time.time() - 600                                                           # (a file that changed a moment ago is another program's write under way, and waits to be listed)
        os.utime(path, (aged, aged))
        cls.replay_port = free_port()
        cls.proc = subprocess.Popen([SERVER, "--maps", os.path.join(REPO, "Original-Ants", "Maps"), "--port", "0", "--ws-port", str(free_port()), "--replay-port", str(cls.replay_port), "--no-db",
                                     "--results-dir", results, "--replays-days", "3650"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for _ in range(300):
            try:
                socket.create_connection(("127.0.0.1", cls.replay_port), 0.2).close()
                break
            except OSError:
                if cls.proc.poll() is not None:
                    raise AssertionError("the server did not start: " + cls.proc.communicate(timeout=10)[1])
                time.sleep(0.05)
        cls.rig = nginx_rig.Rig(real_ports=(cls.replay_port, cls.replay_port))

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()
        if cls.proc.poll() is None:
            cls.proc.send_signal(signal.SIGTERM)
        try:
            cls.proc.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            cls.proc.kill()
            cls.proc.communicate()
        shutil.rmtree(cls.work, ignore_errors=True)

    def test_the_file_is_what_the_test_means_it_to_be(self):
        self.assertTrue(1024 * 1024 < len(self.file) < 2 * 1024 * 1024)                    # (the 1 MiB of the door lies below it, the 2 MiB of a replay above it)
        self.assertTrue(len(self.file) > 1500 * 1000)
        self.assertEqual(self.file.count(b"tmap"), 1)

    def test_the_list_has_it_as_a_file_that_can_be_read(self):
        status, headers, body = get(self.replay_port, "/replays")
        self.assertEqual(status, 200)
        listed = [e for e in json.loads(body)["replays"] if e["file"] == NAME]
        self.assertEqual(len(listed), 1, body[:300])
        self.assertEqual(listed[0]["bytes"], len(self.file))
        self.assertEqual(listed[0]["map"], "Draft Map ~test-0badf00d.lvl")

    def test_the_game_servers_own_door_sends_the_file_whole(self):
        status, headers, body = get(self.replay_port, "/replays/" + NAME)
        self.assertEqual(status, 200, "the replay door answered %d %r" % (status, body[:80]))
        self.assertEqual(headers["content-type"], "application/octet-stream")
        self.assertEqual(hashlib.sha256(body).hexdigest(), hashlib.sha256(self.file).hexdigest())

    def test_nginx_passes_the_file_on_whole(self):
        status, headers, body = self.rig.ask("GET", "/replays/" + NAME)
        self.assertEqual(status, 200, "through nginx: %d %r" % (status, body[:80]))
        self.assertEqual(len(body), len(self.file))
        self.assertEqual(hashlib.sha256(body).hexdigest(), hashlib.sha256(self.file).hexdigest())
        self.assertEqual(body[:8], b"\x89ARPL\r\n\x1a")
        self.assertEqual(body[body.index(b"tmap") + 8:][:MAP_BYTES], self.map_bytes)       # (the map is inside, exactly)


if __name__ == "__main__":
    unittest.main()
