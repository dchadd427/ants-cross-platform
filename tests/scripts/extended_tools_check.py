#!/usr/bin/env python3
"""The tools on an Extended map (suite 2.21.1 of ./run_tests.sh; docs/TESTING.md): bot_arena, map_sweep and replay_tool play a 100 x 100 Extended map (rules 1, 100 ants in the middle,
a hill in each corner) with --extended, and without it they say that the file does not load (a map of the network is a Classic map). The map is made by the test of the bots (test_ai
writes it when ANTS_CROWD_FILE names a file), so no map file is kept in the repository.

bot_arena: two runs of 600 ticks with the same seed give the same hash, and its replay check passes; the replay that --save-replays keeps is verified by replay_tool with --extended and
is refused without it, in the one plain sentence (Extended maps can't be played online yet), with the hint to use --extended. map_sweep loads and plays the map with --extended and reports
a load failure without it. Exit 0: all passed (or a program is not built: the check says so and passes, as the other program checks do).
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
BUILD = os.path.join(REPO, os.environ.get("BUILD_DIR", "build"))
TEST_AI = os.path.join(BUILD, "tests", "test_ai", "test_ai")
ARENA = os.path.join(BUILD, "bot_arena")
SWEEP = os.path.join(BUILD, "map_sweep")
REPLAY = os.path.join(BUILD, "replay_tool")
SENTENCE = "Extended maps can't be played online yet"


def run(args, cwd=REPO, env=None, timeout=300):
    return subprocess.run(args, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout)


@unittest.skipUnless(all(os.path.isfile(p) for p in (TEST_AI, ARENA, SWEEP, REPLAY)), "test_ai, bot_arena, map_sweep and replay_tool are not built (run_tests.sh builds them)")
class ExtendedTools(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="ants_ext_tools_")
        cls.maps = os.path.join(cls.tmp.name, "maps")
        cls.reps = os.path.join(cls.tmp.name, "replays")
        os.makedirs(cls.maps)
        os.makedirs(cls.reps)
        cls.map = os.path.join(cls.maps, "crowd.lvl")
        env = dict(os.environ, ANTS_CROWD_FILE=cls.map, ANTS_TEST_FILTER="AI31.1")
        made = run([TEST_AI], env=env)
        assert made.returncode == 0 and os.path.isfile(cls.map), made.stdout[-500:] + made.stderr[-500:]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def arena(self, *extra):
        return run([ARENA, "--map", self.map, "--seeds", "3", "--ticks", "600", "--no-wall-time"] + list(extra))

    def test_bot_arena_plays_the_map_with_extended_twice_to_one_hash(self):
        one, two = self.arena("--extended", "--replay-check"), self.arena("--extended", "--replay-check")
        self.assertEqual(one.returncode, 0, one.stdout + one.stderr)
        self.assertIn("replay ok", one.stdout)
        hashes = [re.search(r"hash ([0-9a-f]{16})", r.stdout).group(1) for r in (one, two)]
        self.assertEqual(hashes[0], hashes[1])

    def test_bot_arena_without_extended_does_not_load_the_map(self):
        r = self.arena()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("does not load", r.stdout + r.stderr)
        self.assertNotIn("hash ", r.stdout)

    def test_map_sweep_plays_it_with_extended_and_reports_a_load_failure_without(self):
        with_flag = run([SWEEP, self.maps, "--extended", "--ticks", "400"])
        self.assertEqual(with_flag.returncode, 0, with_flag.stdout[-800:] + with_flag.stderr[-800:])
        self.assertIn("100x100", with_flag.stdout)
        self.assertNotIn("LOADFAIL", with_flag.stdout)
        without = run([SWEEP, self.maps, "--ticks", "400"])
        self.assertNotEqual(without.returncode, 0)
        self.assertIn("LOADFAIL", without.stdout)

    def test_replay_tool_verifies_the_replay_of_the_map_with_extended_only(self):
        saved = self.arena("--extended", "--save-replays", self.reps)
        self.assertEqual(saved.returncode, 0, saved.stdout + saved.stderr)
        files = [f for f in os.listdir(self.reps) if f.endswith(".antsrep")]
        self.assertEqual(len(files), 1)
        replay = os.path.join(self.reps, files[0])
        ok = run([REPLAY, "verify", replay, "--maps-dir", self.maps, "--extended"])
        self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)
        self.assertIn("match", ok.stdout)
        refused = run([REPLAY, "verify", replay, "--maps-dir", self.maps])
        self.assertEqual(refused.returncode, 1)
        self.assertIn(SENTENCE, refused.stderr)
        self.assertIn("--extended", refused.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
