#!/usr/bin/env python3
"""The activity curve over the timeline of the replay page (run by ./run_tests.sh --fast and by the CI): web/replay_activity.js and the record handler of web/history_page.js. docs/HEATMAPS.md "The activity curve" says
what it shows and where the data comes from (the replay pass, ants_replay/match_stats.hpp, ants_server/history_action.hpp).

The code is RUN (node, when it is installed): the field as the server writes it, damaged fields, the deaths-only curve of a record that has no action, the weights, the buckets, the curve and its label, the curve on
the page's timeline (a computer: nothing at rest and on while the pointer is over the timeline; a touch screen: faint at rest and solid while a finger is on it), the played part, the page's call and the record handler
of AntsHistory.replayStats, on a STRICT fake of the DOM (tests/scripts/web_activity_check.js: an element that throws on innerHTML, so that nothing a record carries can become markup). Nothing reads the source as
text. The mutants of the page's rules (the weights' order of importance, the deaths alone, hover only on a computer, the limits of a field) are in tests/data/replay_activity_mutants.json, run with tools/mutate.py.
The stylesheet's rule that the curve is hidden at rest on a computer, and the wiring of the two files into the game page, need a real browser (the pictures of the pull request were made in one);
test_web_image_files.py checks that a page loads both files and that the image carries them.
"""
import os
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_activity_check.js")
NODE = shutil.which("node")


@unittest.skipUnless(NODE, "node is not installed: the activity curve was NOT run (tests/scripts/web_activity_check.js)")
class TheCurve(unittest.TestCase):
    def test_the_curve_is_built_and_shown_from_the_data_of_a_record(self):
        done = subprocess.run([NODE, CHECK_JS, os.path.join(REPO, "web", "replay_activity.js"), os.path.join(REPO, "web", "history_page.js")], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"^ok: \d+ checks")


if __name__ == "__main__":
    unittest.main()
