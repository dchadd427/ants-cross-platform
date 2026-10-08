#!/usr/bin/env python3
"""The lobby page's client of network protocol 16 (web/front/lobby_net.js; run by ./run_tests.sh --fast and by the CI). The front page becomes the game's lobby (it waits in a lobby room of the server,
and the host arranges the colours, the bots and the teams for everybody); this is the page's side of the talk, a script that a browser loads and that a screen will use, written and checked before any
screen: the codec of every message of the protocol and the client that keeps the page's seat.

  - tests/scripts/web_lobby_net_check.js holds the codec to the bytes that the C++ encoders write (tests/data/lobby_messages.txt, which test_lockstep N2.111 holds the encoders to: a layout that
    changes in C++ changes the file, and this check then fails until the script follows), and to what it must refuse (a message cut at every length, a name or a map that is no name);
  - tests/scripts/web_lobby_client_check.js runs the client against a scripted server and a fake browser (sockets and timers): the Hello it sends, the Welcome and Room messages it takes in, the answers
    to the server's pings, what changed between two Room messages (who joined, left, was renamed, was moved, changed places, leads now), the requests that only the leader may make and the bytes they
    put on the wire (a colour move's guard is the room's seating hash), the way back with the key after a lost link (the waits grow, a link that says nothing is given up), what each refusal means, and
    the entry that the game page reads to take the seat over;
  - the client against a real server is run by tests/scripts/test_ants_server.sh (tests/scripts/web_lobby_server_check.js), which starts the servers; the same script hands a lobby over to two real games
    (tests/scripts/web_lobby_handoff_check.js: START, the pages go, a game of each name takes its seat with the page's key, the match runs).
The node checks need node; without it these tests are skipped and say so.
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPTS = os.path.join(REPO, "tests", "scripts")
MODULE = os.path.join(REPO, "web", "front", "lobby_net.js")
GOLDEN = os.path.join(REPO, "tests", "data", "lobby_messages.txt")
NODE = shutil.which("node")


def run_node(*args):
    done = subprocess.run([NODE, *args], capture_output=True, text=True, timeout=120)
    return done.returncode, done.stdout + done.stderr


class TheFiles(unittest.TestCase):
    def test_the_script_is_a_plain_script_with_no_dependency(self):
        with open(MODULE, encoding="utf-8") as f:
            text = f.read()
        self.assertNotRegex(text, r"(?m)^\s*(import|export)\s", "a page loads it with a script tag: no module syntax")
        self.assertNotRegex(text, r"\brequire\(", "no dependency")
        self.assertIn("module.exports", text)                                  # (node loads it for the checks; a page gets the global)
        self.assertIn("root.AntsLobbyNet = api", text)

    def test_the_golden_file_has_a_line_for_every_kind_the_checks_name(self):
        with open(GOLDEN, encoding="utf-8") as f:
            lines = [ln for ln in f.read().splitlines() if ln and not ln.startswith("#")]
        kinds = {ln.split(" ")[0] for ln in lines}
        for kind in ("hello", "leave", "pong", "ping", "seatmove", "plan", "name", "remove", "startrequest", "chat", "welcome", "reject", "room", "other", "hash"):
            self.assertIn(kind, kinds, "tests/data/lobby_messages.txt has no line of kind " + kind)
        for ln in lines:
            if ln.startswith("hash "):
                self.assertRegex(ln, r"^hash \S+ [0-9a-f]{8}$")
            else:
                self.assertRegex(ln, r"^\S+ \S+ ([0-9a-f][0-9a-f])+$", ln[:60])


@unittest.skipUnless(NODE, "node is not installed: the lobby page's codec and client were NOT run (tests/scripts/web_lobby_net_check.js, web_lobby_client_check.js)")
class TheChecksThatNodeRuns(unittest.TestCase):
    def test_the_codec_reads_and_writes_the_bytes_of_the_cpp_encoders(self):
        status, out = run_node(os.path.join(SCRIPTS, "web_lobby_net_check.js"), MODULE, GOLDEN)
        self.assertEqual(status, 0, out)
        self.assertRegex(out, r"\b[1-9][0-9]* checks, 0 failed")

    def test_the_client_keeps_the_pages_seat_against_a_scripted_server(self):
        status, out = run_node(os.path.join(SCRIPTS, "web_lobby_client_check.js"), MODULE)
        self.assertEqual(status, 0, out)
        self.assertRegex(out, r"\b[1-9][0-9]* checks, 0 failed")

    def test_a_changed_byte_of_the_golden_file_fails_the_codec_check(self):
        """the check is not vacuous: one byte of one message changed (the colour move's type) must be seen"""
        with open(GOLDEN, encoding="utf-8") as f:
            text = f.read()
        changed, count = re.subn(r"(?m)^(seatmove \S+ )1f", r"\g<1>1e", text, count=1)
        self.assertEqual(count, 1)
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "lobby_messages.txt")
            with open(path, "w", encoding="utf-8") as f:
                f.write(changed)
            status, out = run_node(os.path.join(SCRIPTS, "web_lobby_net_check.js"), MODULE, path)
        self.assertNotEqual(status, 0)
        self.assertIn("FAIL", out)


if __name__ == "__main__":
    unittest.main()
