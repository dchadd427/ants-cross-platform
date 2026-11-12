#!/usr/bin/env python3
"""Tests of tools/check_doc_move.py (the proof that a split of a document is a pure move), on small repositories made for each test: a good split passes, and every way of losing,
changing or breaking something fails."""
import os
import re
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

This page is an index of [the parts](big/two.md).

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
        self.assertIn("+ docs/BIG.md:3: This page is an index of", err)
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

This page is an index of [the parts](rules/p.md).

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

    def changed_copy_fails(self, old, new, expect):
        code, out, err = self.run_split(part=self.changed(old, new))
        self.assertEqual(code, 1, "it passed: " + err)
        self.assertIn(expect, out)

    def test_the_good_split_passes(self):
        code, out, err = self.run_split()
        self.assertEqual((code, out), (0, ""), out + err)

    def test_two_lines_swapped_inside_a_section_fail(self):
        self.changed_copy_fails("- one\n- two\n", "- two\n- one\n", '"## Allowed"')
        self.changed_copy_fails("- one\n- two\n", "- two\n- one\n", "the first difference, old / new: '- one' / '- two'")

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
        self.changed_copy_fails("code line one\ncode line two\n```", "code line one\n```\ncode line two", '"### Limits"')

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
        self.changed_copy_fails("- two\n\n| a | b |", "- two\n| a | b |", '"## Allowed"')

    def test_two_paragraphs_joined_fail(self):
        self.changed_copy_fails("Second limits text.\nhandlers", "Second limits text.\n\nhandlers", '"### Limits"')

    def test_a_blank_line_removed_from_another_page_fails(self):
        code, out, _ = self.run_split(other=RULES_OTHER_NEW.replace("\n\nmore", "\nmore"))
        self.assertEqual(code, 1)
        self.assertIn("(b) docs/OTHER2.md", out)

    def test_the_targets_of_two_links_swapped_fail(self):
        self.changed_copy_fails("[first](p.md#limits), [second](p.md#limits-1)", "[first](p.md#limits-1), [second](p.md#limits)", '"### Limits"')

    def test_a_link_aimed_at_the_other_of_two_equal_headings_fails(self):
        self.changed_copy_fails("[second](p.md#limits-1)", "[second](p.md#limits)", '"### Limits"')

    def test_a_changed_outside_address_fails(self):
        self.changed_copy_fails("https://example.org/a", "https://example.org/b", '"### Limits"')

    def test_the_arguments_of_a_call_that_looks_like_a_link_are_not_blanked(self):
        self.changed_copy_fails("handlers[i](first, second)", "handlers[i](second, first)", '"### Limits"')

    def test_a_link_in_another_page_that_names_a_section_that_is_not_there_fails(self):
        code, out, _ = self.run_split(other=RULES_OTHER)
        self.assertEqual(code, 1)

    def test_a_heading_of_another_level_fails(self):
        self.changed_copy_fails("### Limits", "## Limits", '"### Limits"')

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
        self.assertIn("+ docs/RULES.md:3: This page is an index of", err)
        self.assertIn("+ docs/RULES.md:9: | Rules | [p](rules/p.md) |", err)


TREE = """# Net

Intro.

## About

About text.

## Server

Server intro.

### Sizes

Server sizes text.

### Timeouts

Server timeouts text.

## Client

Client intro.

### Sizes

Client sizes text.

### Timeouts

Client timeouts text.

## Notes

Notes text.
"""
TREE_INDEX = "# Net\n\nIntro.\n\nSee [the parts](net/p.md).\n\n## About\n\nAbout text.\n"
TREE_PART = "# Net, the parts\n\nSee [the index](../NET.md).\n\n" + TREE[TREE.index("## Server"):]
SIZES_SERVER = "### Sizes\n\nServer sizes text.\n\n"
SIZES_CLIENT = "### Sizes\n\nClient sizes text.\n\n"
TIMEOUTS_SERVER = "### Timeouts\n\nServer timeouts text.\n\n"
TIMEOUTS_CLIENT = "### Timeouts\n\nClient timeouts text.\n\n"
NAV = "See [the index](../NET.md)."


