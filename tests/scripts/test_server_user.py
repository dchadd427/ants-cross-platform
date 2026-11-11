#!/usr/bin/env python3
"""The game server runs as the user ants, which owns /results; no compose file runs a service as root or adds capabilities.

Read as text: Dockerfile.server makes the user ants, gives it the results folder and switches to it; every docker-compose*.yml
has no `user:` of root and no `cap_add`.
"""
import glob
import os
import re
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def code(path):
    with open(os.path.join(REPO, path), encoding="utf-8") as f:
        return "\n".join(l for l in f.read().splitlines() if not l.lstrip().startswith("#"))


class TheServerUser(unittest.TestCase):
    def test_the_image_makes_the_user_ants_and_gives_it_the_results_folder(self):
        text = code("Dockerfile.server")
        self.assertRegex(text, r"useradd [^\n]*\bants\b")
        self.assertRegex(text, r"chown ants /results\b")
        self.assertRegex(text, r"(?m)^USER ants\s*$")

    def test_no_compose_file_runs_a_service_as_root_or_adds_capabilities(self):
        files = sorted(glob.glob(os.path.join(REPO, "docker-compose*.yml")))
        self.assertGreaterEqual(len(files), 4)
        for path in files:
            text = code(os.path.relpath(path, REPO))
            self.assertNotIn("cap_add", text, path)
            self.assertNotRegex(text, r"(?m)^\s*user:\s*[\"']?(0|root)\b", path)


if __name__ == "__main__":
    unittest.main()
