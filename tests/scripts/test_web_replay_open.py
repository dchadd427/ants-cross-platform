#!/usr/bin/env python3
"""Opening a replay FILE of your own on the website (run by ./run_tests.sh --fast and by the CI): the "Got a replay file?" strip of the Matches page (web/matches.html, web/matches.css), the player's own
mode (web/shell.html at play.html?local=<id>) and the code that they share (web/replay_page.js). docs/REPLAYS.md "A file of your own" says what it does and why nothing leaves the device.

  - the shared code is RUN (node, when it is installed: tests/scripts/web_replay_open_check.js): the cheap questions that are asked before the game is loaded for a file (the size from File.size before a
    byte is read, and again on the bytes; the 8 bytes that open a replay; a HEAD chunk after them, at most 4096 bytes long, with the right checksum) on the real file of tests/data/ and on every cut and every
    flipped bit of its head; the name of the file as text; the size in words;
  - tests/data/replay_open_mutants.json is the list of changes to the page and the code that the checks must notice (tools/mutate.py runs it with `--keep-going --jobs 1`; the class at the end of this file
    runs the same list in the quick tier on a copy of the files): every one is a way to lose a promise (the size is not checked, the checksum is not, a drop that is not taken over, a file that is sent to the
    server, a record that is kept for ever ...).
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NODE = shutil.which("node")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_replay_open_check.js")
FIXTURE = os.path.join(REPO, "tests", "data", "replay_bots_tiny.antsrep")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_node(script, *args):
    return subprocess.run([NODE, CHECK_JS, script] + list(args), capture_output=True, text=True, timeout=120)


@unittest.skipUnless(NODE, "node is not installed: the code that opens a file of your own was NOT run (tests/scripts/web_replay_open_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_size_the_magic_the_head_and_the_checksum_of_a_file_the_name_and_the_size_in_words(self):
        done = run_node(os.path.join(REPO, "web", "replay_page.js"), FIXTURE)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r" 0 failures")


class TheFixture(unittest.TestCase):
    """tests/data/replay_bots_tiny.antsrep: a real recording that bot_arena --save-replays wrote (a match on Tiny between four computer players, 6 minutes), 3 KB. It holds nothing of a person: the
    names are "Bot (Medium)", the build is a commit's short name. A byte of it must never be changed by a line-end conversion (.gitattributes)."""

    def test_it_is_a_whole_replay_of_the_size_that_the_cases_assume(self):
        with open(FIXTURE, "rb") as f:
            data = f.read()
        self.assertEqual(data[:8], bytes([0x89, 0x41, 0x52, 0x50, 0x4C, 0x0D, 0x0A, 0x1A]))
        self.assertEqual(data[8:12], b"HEAD")
        self.assertTrue(2000 < len(data) < 6000, len(data))
        self.assertIn(b"ENDS", data[-40:])                                                              # (a whole match: its last chunk)
        self.assertNotIn(b"tmap", data)                                                                 # (it plays on a map that the game has)

    def test_git_leaves_its_bytes_alone(self):
        self.assertRegex(read(".gitattributes"), r"(?m)^tests/data/\*\.antsrep\s+-text$")

    def test_it_names_nobody(self):
        with open(FIXTURE, "rb") as f:
            data = f.read()
        head = data[16:16 + int.from_bytes(data[12:16], "little")].decode("latin-1")             # (every text of the file is a field of its head: the other chunks are numbers)
        self.assertIn("Bot (Medium)", head)
        for word in ("/home", "/root", "/Users", "@", "password", "token", "C:\\"):
            self.assertFalse(word in head, "the head holds %r" % word)


if __name__ == "__main__":
    unittest.main()
