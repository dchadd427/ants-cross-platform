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
        self.assertIn("(a) docs/BIG.md: the section", out)

    def test_a_changed_word_fails(self):
        self.good_split()
        self.put("docs/big/two.md", PART.replace("Second text.\nSecond text.", "Second text.\nSecond words."))
        code, out, _ = self.run_tool()
        self.assertEqual(code, 1)
        self.assertIn('(a) docs/BIG.md: the section "## 2. Second"', out)

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


RULES = """# Rules

Intro.

## Allowed

- one
- two

| a | b |
|---|---|
| 1 | 2 |

## Forbidden

- three
- four

## Limits

First limits text.

## More

### Limits

Second limits text.
handlers[i](first, second)
See [first](#limits), [second](#limits-1), [site](https://example.org/a), [other](OTHER2.md#x).

```text
code line one
code line two
```
"""
RULES_OTHER = "# Other\n\n## X\n\ntext [r](RULES.md#allowed)\n\nmore\n"
RULES_INDEX = """# Rules

This page is an index.

Intro.

| Part | Where |
|---|---|
| Rules | [p](rules/p.md) |
"""
RULES_PART = """# Rules, part one

## Allowed

- one
- two

| a | b |
|---|---|
| 1 | 2 |

## Forbidden

- three
- four

## Limits

First limits text.

## More

### Limits

Second limits text.
handlers[i](first, second)
See [first](p.md#limits), [second](p.md#limits-1), [site](https://example.org/a), [other](../OTHER2.md#x).

```text
code line one
code line two
```
"""
RULES_OTHER_NEW = RULES_OTHER.replace("(RULES.md#allowed)", "(rules/p.md#allowed)")


