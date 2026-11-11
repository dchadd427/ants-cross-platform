#!/usr/bin/env python3
"""Tests of tools/check_doc_move.py (the proof that a split of a document is a pure move), on small repositories made for each test: a good split passes, and every way of losing,
changing or breaking something fails."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(ROOT, "tools", "check_doc_move.py")

OLD = """# Big

Intro line.

## 1. First

First text, see [second](#2-second) and [other](OTHER.md#the-heading).

## 2. Second

Second text.
Second text.

## 3. Third

Third text with [a page](OTHER.md) in it.
"""
OTHER = "# Other\n\n## The heading\n\nSee [first](BIG.md#1-first) and [third](BIG.md#3-third).\n"
README = "[big](docs/BIG.md) [other](docs/OTHER.md)\n"

INDEX = """# Big

This page is an index.

Intro line.

## 1. First

First text, see [second](big/two.md#2-second) and [other](OTHER.md#the-heading).

| Part | Where |
|---|---|
| <a id="2-second"></a>Second | [two](big/two.md) |
| <a id="3-third"></a>Third | [two](big/two.md) |
"""
PART = """# Parts two

## 2. Second

Second text.
Second text.

## 3. Third

Third text with [a page](../OTHER.md) in it.
"""
OTHER_NEW = "# Other\n\n## The heading\n\nSee [first](BIG.md#1-first) and [third](big/two.md#3-third).\n"


@unittest.skipUnless(shutil.which("git"), "git is not installed")
class Move(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.addCleanup(self._tmp.cleanup)
        self.git("init", "-q")
        self.put("README.md", README)
        self.put("docs/BIG.md", OLD)
        self.put("docs/OTHER.md", OTHER)
        self.git("add", "-A")
        self.git("-c", "user.name=t", "-c", "user.email=t@example.org", "commit", "-q", "-m", "base")
        self.base = self.git("rev-parse", "HEAD").strip()

    def git(self, *args):
        return subprocess.run(["git", "-C", self.root] + list(args), check=True, capture_output=True, text=True).stdout

    def put(self, rel, text):
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)

    def good_split(self):
        self.put("docs/BIG.md", INDEX)
        self.put("docs/big/two.md", PART)
        self.put("docs/OTHER.md", OTHER_NEW)

    def run_tool(self, *args):
        done = subprocess.run([sys.executable, TOOL, self.base, "--root", self.root] + list(args), capture_output=True, text=True)
        return done.returncode, done.stdout, done.stderr

    def test_a_pure_move_passes_and_the_added_lines_are_shown(self):
        self.good_split()
        code, out, err = self.run_tool()
        self.assertEqual((code, out), (0, ""), out + err)
        self.assertIn("split: docs/BIG.md into 1 part(s): docs/big/two.md", err)
        self.assertIn("+ This page is an index.", err)
        self.assertNotIn("+ Second text.", err)

    def test_a_lost_line_fails(self):
        self.good_split()
        self.put("docs/big/two.md", PART.replace("Third text with [a page](../OTHER.md) in it.\n", ""))
        code, out, _ = self.run_tool()
        self.assertEqual(code, 1)
        self.assertIn("(a) 1 line(s) of docs/BIG.md", out)

    def test_a_changed_word_fails(self):
        self.good_split()
        self.put("docs/big/two.md", PART.replace("Second text.\nSecond text.", "Second text.\nSecond words."))
        code, out, _ = self.run_tool()
        self.assertEqual(code, 1)
        self.assertIn("(a) 1 line(s)", out)

    def test_a_line_that_was_twice_and_is_once_fails(self):
        self.good_split()
        self.put("docs/big/two.md", PART.replace("Second text.\nSecond text.", "Second text."))
        self.assertIn("(a)", self.run_tool()[1])

    def test_a_changed_link_target_alone_is_not_a_changed_line(self):
        self.good_split()
        self.put("docs/big/two.md", PART.replace("(../OTHER.md)", "(../../docs/OTHER.md)"))
        self.assertEqual(self.run_tool()[0], 0)

    def test_another_page_that_changes_more_than_links_fails(self):
        self.good_split()
        self.put("docs/OTHER.md", OTHER_NEW + "A new sentence.\n")
        code, out, _ = self.run_tool()
        self.assertEqual(code, 1)
        self.assertIn("(b) docs/OTHER.md", out)

    def test_a_link_that_no_longer_resolves_to_its_heading_fails(self):
        self.good_split()
        self.put("docs/OTHER.md", OTHER_NEW.replace("big/two.md#3-third", "big/two.md#3-nothird"))
        out = self.run_tool()[1]
        self.assertIn("(c)", out)
        self.assertIn("names no heading", out)

    def test_a_link_to_a_heading_that_was_lost_from_the_index_fails(self):
        self.good_split()
        self.put("docs/BIG.md", INDEX.replace("[second](big/two.md#2-second)", "[second](big/two.md#2-gone)"))
        out = self.run_tool()[1]
        self.assertIn("names no heading", out)
        self.assertIn("(c) 1 link(s) from (split) to (split)", out)

    def test_a_heading_that_changed_its_text_fails_the_links_to_it(self):
        self.good_split()
        self.put("docs/big/two.md", PART.replace("## 3. Third", "## 3. Third part"))
        out = self.run_tool()[1]
        self.assertIn('heading "3. Third"', out)

    def test_nothing_changed_and_a_bad_document_are_usage_errors(self):
        self.assertEqual(self.run_tool()[0], 2)
        self.good_split()
        self.assertEqual(self.run_tool("--doc", "docs/OTHER.md")[0], 1)            # (the wrong page named: the proof fails)
        self.assertEqual(self.run_tool("--doc", "docs/NOPE.md")[0], 2)
        self.assertEqual(subprocess.run([sys.executable, TOOL], capture_output=True).returncode, 2)

    def test_an_untracked_part_counts(self):
        self.good_split()                                                         # (nothing was added to git: the parts are new files of the checkout)
        self.assertEqual(self.run_tool()[0], 0)


if __name__ == "__main__":
    unittest.main()