@unittest.skipUnless(shutil.which("git"), "git is not installed")
class Tree(unittest.TestCase):
    """Sections that keep their order and their parent heading, and the few lines that a split may add."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.addCleanup(self._tmp.cleanup)
        self.git("init", "-q")
        self.put("README.md", "[n](docs/NET.md)\n")
        self.put("docs/NET.md", TREE)
        self.git("add", "-A")
        self.git("-c", "user.name=t", "-c", "user.email=t@example.org", "commit", "-q", "-m", "base")
        self.base = self.git("rev-parse", "HEAD").strip()

    git = Move.git
    put = Move.put

    def run_split(self, index=TREE_INDEX, part=TREE_PART, extra=None):
        self.put("docs/NET.md", index)
        self.put("docs/net/p.md", part)
        for rel, text in (extra or {}).items():
            self.put(rel, text)
        done = subprocess.run([sys.executable, TOOL, self.base, "--root", self.root], capture_output=True, text=True)
        return done.returncode, done.stdout, done.stderr

    def fails(self, expect, index=TREE_INDEX, part=TREE_PART, extra=None):
        code, out, err = self.run_split(index, part, extra)
        self.assertEqual(code, 1, "it passed: " + err)
        self.assertIn(expect, out)

    def in_part(self, old, new):
        assert old in TREE_PART, old
        return TREE_PART.replace(old, new)

    def test_the_good_split_passes_and_so_do_two_parts(self):
        code, out, err = self.run_split()
        self.assertEqual((code, out), (0, ""), out + err)
        cut = TREE_PART.index("## Client")
        code, out, err = self.run_split(part=TREE_PART[:cut], extra={"docs/net/q.md": "# Net, more\n\n" + NAV + "\n\n" + TREE_PART[cut:]})
        self.assertEqual((code, out), (0, ""), out + err)

    def test_two_subsections_swapped_between_their_parents_fail(self):
        self.fails("stands before the section", part=self.in_part(SIZES_SERVER, "@@").replace(SIZES_CLIENT, SIZES_SERVER).replace("@@", SIZES_CLIENT).replace(
            "Server sizes text.\n\n### Timeouts\n\nServer timeouts", "Client sizes text.\n\n### Timeouts\n\nServer timeouts"))

    def test_a_subsection_moved_to_the_other_parent_fails(self):
        self.fails("is not under its parent", part=self.in_part(TIMEOUTS_CLIENT, "").replace(TIMEOUTS_SERVER, TIMEOUTS_SERVER + TIMEOUTS_CLIENT))

    def test_a_subsection_moved_to_the_end_under_the_last_section_fails(self):
        self.fails("is not under its parent", part=self.in_part(TIMEOUTS_SERVER, "") + "\n" + TIMEOUTS_SERVER)

    def test_a_section_with_its_intro_moved_up_leaves_its_neighbors_under_it_fails(self):
        self.fails("is not under its parent", part=self.in_part("## Client\n\nClient intro.\n\n", "").replace(SIZES_SERVER, "## Client\n\nClient intro.\n\n" + SIZES_SERVER))

    def test_the_sections_of_a_part_in_reverse_order_fail(self):
        head, *chunks = re.split(r"(?m)^(?=## )", TREE_PART)
        self.fails("stands before the section", part=head + "\n".join(c.rstrip("\n") + "\n" for c in reversed(chunks)))

    def test_the_text_before_the_first_heading_moved_under_a_heading_fails(self):
        self.put("docs/PRE.md", "Preface line.\n\n# Pre\n\n## S1\n\ntext one\n\n## S2\n\ntext two\n")
        self.git("add", "-A")
        self.git("-c", "user.name=t", "-c", "user.email=t@example.org", "commit", "-q", "-m", "pre")
        self.base = self.git("rev-parse", "HEAD").strip()
        self.put("docs/pre/s2.md", "# Part\n\n## S2\n\ntext two\n")
        self.put("docs/PRE.md", "# Pre\n\nPreface line.\n\n## S1\n\ntext one\n")
        done = subprocess.run([sys.executable, TOOL, self.base, "--root", self.root, "--doc", "docs/PRE.md"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 1)
        self.assertIn("the section before the first heading (line 1", done.stdout)

    def test_a_sentence_under_a_heading_fails(self):
        self.fails("net/p.md:", part=self.in_part("## Server\n\nServer intro.", "## Server\n\nEverything below is out of date.\n\nServer intro."))

    def test_a_sentence_after_a_body_fails(self):
        self.fails("inside a part", part=self.in_part("Server intro.\n", "Server intro.\n\nNext words.\n"))
        self.fails("inside a part", part=TREE_PART + "\nA last sentence.\n")

    def test_a_navigation_line_after_a_moved_body_in_a_part_fails(self):
        self.fails("inside a part", part=TREE_PART + "\n" + NAV + "\n")

    def test_a_new_heading_above_a_moved_one_fails(self):
        self.fails("a heading that is not", part=self.in_part("## Server", "## Deprecated\n\n" + NAV + "\n\n## Server"))

    def test_a_new_section_in_a_part_fails(self):
        self.fails("net/p.md:", part=TREE_PART + "\n## Extra\n\nMore text.\n")

    def test_a_body_wrapped_in_a_comment_or_a_fence_or_details_fails(self):
        self.fails("a fence, comment or details mark", part=self.in_part("Notes text.", "<!--\nNotes text.\n-->"))
        self.fails("a fence, comment or details mark", part=self.in_part("Notes text.", "```text\nNotes text.\n```"))
        self.fails("a fence, comment or details mark", part=self.in_part("Notes text.", "<details>\nNotes text.\n</details>"))

    def test_a_mark_on_a_line_that_looks_like_navigation_fails(self):
        for mark in ("<!-- %s -->" % NAV, "```" + NAV, "<details>" + NAV, "<summary>" + NAV):
            self.fails("a fence, comment or details mark", part=self.in_part("## Server", mark + "\n\n## Server") if False else TREE_PART + "\n" + mark + "\n")

    def test_a_navigation_line_at_the_top_of_a_part_passes_and_a_bad_one_fails(self):
        code, out, err = self.run_split(part=self.in_part(NAV, NAV + "\n\n- [back](../NET.md#about)"))
        self.assertEqual((code, out), (0, ""), out + err)
        self.fails("text that is not a title", part=self.in_part(NAV, NAV + " " + "More words. " * 30))
        self.fails("text that is not a title", part=self.in_part(NAV, "See [nowhere](../NOPE.md)."))
        self.fails("text that is not a title", part=self.in_part(NAV, "See [a site](https://example.org/x)."))
        self.fails("text that is not a title", part=self.in_part(NAV, "A sentence with no link."))

    def test_a_second_title_in_a_part_fails(self):
        self.fails("a heading that is not", part=self.in_part(NAV, NAV + "\n\n# A second title"))

    def test_a_new_page_that_is_no_part_of_the_move_fails(self):
        self.fails("docs/EXTRA.md:", extra={"docs/EXTRA.md": "# Extra\n\nSome new sentence.\n"})

    def test_the_index_may_add_a_moved_to_line_a_table_and_a_name_for_it(self):
        moved = TREE_INDEX + "\nServer and the rest moved to [the part](net/p.md).\n"
        self.assertEqual(self.run_split(index=moved)[0], 0)
        table = "\n## Where it went\n\n| Part | Where |\n|---|---|\n| <a id=\"server\"></a>Server | [p](net/p.md#server) |\n| Notes | [p](net/p.md#notes) |\n"
        code, out, err = self.run_split(index=TREE_INDEX + table)
        self.assertEqual((code, out), (0, ""), out + err)
        self.assertIn("+ docs/NET.md:", err)

    def test_the_index_fails_for_a_sentence_a_bad_table_or_a_lone_heading(self):
        self.fails("text that is not a title", index=TREE_INDEX + "\nThe rest is out of date.\n")
        self.fails("a table that is not", index=TREE_INDEX + "\n| Part | Where |\n|---|---|\n| Server | nowhere |\n")
        self.fails("a table that is not", index=TREE_INDEX + "\n| Part | Where |\n| Server | [p](net/p.md) |\n")
        self.fails("a heading that is not", index=TREE_INDEX + "\n## Deprecated\n")
        self.fails("a heading that is not", index=TREE_INDEX + "\n# A second title\n\n| Part | Where |\n|---|---|\n| Server | [p](net/p.md) |\n")
        self.fails("a table that is not", index=TREE_INDEX + "\n| Part | Where |\n| Server | [p](net/p.md) |\n| Notes | [p](net/p.md) |\n")
        self.fails("a table in a part", part=TREE_PART.replace(NAV, NAV + "\n\n| Part | Where |\n|---|---|\n| Server | [i](../NET.md) |"))

    def test_the_index_fails_for_a_line_between_a_kept_heading_and_its_body(self):
        self.fails("between a heading and its body", index=TREE_INDEX.replace("## About\n\n", "## About\n\nMoved: [the part](net/p.md).\n\n"))
        self.fails("between a heading and its body", index=TREE_INDEX.replace("## About\n\n", "## About\n\n| Part | Where |\n|---|---|\n| S | [p](net/p.md) |\n\n"))


ALIKE = """# Lim