@unittest.skipUnless(shutil.which("git"), "git is not installed")
class Strict(unittest.TestCase):
    """What a pure move is: every section of the old page, in its own words and its own order, blank lines and links included, in one place under its own heading."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.addCleanup(self._tmp.cleanup)
        self.git("init", "-q")
        self.put("README.md", "[r](docs/RULES.md) [o](docs/OTHER2.md)\n")
        self.put("docs/RULES.md", RULES)
        self.put("docs/OTHER2.md", RULES_OTHER)
        self.git("add", "-A")
        self.git("-c", "user.name=t", "-c", "user.email=t@example.org", "commit", "-q", "-m", "base")
        self.base = self.git("rev-parse", "HEAD").strip()

    git = Move.git
    put = Move.put

    def run_split(self, index=RULES_INDEX, part=RULES_PART, other=RULES_OTHER_NEW):
        self.put("docs/RULES.md", index)
        self.put("docs/rules/p.md", part)
        self.put("docs/OTHER2.md", other)
        done = subprocess.run([sys.executable, TOOL, self.base, "--root", self.root], capture_output=True, text=True)
        return done.returncode, done.stdout, done.stderr

    def changed(self, old, new):
        assert old in RULES_PART, old
        return RULES_PART.replace(old, new)

    def attack(self, old, new, expect):
        code, out, err = self.run_split(part=self.changed(old, new))
        self.assertEqual(code, 1, "it passed: " + err)
        self.assertIn(expect, out)

    def test_the_good_split_passes(self):
        code, out, err = self.run_split()
        self.assertEqual((code, out), (0, ""), out + err)

    def test_two_lines_swapped_inside_a_section_fail(self):
        self.attack("- one\n- two\n", "- two\n- one\n", '"## Allowed"')
        self.attack("- one\n- two\n", "- two\n- one\n", "the first difference, old / new: '- one' / '- two'")

    def test_a_line_moved_into_the_next_section_fails(self):
        code, out, _ = self.run_split(part=RULES_PART.replace("- two\n\n|", "\n|").replace("- three\n", "- two\n- three\n"))
        self.assertEqual(code, 1)
        self.assertIn('"## Allowed"', out)                                                # (it leaves Allowed: that section is no longer what it was)

    def test_a_line_that_is_copied_into_another_section_fails_as_a_duplicate(self):
        code, out, _ = self.run_split(part=RULES_PART.replace("- three\n", "- one\n- three\n"))
        self.assertEqual(code, 1)
        self.assertIn("more than once", out)

    def test_two_bullets_swapped_between_two_sections_fail(self):
        code, out, _ = self.run_split(part=RULES_PART.replace("- two", "- TMP").replace("- three", "- two").replace("- TMP", "- three"))
        self.assertEqual(code, 1)
        self.assertIn('"## Allowed"', out)
        self.assertIn('"## Forbidden"', out)

    def test_a_section_under_another_heading_fails(self):
        code, out, _ = self.run_split(part=RULES_PART.replace("## Allowed", "## TMP").replace("## Forbidden", "## Allowed").replace("## TMP", "## Forbidden"))
        self.assertEqual(code, 1)

    def test_a_line_moved_out_of_its_code_fence_fails(self):
        self.attack("code line one\ncode line two\n```", "code line one\n```\ncode line two", '"### Limits"')

    def test_table_rows_in_another_order_fail(self):
        code, out, _ = self.run_split(part=RULES_PART.replace("|---|---|\n| 1 | 2 |", "| 1 | 2 |\n|---|---|"))
        self.assertEqual(code, 1)
        self.assertIn('"## Allowed"', out)

    def test_a_moved_line_that_stays_in_the_index_fails_as_a_duplicate(self):
        code, out, _ = self.run_split(index=RULES_INDEX + "\n- one\n")
        self.assertEqual(code, 1)
        self.assertIn("(a) 1 line(s) of docs/RULES.md are in the new set more than once", out)

    def test_a_section_left_in_both_files_fails_as_a_duplicate(self):
        code, out, _ = self.run_split(index=RULES_INDEX + "\n## Forbidden\n\n- three\n- four\n")
        self.assertEqual(code, 1)
        self.assertIn("more than once", out)

    def test_a_line_written_twice_fails(self):
        code, out, _ = self.run_split(part=RULES_PART.replace("- four\n", "- four\n- four\n"))
        self.assertEqual(code, 1)
        self.assertIn("more than once", out)

    def test_a_removed_blank_line_fails(self):
        self.attack("- two\n\n| a | b |", "- two\n| a | b |", '"## Allowed"')

    def test_two_paragraphs_joined_fail(self):
        self.attack("Second limits text.\nhandlers", "Second limits text.\n\nhandlers", '"### Limits"')

    def test_a_blank_line_removed_from_another_page_fails(self):
        code, out, _ = self.run_split(other=RULES_OTHER_NEW.replace("\n\nmore", "\nmore"))
        self.assertEqual(code, 1)
        self.assertIn("(b) docs/OTHER2.md", out)

    def test_the_targets_of_two_links_swapped_fail(self):
        self.attack("[first](p.md#limits), [second](p.md#limits-1)", "[first](p.md#limits-1), [second](p.md#limits)", '"### Limits"')

    def test_a_link_aimed_at_the_other_of_two_equal_headings_fails(self):
        self.attack("[second](p.md#limits-1)", "[second](p.md#limits)", '"### Limits"')

    def test_a_changed_outside_address_fails(self):
        self.attack("https://example.org/a", "https://example.org/b", '"### Limits"')

    def test_the_arguments_of_a_call_that_looks_like_a_link_are_not_blanked(self):
        self.attack("handlers[i](first, second)", "handlers[i](second, first)", '"### Limits"')

    def test_a_link_in_another_page_that_names_a_section_that_is_not_there_fails(self):
        code, out, _ = self.run_split(other=RULES_OTHER)
        self.assertEqual(code, 1)

    def test_a_heading_of_another_level_fails(self):
        self.attack("### Limits", "## Limits", '"### Limits"')

    def test_an_old_page_that_is_deleted_fails_with_that_message(self):
        self.put("docs/rules/p.md", RULES_PART)
        self.put("docs/OTHER2.md", RULES_OTHER_NEW)
        os.remove(os.path.join(self.root, "docs", "RULES.md"))
        done = subprocess.run([sys.executable, TOOL, self.base, "--root", self.root], capture_output=True, text=True)
        self.assertEqual(done.returncode, 1)
        self.assertIn("(b) docs/RULES.md was deleted", done.stdout)

    def test_the_text_before_the_first_heading_is_checked_too(self):
        self.put("docs/PRE.md", "Preface line.\n\n# Pre\n\n## S1\n\ntext one\n\n## S2\n\ntext two\n")
        self.git("add", "-A")
        self.git("-c", "user.name=t", "-c", "user.email=t@example.org", "commit", "-q", "-m", "pre")
        self.base = self.git("rev-parse", "HEAD").strip()
        self.put("docs/pre/s2.md", "# Part\n\n## S2\n\ntext two\n")

        def run(index):
            self.put("docs/PRE.md", index)
            return subprocess.run([sys.executable, TOOL, self.base, "--root", self.root], capture_output=True, text=True)
        good = run("Preface line.\n\n# Pre\n\n## S1\n\ntext one\n")
        self.assertEqual((good.returncode, good.stdout), (0, ""), good.stderr)
        bad = run("Preface lines.\n\n# Pre\n\n## S1\n\ntext one\n")
        self.assertEqual(bad.returncode, 1)
        self.assertIn("before the first heading", bad.stdout)

    def test_the_lines_that_are_new_are_shown_and_a_table_rule_is_not_a_duplicate(self):
        code, out, err = self.run_split()
        self.assertEqual(code, 0)
        self.assertIn("+ This page is an index.", err)
        self.assertIn("+ | Rules | [p](<(split) file>) |", err)


if __name__ == "__main__":
    unittest.main()