## A

### The limits

| what | number |
|---|---|
| a | 1 |

## B

### The limits

| what | number |
|---|---|
| b | 2 |

## C

See [a](#the-limits) and [b](#the-limits-1).
"""
ALIKE_OTHER = "# Other\n\nSee [a](LIM.md#the-limits) and [b](LIM.md#the-limits-1) and [c](SAME.md#s-1).\n"
SAME = "# Same\n\n## S\n\nfirst words\n\n## S\n\nsecond words\n"
ALIKE_OTHER_NEW = ALIKE_OTHER.replace("LIM.md", "lim/p.md")
ALIKE_INDEX = "# Lim\n\nSee [the part](lim/p.md).\n"
ALIKE_PART = "# Lim, the part\n\nSee [the index](../LIM.md).\n\n" + ALIKE[ALIKE.index("## A"):]


@unittest.skipUnless(shutil.which("git"), "git is not installed")
class Alike(unittest.TestCase):
    """Two headings with the same text that begin alike: a link to one is not a link to the other."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.addCleanup(self._tmp.cleanup)
        self.git("init", "-q")
        self.put("README.md", "[l](docs/LIM.md) [o](docs/OTHER.md)\n")
        self.put("docs/LIM.md", ALIKE)
        self.put("docs/OTHER.md", ALIKE_OTHER)
        self.put("docs/SAME.md", SAME)
        self.git("add", "-A")
        self.git("-c", "user.name=t", "-c", "user.email=t@example.org", "commit", "-q", "-m", "base")
        self.base = self.git("rev-parse", "HEAD").strip()

    git = Move.git
    put = Move.put

    def run_split(self, part=ALIKE_PART, other=ALIKE_OTHER_NEW):
        self.put("docs/LIM.md", ALIKE_INDEX)
        self.put("docs/lim/p.md", part)
        self.put("docs/OTHER.md", other)
        done = subprocess.run([sys.executable, TOOL, self.base, "--root", self.root], capture_output=True, text=True)
        return done.returncode, done.stdout, done.stderr

    def test_the_good_split_passes(self):
        code, out, err = self.run_split()
        self.assertEqual((code, out), (0, ""), out + err)

    def test_two_targets_swapped_in_a_moved_line_fail(self):
        code, out, _ = self.run_split(part=ALIKE_PART.replace("[a](#the-limits) and [b](#the-limits-1)", "[a](#the-limits-1) and [b](#the-limits)"))
        self.assertEqual(code, 1)
        self.assertIn('"## C"', out)

    def test_a_link_aimed_at_the_other_one_fails(self):
        code, out, _ = self.run_split(part=ALIKE_PART.replace("[b](#the-limits-1)", "[b](#the-limits)"))
        self.assertEqual(code, 1)
        self.assertIn('"## C"', out)

    def test_a_link_to_the_second_of_two_equal_headings_of_a_page_that_stays_is_read_for_its_body(self):
        code, out, _ = self.run_split(other=ALIKE_OTHER_NEW.replace("SAME.md#s-1", "SAME.md#s"))
        self.assertEqual(code, 1)
        self.assertIn("(b) docs/OTHER.md", out)

    def test_a_link_of_another_page_aimed_at_the_other_one_fails(self):
        code, out, _ = self.run_split(other=ALIKE_OTHER_NEW.replace("p.md#the-limits-1", "p.md#the-limits"))
        self.assertEqual(code, 1)
        self.assertIn("(b) docs/OTHER.md", out)


if __name__ == "__main__":
    unittest.main()
