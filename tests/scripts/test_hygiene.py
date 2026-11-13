#!/usr/bin/env python3
"""Tests of tools/check_hygiene.py: each check on small synthetic trees, and the real repository (which must pass)."""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import check_hygiene as ch  # noqa: E402
import size_limits as sl  # noqa: E402


class Tree(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.addCleanup(self._tmp.cleanup)

    def put(self, rel, content="", size=None, lines=None):
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if lines is not None:
            content = "x\n" * lines
        if size is not None:
            content = "a" * size
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)

    def files(self):
        return ch.tracked_files(self.root)


class Levels(unittest.TestCase):
    def test_the_three_levels_of_each_base(self):
        self.assertEqual(ch.levels(sl.DOC_BASE_BYTES), {"target": 76800, "split": 92160, "cap": 128000})
        self.assertEqual(ch.levels(sl.PRODUCTION_BASE_LINES), {"target": 1125, "split": 1350, "cap": 1875})
        self.assertEqual(ch.levels(sl.TEST_BASE_LINES), {"target": 1875, "split": 2250, "cap": 3125})

    def test_the_numbers_are_ordered_and_the_pending_names_are_paths(self):
        self.assertLess(sl.TARGET_PERCENT, sl.SPLIT_PERCENT)
        self.assertLess(sl.SPLIT_PERCENT, sl.CAP_PERCENT)
        self.assertGreater(sl.CAP_PERCENT, 100)
        for name in sl.SPLIT_PENDING:
            self.assertTrue(os.path.exists(os.path.join(ROOT, name)), name)

    def test_the_pending_list_can_only_shrink(self):
        self.assertLessEqual(sl.SPLIT_PENDING, {"docs/GAME_REVERSE_ENGINEERING.md", "docs/NETWORK_PORT.md", "docs/BOTS.md", "web/shell.html", "tools/map_sweep.cpp"},
                             "SPLIT_PENDING is the files that were over the cap when the policy began: a split removes a name, nothing adds one (split the new file instead)")


class DocSize(Tree):
    def sizes(self, **kw):
        errors, warnings = ch.check_sizes(self.root, self.files(), **{"pending": set(), **kw})
        return errors, warnings

    def test_a_document_warns_from_its_target_and_fails_only_over_the_cap(self):
        at = ch.levels(sl.DOC_BASE_BYTES)
        for name, size in (("below", at["target"]), ("target", at["target"] + 1), ("below_split", at["split"] - 1), ("split", at["split"]), ("at_cap", at["cap"]), ("over_cap", at["cap"] + 1)):
            self.put("docs/%s.md" % name, size=size)
        errors, warnings = self.sizes()
        self.assertEqual([e.split(":")[0] for e in errors], ["docs/over_cap.md"])
        self.assertIn("above the safety cap 128,000", errors[0])
        self.assertEqual([w[0] for w in warnings], ["docs/at_cap.md", "docs/below_split.md", "docs/split.md", "docs/target.md"])
        self.assertIn("split it by topic now (target 76,800)", warnings[0][1])
        self.assertIn("125% of 102,400", warnings[0][1])
        self.assertEqual([w[2] for w in warnings], ["split", "target", "split", "target"])
        self.assertIn("over its target 76,800: split it by topic before it reaches 92,160", warnings[1][1])        # (80% and 96% are not told the same)
        self.assertNotIn("now", warnings[1][1])

    def test_root_documents_count_and_other_files_do_not(self):
        self.put("TOP.md", size=ch.levels(sl.DOC_BASE_BYTES)["cap"] + 1)
        self.put("docs/data.json", size=sl.DOC_BASE_BYTES * 2)
        self.put("src/NOTES.md", size=sl.DOC_BASE_BYTES * 2)
        errors, warnings = self.sizes()
        self.assertEqual([e.split(":")[0] for e in errors], ["TOP.md"])
        self.assertEqual(warnings, [])

    def test_a_small_document_is_quiet(self):
        self.put("docs/A.md", size=100)
        self.assertEqual(self.sizes(), ([], []))


class CodeSize(Tree):
    def sizes(self, **kw):
        return ch.check_sizes(self.root, self.files(), **{"pending": set(), **kw})

    def test_a_code_file_warns_from_the_split_line_and_fails_only_over_the_cap(self):
        for base, make in ((sl.PRODUCTION_BASE_LINES, "src/x/%s.cpp"), (sl.TEST_BASE_LINES, "tests/a/test_%s.cpp")):
            at = ch.levels(base)
            for name, n in (("target", at["target"] + 1), ("below_split", at["split"] - 1), ("split", at["split"]), ("at_cap", at["cap"]), ("over_cap", at["cap"] + 1)):
                self.put(make % name, lines=n)
        errors, warnings = self.sizes()
        self.assertEqual([e.split(":")[0] for e in errors], ["src/x/over_cap.cpp", "tests/a/test_over_cap.cpp"])
        self.assertIn("above the safety cap 1,875", errors[0])
        self.assertIn("above the safety cap 3,125", errors[1])
        self.assertEqual([w[0] for w in warnings], ["src/x/at_cap.cpp", "src/x/split.cpp", "tests/a/test_at_cap.cpp", "tests/a/test_split.cpp"])
        self.assertIn("1,350 lines, 90% of 1,500", warnings[1][1])
        self.assertNotIn("check_doc_move", errors[0])                                 # (that tool reads .md only)

    def test_the_kinds_of_files(self):
        for rel in ("web/big.html", "tools/big.py", "include/x/big.hpp"):
            self.put(rel, lines=sl.PRODUCTION_BASE_LINES * 2)
        self.put("src/x/generated.inc", lines=sl.PRODUCTION_BASE_LINES * 3)
        self.put("docs/x.md", lines=sl.PRODUCTION_BASE_LINES * 3)
        errors, _ = self.sizes()
        self.assertEqual(sorted(e.split(":")[0] for e in errors), ["include/x/big.hpp", "tools/big.py", "web/big.html"])

    def test_last_line_without_newline_counts(self):
        self.assertEqual(ch.line_count(b"a\nb"), 2)
        self.assertEqual(ch.line_count(b"a\nb\n"), 2)
        self.assertEqual(ch.line_count(b""), 0)


class Pending(Tree):
    def test_a_pending_file_over_the_cap_warns_instead_of_failing(self):
        self.put("docs/BIG.md", size=ch.levels(sl.DOC_BASE_BYTES)["cap"] + 1)
        self.put("src/x/big.cpp", lines=ch.levels(sl.PRODUCTION_BASE_LINES)["cap"] + 1)
        errors, warnings = ch.check_sizes(self.root, self.files(), pending={"docs/BIG.md", "src/x/big.cpp"})
        self.assertEqual(errors, [])
        self.assertEqual([w[0] for w in warnings], ["docs/BIG.md", "src/x/big.cpp"])
        self.assertTrue(all("SPLIT_PENDING" in w[1] for w in warnings))

    def test_a_pending_file_that_is_no_longer_over_the_cap_fails(self):
        self.put("docs/A.md", size=ch.levels(sl.DOC_BASE_BYTES)["cap"])
        self.put("src/x/b.cpp", lines=10)
        errors, _ = ch.check_sizes(self.root, self.files(), pending={"docs/A.md", "src/x/b.cpp"})
        self.assertEqual(sorted(e.split(":")[0] for e in errors), ["docs/A.md", "src/x/b.cpp"])
        self.assertTrue(all("remove it from SPLIT_PENDING" in e for e in errors))

    def test_a_pending_name_that_is_gone_or_is_not_measured_fails(self):
        self.put("docs/data.json", size=sl.DOC_BASE_BYTES * 3)
        errors, _ = ch.check_sizes(self.root, self.files(), pending={"docs/GONE.md", "docs/data.json"})
        self.assertEqual(sorted(e.split(":")[0] for e in errors), ["docs/GONE.md", "docs/data.json"])
        self.assertIn("not a tracked file", errors[0] if errors[0].startswith("docs/GONE") else errors[1])

    def test_a_file_that_is_not_pending_gets_no_leave(self):
        self.put("docs/BIG.md", size=ch.levels(sl.DOC_BASE_BYTES)["cap"] + 1)
        errors, _ = ch.check_sizes(self.root, self.files(), pending={"docs/OTHER.md"})
        self.assertEqual(sorted(e.split(":")[0] for e in errors), ["docs/BIG.md", "docs/OTHER.md"])


class Warnings(Tree):
    def w(self, rel, kind, pct, text="x"):
        return (rel, text, kind, pct)

    def test_the_lines_are_plain_or_annotations_for_the_pull_request(self):
        w = [self.w("docs/A.md", "split", 93, "95,000 B, 93% of 102,400: split it by topic now (target 76,800)"), self.w("docs/B.md", "long", 0, "3 line(s) in 1 document(s)")]
        self.assertEqual(ch.warning_lines(w, github=False), ["warning: docs/A.md: 95,000 B, 93% of 102,400: split it by topic now (target 76,800)", "notice: docs/B.md: 3 line(s) in 1 document(s)"])
        self.assertEqual(ch.warning_lines(w, github=True), ["::warning file=docs/A.md::95,000 B, 93% of 102,400: split it by topic now (target 76,800)", "::notice file=docs/B.md::3 line(s) in 1 document(s)"])

    def test_the_environment_chooses_the_form(self):
        old = os.environ.pop("GITHUB_ACTIONS", None)
        self.addCleanup(lambda: os.environ.update({"GITHUB_ACTIONS": old}) if old is not None else os.environ.pop("GITHUB_ACTIONS", None))
        self.assertTrue(ch.warning_lines([self.w("a.md", "split", 91)])[0].startswith("warning: "))
        os.environ["GITHUB_ACTIONS"] = "true"
        self.assertTrue(ch.warning_lines([self.w("a.md", "split", 91)])[0].startswith("::warning file=a.md::"))

    def test_the_closest_to_the_limit_come_first_and_the_waiting_ones_after(self):
        w = [self.w("pending.md", "pending", 400), self.w("target.md", "target", 99), self.w("b.md", "split", 92), self.w("a.md", "split", 92), self.w("c.md", "split", 99)]
        self.assertEqual([l.split(":")[1].strip() for l in ch.warning_lines(w, github=False)], ["c.md", "a.md", "b.md", "target.md", "pending.md"])

    def test_on_the_pull_request_at_most_the_limit_is_shown_and_the_last_counts_the_rest(self):
        w = [self.w("f%02d.md" % i, "split", 100 - i) for i in range(25)] + [self.w("long.md", "long", 0, "lines")]
        out = ch.warning_lines(w, github=True)
        warnings = [l for l in out if l.startswith("::warning")]
        self.assertEqual(len(warnings), sl.ANNOTATION_LIMIT)
        self.assertIn("f00.md", warnings[0])
        self.assertIn("f08.md", warnings[sl.ANNOTATION_LIMIT - 2])
        self.assertNotIn("f09.md", "".join(out))
        self.assertTrue(warnings[-1].startswith("::warning::16 more file(s)"))
        self.assertEqual(out[-1], "::notice file=long.md::lines")
        few = ch.warning_lines(w[:sl.ANNOTATION_LIMIT], github=True)                     # (exactly the limit: all shown, nothing to count)
        self.assertEqual(len(few), sl.ANNOTATION_LIMIT)
        self.assertFalse(any("more file" in l for l in few))

    def test_a_long_line_is_found_once_for_each_document_and_folded_into_one_notice(self):
        limit = sl.DOC_LINE_WARN_CHARACTERS
        self.put("docs/A.md", "ok\n" + "a" * limit + "\n" + "b" * (limit + 1) + "\n" + "c" * (limit + 5) + "\n")
        self.put("docs/B.md", "a" * limit + "\n")
        self.put("docs/C.md", "d" * (limit + 9) + "\n")
        self.put("src/x.cpp", "// " + "a" * (limit * 2) + "\n")
        found = ch.check_long_lines(self.root, self.files())
        self.assertEqual(found, [("docs/A.md", 2, limit + 5, 4), ("docs/C.md", 1, limit + 9, 1)])
        rel, text, kind, _ = ch.long_line_warning(found)
        self.assertEqual((rel, kind), ("docs/C.md", "long"))
        self.assertIn("3 line(s) in 2 document(s) are over 2,000 characters (the longest has 2,009, docs/C.md line 1)", text)
        self.assertIsNone(ch.long_line_warning([]))

    def test_the_run_gathers_warnings_and_errors_apart(self):
        self.put("README.md", "[a](docs/A.md)\n")
        self.put("docs/A.md", "x" * (ch.levels(sl.DOC_BASE_BYTES)["target"] + 1))
        errors, warnings = ch.run_all(self.root)
        self.assertTrue(any(w[0] == "docs/A.md" and w[2] == "target" for w in warnings))
        self.assertFalse(any(e.startswith("docs/A.md") for e in errors))
        self.put("docs/B.md", "a" * (sl.DOC_LINE_WARN_CHARACTERS + 1) + "\n")
        self.assertEqual([w[2] for w in ch.run_all(self.root)[1]].count("long"), 1)


class Reachable(Tree):
    def test_transitive_folder_and_unreachable(self):
        self.put("README.md", "[a](docs/A.md) [f](docs/sub/) [web](https://x.org/docs/Z.md) [anchor](#top)\n")
        self.put("docs/A.md", "see [b](B.md#part)\n")
        self.put("docs/B.md", "end\n")
        self.put("docs/sub/README.md", "[p](page.md)\n")
        self.put("docs/sub/page.md", "page\n")
        self.put("docs/Z.md", "orphan\n")
        self.put("docs/sub/deeper/README.md", "orphan folder\n")
        out = ch.check_reachable(self.root, self.files())
        self.assertEqual(sorted(l.split(":")[0] for l in out), ["docs/Z.md", "docs/sub/deeper/README.md"])

    def test_reference_style_link_and_code_block(self):
        self.put("README.md", "[a]\n\n[a]: docs/A.md\n\n```\n[x](docs/B.md)\n```\n")
        self.put("docs/A.md", "a\n")
        self.put("docs/B.md", "b\n")
        out = ch.check_reachable(self.root, self.files())
        self.assertEqual([l.split(":")[0] for l in out], ["docs/B.md"])

    def test_deep_pages_are_checked_too_and_need_a_link_from_a_reachable_page(self):
        self.put("README.md", "[s](docs/sub/README.md)\n")
        self.put("docs/sub/README.md", "[d](d/one.md)\n")
        self.put("docs/sub/d/one.md", "one\n")
        self.put("docs/sub/d/two.md", "never linked: a deep page is checked like any other\n")
        self.put("docs/sub/d/e/three.md", "[o](../one.md)\n")
        self.put("src/notes.md", "not a document of docs/\n")
        out = ch.check_reachable(self.root, self.files())
        self.assertEqual(sorted(l.split(":")[0] for l in out), ["docs/sub/d/e/three.md", "docs/sub/d/two.md"])


class ReachableAsAReaderSees(Tree):
    def test_only_a_link_that_the_page_shows_makes_a_page_reachable(self):
        self.put("README.md", "[ok](docs/Ok.md)\n`[code](docs/Code.md)`\n~~~\n[tilde](docs/Tilde.md)\n~~~\n<!-- [comment](docs/Comment.md) -->\n[unused]: docs/NeverUsed.md\n")
        for name in ("Ok", "Code", "Tilde", "Comment"):
            self.put("docs/%s.md" % name, "x\n")
        out = ch.check_reachable(self.root, self.files())
        self.assertEqual(sorted(l.split(":")[0] for l in out), ["docs/Code.md", "docs/Comment.md", "docs/Tilde.md"])


class Anchors(Tree):
    def test_the_slug_of_a_heading(self):
        for heading, slug in (("5.17 Foo", "517-foo"), ("The `x_y` limits (v2)", "the-x_y-limits-v2"), ("Q&A: What?", "qa-what"), ("A -- B", "a----b"), ("**Bold** and _emphasis_", "bold-and-emphasis"),
                              ("See [the page](other.md) now", "see-the-page-now"), ("A <code>tag</code>", "a-tag"), ("A `<tag>` b", "a-tag-b"), ("The `*star*` rule", "the-star-rule"), ("With `a` and `b` end", "with-a-and-b-end"), ("snake_case_word", "snake_case_word"), ("\u00dcmlaut Stra\u00dfe", "\u00fcmlaut-stra\u00dfe")):
            self.assertEqual(ch.slug_of(heading), slug, heading)

    def test_the_headings_of_a_text_repeat_with_a_number_and_skip_code(self):
        text = "# Top part\n## Limits\ntext\n## Limits\n### Limits #\n```\n# not a heading\n```\n#not a heading either\n<a id=\"old\"></a>\n"
        self.assertEqual(ch.headings_of(text), {"top-part", "limits", "limits-1", "limits-2"})

    def test_a_link_to_a_heading_passes_and_a_dead_one_fails_with_its_place(self):
        self.put("README.md", "# Start\n[ok](docs/A.md#second-part) [self](#start) [dup](docs/A.md#limits-1) [enc](docs/A.md#second%20part)\n[bad](docs/A.md#third-part) [badself](#nowhere)\n")
        self.put("docs/A.md", "# A\n## Second part\n## Limits\n## Limits\n")
        out = ch.check_anchors(self.root, self.files())
        self.assertEqual(len(out), 3)                                                  # (the encoded space is not a hyphen: GitHub gives no heading either)
        self.assertTrue(any(o.startswith("README.md:3: ") and "docs/A.md#third-part" in o for o in out))
        self.assertTrue(any(o.startswith("README.md:3: ") and "README.md#nowhere" in o for o in out))
        self.assertTrue(any(o.startswith("README.md:2: ") and "second%20part" in o.replace(" ", "%20") for o in out))

    def test_links_are_followed_from_a_folder_and_case_does_not_matter(self):
        self.put("docs/sub/B.md", "[up](../A.md#The-Heading) [same](B.md#my-title)\n# My Title\n")
        self.put("docs/A.md", "# The Heading\n")
        self.assertEqual(ch.check_anchors(self.root, self.files()), [])
        self.put("docs/sub/C.md", "[gone](../A.md#heading)\n")
        self.assertEqual(len(ch.check_anchors(self.root, self.files())), 1)

    def test_a_redirect_id_is_not_a_heading(self):
        self.put("docs/A.md", "<a id=\"old-slug\"></a>\n# New\n")
        self.put("docs/B.md", "[x](A.md#old-slug) [y](A.md#new)\n")
        out = ch.check_anchors(self.root, self.files())
        self.assertEqual(len(out), 1)
        self.assertIn("A.md#old-slug", out[0])

    def test_code_external_links_and_other_files_are_not_checked(self):
        self.put("docs/A.md", "# A\n")
        self.put("docs/B.md", "```\n[x](A.md#gone)\n```\n`[y](A.md#gone)` [web](https://x.org/A.md#gone) [src](../src/a.cpp#L10) [file](A.md) [empty](A.md#)\n~~~\n[z](#gone)\n~~~\n")
        self.put("src/a.cpp", "x\n")
        self.assertEqual(ch.check_anchors(self.root, self.files()), [])

    def test_a_link_to_a_markdown_file_that_is_not_tracked_fails_with_or_without_an_anchor(self):
        self.put("docs/A.md", "# A\n[x](NOPE.md#real-one) [y](NOPE.md) [wrong case](b.md#b) [ok](B.md#b) [folder](sub/#x) [code](../src/a.cpp)\n")
        self.put("docs/B.md", "# B\n")
        out = ch.check_anchors(self.root, self.files())
        self.assertEqual(len(out), 3)
        self.assertTrue(all("docs/A.md:2: " in o and ".md file that is not tracked" in o for o in out))
        self.assertTrue(any("docs/NOPE.md" in o for o in out) and any("docs/b.md" in o for o in out))

    def test_underscores_go_in_pairs_only(self):
        for heading, slug in (("[__thiscall ant, ret 0x18]", "__thiscall-ant-ret-0x18"), ("a _b_ c", "a-b-c"), ("a __b__ c", "a-b-c"), ("snake_case_word", "snake_case_word"),
                              ("_lone", "_lone"), ("lone_", "lone_"), ("__init__ method", "init-method"), ("x_ y_", "x_-y_")):
            self.assertEqual(ch.slug_of(heading), slug, heading)

    def test_an_entity_in_a_heading_is_its_character(self):
        self.assertEqual(ch.slug_of("Q &amp; A"), "q--a")
        self.assertEqual(ch.slug_of("Less &lt; more"), "less--more")

    def test_a_heading_in_an_html_comment_is_not_one_and_does_not_shift_the_numbers(self):
        text = "# Top\n<!--\n## Hidden\n## Limits\n-->\n## Limits\n<!-- ## Inline -->\ntext <!-- x --> more\n## Visible\n"
        self.assertEqual(ch.headings_of(text), {"top", "limits", "visible"})
        self.assertEqual(ch.headings_of("a <!-- c --> b\n## H\n"), {"h"})                                  # (a comment inside a line ends in the line)
        self.assertEqual(ch.headings_of("a <!-- c\n## G\nd --> b\n## H\n"), {"h"})
        self.put("docs/A.md", "# A\n<!-- ## Template -->\n")
        self.put("docs/B.md", "[x](A.md#template) <!-- [y](A.md#gone) -->\n")
        out = ch.check_anchors(self.root, self.files())
        self.assertEqual(len(out), 1)
        self.assertIn("A.md#template", out[0])

    def test_a_comment_mark_in_inline_code_is_text_and_hides_nothing(self):
        self.assertEqual(ch.headings_of("Write `<!--` to open a comment.\n## H\n"), {"h"})
        self.assertEqual(ch.headings_of("Write ``a <!-- b`` and `<!--` here.\n## H\n"), {"h"})
        self.assertEqual(ch.headings_of("`<!--` <!-- real --> text\n## H\n"), {"h"})
        self.assertEqual(ch.headings_of("`<!--` <!-- real\n## G\n--> text\n## H\n"), {"h"})
        self.assertEqual(ch.headings_of("<!-- real `-->` code is no code inside a comment\n## G\n"), {"g"})
        self.put("docs/A.md", "# A\n\nWrite `<!--` to open a comment.\n\n## Later\n")
        self.put("docs/B.md", "[ok](A.md#later) [bad](A.md#gone)\n")
        out = ch.check_anchors(self.root, self.files())
        self.assertEqual(len(out), 1)
        self.assertIn("A.md#gone", out[0])

    def test_a_fence_closes_with_its_own_mark_and_no_words(self):
        text = "````\n```\n# inside\n```\n# still inside\n````\n# outside\n~~~ python\n# in a tilde block\n~~~ not a close\n# still in it\n~~~\n# after\n"
        self.assertEqual(ch.headings_of(text), {"outside", "after"})
        self.assertEqual(ch.headings_of("``` has `tick`\n# not a fence, a paragraph\n"), {"not-a-fence-a-paragraph"})

    def test_a_byte_order_mark_does_not_hide_the_first_heading(self):
        self.assertEqual(ch.headings_of("\ufeff# First\n"), {"first"})

    def test_a_reference_style_link_is_checked(self):
        self.put("docs/A.md", "# A\n[r]: B.md#gone\n")
        self.put("docs/B.md", "# B\n")
        self.assertEqual(len(ch.check_anchors(self.root, self.files())), 1)


class Forbidden(Tree):
    DECOY = (hashlib.sha256(b"decoy-notes.md").hexdigest(), 14)      # (a made-up name: the real ones are on PRIVATE_NAME_DIGESTS and are spelled nowhere)

    def test_each_string_is_found(self):
        for i, s in enumerate(["claude.ai/code", "Co-Authored-By", "Claude-Session", "Project Thread", "THE OTHER THREAD"]):
            self.put("src/f%d.cpp" % i, "// see %s here\n" % s)
        out = ch.check_forbidden(self.root, self.files(), allowed=set())
        self.assertEqual(len(out), 5)
        self.assertTrue(out[0].startswith("src/f0.cpp:1:"))

    def test_a_private_name_is_found_by_its_digest_in_any_case_and_inside_a_longer_run(self):
        self.put("src/a.cpp", "int x;\n// see Decoy-Notes.md here\n")
        self.put("docs/b.md", "[x](DECOY-NOTES.MD) and mydecoy-notes.mdx\n")
        self.put("src/c.cpp", "// decoy notes.md, decoy_notes.md, decoy-notes.m, ecoy-notes.md, a decoy\n")        # near misses
        out = ch.check_forbidden(self.root, self.files(), allowed=set(), digests=(self.DECOY,))
        self.assertEqual([o.split(" ")[0] for o in out], ["docs/b.md:1:", "src/a.cpp:2:"])

    def test_a_finding_about_a_private_name_does_not_repeat_the_name(self):
        self.put("src/a.cpp", "// decoy-notes.md\n")
        out = ch.check_forbidden(self.root, self.files(), allowed=set(), digests=(self.DECOY,))
        self.assertEqual(len(out), 1)
        self.assertNotIn("decoy", out[0].lower())

    def test_the_names_on_the_list_are_digests_that_pass_the_minimum(self):
        self.assertEqual(len(ch.PRIVATE_NAME_DIGESTS), len(set(ch.PRIVATE_NAME_DIGESTS)))
        for digest, length in ch.PRIVATE_NAME_DIGESTS:
            self.assertRegex(digest, r"^[0-9a-f]{64}$")
            self.assertGreaterEqual(length, ch.PRIVATE_NAME_MIN_LENGTH)
        ch.private_name_matcher()                                                            # (the real list is accepted)

    def test_a_short_entry_is_refused_when_the_check_starts(self):
        short = (hashlib.sha256(b"x" * (ch.PRIVATE_NAME_MIN_LENGTH - 1)).hexdigest(), ch.PRIVATE_NAME_MIN_LENGTH - 1)
        with self.assertRaises(ValueError):
            ch.private_name_matcher((short,))
        with self.assertRaises(ValueError):
            ch.check_forbidden(self.root, self.files(), digests=(self.DECOY, short))

    def test_allowlist_binary_maps_and_previews_are_skipped(self):
        self.put("AGENTS.md", "Claude-Session\n")
        self.put("Community-Maps/m.lvl", "Co-Authored-By decoy-notes.md\n")
        self.put("previews/p.txt", "claude.ai/code decoy-notes.md\n")
        self.put("third_party/sqlite/sqlite3.c", "/* the other thread */\n")
        self.put("third_party/sqlite/sqlite3.h", "/* the other thread */\n")
        with open(os.path.join(self.root, "b.bin"), "wb") as f:
            f.write(b"\0\0claude.ai/code decoy-notes.md")
        self.put("docs/ok.md", "plain words\n")
        self.assertEqual(ch.check_forbidden(self.root, self.files(), allowed={"AGENTS.md"}, digests=(self.DECOY,)), [])
        self.assertEqual(len(ch.check_forbidden(self.root, self.files(), allowed=set(), digests=(self.DECOY,))), 1)

    def test_the_allow_list_is_for_the_trailers_and_not_for_the_names(self):
        self.put("AGENTS.md", "Claude-Session\n// see decoy-notes.md\n")
        out = ch.check_forbidden(self.root, self.files(), allowed={"AGENTS.md"}, digests=(self.DECOY,))
        self.assertEqual([o.split(" ")[0] for o in out], ["AGENTS.md:2:"])                       # (the trailer on line 1 is allowed there, the name on line 2 is not)

    def test_the_name_of_a_tracked_file_is_looked_at_too(self):
        self.put("notes/Decoy-Notes.md", "innocent words\n")                                      # (any case; the text is clean: only the name of the file is private)
        self.put("docs/mydecoy-notes.mdx.txt", "x\n")                                             # (inside a longer run)
        self.put("docs/decoy notes.md", "x\n")                                                    # (near misses)
        self.put("docs/decoy-notes", "x\n")
        out = ch.check_forbidden(self.root, self.files(), allowed=set(), digests=(self.DECOY,))
        self.assertEqual([o.split(":")[0] for o in out], ["docs/my**************x.txt", "notes/**************"])
        self.assertFalse(any("decoy" in o.lower() for o in out), "a finding about a path does not repeat the name")

    def test_the_name_of_a_file_in_the_allow_list_or_in_a_skipped_folder_is_looked_at_too(self):
        self.put("tests/decoy-notes.md", "x\n")
        self.put("previews/x-decoy-notes.md", "x\n")
        self.put("Community-Maps/decoy-notes.md", "x\n")
        out = ch.check_forbidden(self.root, self.files(), allowed={"tests/decoy-notes.md"}, digests=(self.DECOY,))
        self.assertEqual(len(out), 3)

    def test_our_own_files_in_third_party_are_checked(self):
        self.put("third_party/sqlite/README.md", "see Claude-Session\n")
        self.put("third_party/sqlite/CMakeLists.txt", "# Co-Authored-By\n")
        out = ch.check_forbidden(self.root, self.files(), allowed=set())
        self.assertEqual(len(out), 2)


class AssertTrue(Tree):
    def test_count_ratchet(self):
        self.put("tests/a/t1.cpp", "#define ASSERT_TRUE(x) y\n")
        self.put("tests/a/t2.cpp", "  #  define ASSERT_TRUE(x) y\n// ASSERT_TRUE(x) is used here\n#define ASSERT_TRUE_MSG 1\n")
        self.put("docs/x.md", "#define ASSERT_TRUE\n")
        f = self.files()
        self.assertEqual(ch.assert_true_count(self.root, f), 2)
        self.assertEqual(ch.check_assert_true(self.root, f, allowed=2), [])
        self.assertIn("at most 1", ch.check_assert_true(self.root, f, allowed=1)[0])
        self.assertIn("lower the constant", ch.check_assert_true(self.root, f, allowed=3)[0])


class CMake(Tree):
    def test_unnamed_cpp_fails_and_exceptions_pass(self):
        self.put("CMakeLists.txt", "add_executable(a src/x/a.cpp tests/t/test_b.cpp)\n")
        self.put("src/x/a.cpp")
        self.put("tests/t/test_b.cpp")
        self.put("tests/t/test_c.cpp")
        self.put("tools/xtest_b.cpp")
        self.put("web/other.cpp")
        out = ch.check_cmake(self.root, self.files(), exceptions=set())
        self.assertEqual(sorted(l.split(":")[0] for l in out), ["tests/t/test_c.cpp", "tools/xtest_b.cpp"])
        self.assertEqual(ch.check_cmake(self.root, self.files(), exceptions={"tests/t/test_c.cpp", "tools/xtest_b.cpp"}), [])

    def test_name_may_be_in_a_nested_cmakelists(self):
        self.put("tests/t/CMakeLists.txt", "add_executable(t test_c.cpp)\n")
        self.put("tests/t/test_c.cpp")
        self.assertEqual(ch.check_cmake(self.root, self.files(), exceptions=set()), [])


class BuildFolders(Tree):
    def git(self, *args):
        subprocess.run(["git", "-C", self.root] + list(args), check=True, capture_output=True)

    def test_a_file_below_a_build_folder_fails_one_line_for_each_folder(self):
        self.put("build/CMakeCache.txt", "x\n")
        self.put("build_clang/CMakeFiles/a.cmake", "x\n")
        self.put("build_clang/CMakeFiles/b.cmake", "x\n")
        self.put("build_x/y.o", "x\n")
        out = ch.check_build_folders(self.files())
        self.assertEqual([l.split("/:")[0] for l in out], ["build", "build_clang", "build_x"])
        self.assertIn("2 tracked file(s), for example build_clang/CMakeFiles/a.cmake", out[1])

    def test_other_paths_are_not_build_folders(self):
        for rel in ("build_web.sh", "builder/x", "rebuild/x", "Build/x", "docs/build/x.md", "src/build_x/y.cpp", "tools/build_all.py"):
            self.put(rel, "x\n")
        self.assertEqual(ch.check_build_folders(self.files()), [])

    @unittest.skipUnless(shutil.which("git"), "git is not installed")
    def test_in_a_checkout_the_ignored_folder_passes_and_a_force_added_file_fails(self):
        self.git("init", "-q")
        self.put(".gitignore", "build_*/\n")
        self.put("build_x/a.o", "x\n")
        self.git("add", "-A")                                                # (the ignore rule keeps the folder out)
        self.assertEqual(ch.check_build_folders(self.files()), [])
        self.git("add", "-f", "build_x/a.o")                                 # (what put build_clang in a pull request)
        out = ch.check_build_folders(self.files())
        self.assertEqual(len(out), 1)
        self.assertTrue(out[0].startswith("build_x/: 1 tracked file(s), for example build_x/a.o"))

    def test_the_tool_exits_1_and_names_the_folder(self):
        self.put("README.md", "hello\n")
        self.put("build_x/CMakeCache.txt", "x\n")
        done = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_hygiene.py"), "--root", self.root], capture_output=True, text=True)
        self.assertEqual(done.returncode, 1)
        self.assertTrue(any(l.startswith("build_x/: ") for l in done.stdout.splitlines()), done.stdout)


class Cli(Tree):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_hygiene.py")] + list(args), capture_output=True, text=True)

    def test_usage_error_is_2(self):
        self.assertEqual(self.run_tool("--bogus").returncode, 2)
        self.assertEqual(self.run_tool("--root", os.path.join(self.root, "nope")).returncode, 2)

    def test_findings_are_one_line_each_with_exit_1(self):
        self.put("README.md", "hello\n")
        self.put("docs/A.md", "orphan\n")
        self.put("src/x.cpp", "// Claude-Session\n")
        done = self.run_tool("--root", self.root)
        self.assertEqual(done.returncode, 1)
        lines = done.stdout.splitlines()
        self.assertTrue(any(l.startswith("docs/A.md:") for l in lines))
        self.assertTrue(any(l.startswith("src/x.cpp:1:") for l in lines))
        self.assertTrue(any("not named in any CMakeLists.txt" in l for l in lines))
        self.assertTrue(any(l.startswith("ASSERT_TRUE:") for l in lines))  # no definitions here: fewer than the constant
        self.assertEqual(len([l for l in lines if "remove it from the" not in l]), 4)  # the pending names and the spelling table name the real repository's files, which this tree lacks

    def test_list_prints_numbers(self):
        done = self.run_tool("--list")
        self.assertEqual(done.returncode, 0)
        self.assertIn("ASSERT_TRUE definitions", done.stdout)
        self.assertIn("files over their target", done.stdout)

    def test_warnings_go_to_stderr_and_do_not_fail_by_themselves(self):
        self.put("README.md", "[a](docs/A.md)\n")
        self.put("docs/A.md", size=ch.levels(sl.DOC_BASE_BYTES)["split"])
        env = {k: v for k, v in os.environ.items() if k != "GITHUB_ACTIONS"}                    # (CI sets it: the plain form is for a person at a terminal)
        plain = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_hygiene.py"), "--root", self.root], capture_output=True, text=True, env=env)
        self.assertIn("warning: docs/A.md: 92,160 B, 90% of 102,400: split it by topic now (target 76,800)", plain.stderr.splitlines())
        self.assertFalse(any("docs/A.md" in l for l in plain.stdout.splitlines()))
        gh = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_hygiene.py"), "--root", self.root], capture_output=True, text=True, env=dict(env, GITHUB_ACTIONS="true"))
        self.assertIn("::warning file=docs/A.md::92,160 B, 90% of 102,400: split it by topic now (target 76,800)", gh.stderr.splitlines())


class Spelling(Tree):
    UK = "col" + "our"          # (the test file passes the check itself)

    def test_the_british_spelling_fails_in_any_case_and_compound(self):
        for i, w in enumerate([self.UK, self.UK.capitalize(), self.UK.upper(), self.UK + "s", "dis" + self.UK + "ed", "k" + self.UK.capitalize() + "Red"]):
            self.put("src/f%d.cpp" % i, "int x;\n// the %s here\n" % w)
        out = ch.check_spelling(self.root, self.files())
        self.assertEqual(len(out), 6)
        self.assertTrue(out[0].startswith("src/f0.cpp:2: British spelling"))

    def test_the_american_spelling_and_other_words_pass(self):
        self.put("docs/a.md", "color, Colors, COLOR, colored, a cool hour\n")
        self.assertEqual(ch.check_spelling(self.root, self.files()), [])

    def test_a_file_name_fails(self):
        self.put("tools/make_%s.py" % self.UK, "x = 1\n")
        self.assertEqual(len(ch.check_spelling(self.root, self.files())), 1)

    def test_skipped_folders_binary_files_and_lvl_pass(self):
        for rel in ("Community-Maps/README.md", "docs/legacy/x.md", "third_party/sqlite/a.c"):
            self.put(rel, "the %s\n" % self.UK)
        self.put("Community-Maps/m.lvl", "%s\n" % self.UK)
        with open(os.path.join(self.root, "b.bin"), "wb") as f:
            f.write(b"\0\0" + self.UK.encode())
        self.assertEqual(ch.check_spelling(self.root, self.files()), [])

    def test_the_older_spelling_passes_only_in_the_places_that_still_have_it(self):
        uk = self.UK
        page_forms = ['"%s": "Green"' % uk, "seat['%s']" % uk, "`%s` (`all`)" % uk, "/history?%s=blue" % uk, "seat.%s" % uk]
        self.put("web/history_page.js", "\n".join(page_forms) + "\n")                    # the pages' fallback: the key, the parameter, the property
        self.put("src/ants_server/history_record.cpp", 'o.set("%s", x);\n' % uk)           # the deprecated key, written
        self.put("docs/REPLAYS.md", "`%s=black` and `\"%s\"`\n" % (uk, uk))
        self.assertEqual(ch.check_spelling(self.root, self.files()), [])
        self.put("src/ants_server/control_history.cpp", "bool x = q.%s == 1;\n" % uk)       # a property is not kept here (only the quoted name and the parameter)
        self.put("src/ants_server/history_record.cpp", 'o.set("%s", x);\n// and %s=blue\n' % (uk, uk))   # nor is the parameter in the record's writer
        for rel in ("web/match.html", "web/matches.html", "web/live.html", "web/replay_page.js", "src/ants_net/x.cpp"):    # the readers that now say color
            self.put(rel, "\n".join(page_forms) + "\n")
        out = ch.check_spelling(self.root, self.files())
        self.assertEqual(len(out), 2 + 5 * len(page_forms))
        self.assertTrue(all(o.split(":")[0] != "web/history_page.js" for o in out))

    def test_only_the_lower_case_forms_are_kept(self):
        uk = self.UK
        self.put("web/history_page.js", "/history?%s=blue\n/history?%s=blue\nseat.%s\n" % (uk, uk.capitalize(), uk.upper()))
        self.assertEqual([x.split(":")[1] for x in ch.check_spelling(self.root, self.files())], ["2", "3"])

    def test_a_kept_pattern_that_no_line_uses_is_a_finding(self):
        uk = self.UK
        kept = {"web/a.js": [r"\.%s\b" % uk, r"\b%s=" % uk], "web/gone.js": [r"\.%s\b" % uk]}
        self.put("web/a.js", "x = s.%s;\n" % uk)
        out = ch.check_spelling_table(self.root, self.files(), kept)
        self.assertEqual(len(out), 2)
        self.assertTrue(out[0].startswith("web/a.js: the kept spelling pattern") and out[1].startswith("web/gone.js: in the spelling table but not tracked"))

    def test_the_wire_notice_has_its_own_places(self):
        uk = self.UK
        notice = "This map cannot be played with these %ss." % uk
        for rel in ("include/ants_net/protocol.hpp", "web/front/lobby_rules.js"):
            self.put(rel, notice + "\n")
        self.assertEqual(ch.check_spelling(self.root, self.files()), [])
        self.put("web/lobby.html", notice + "\n")                                         # the notice is not allowed in a page that does not compare with it
        self.assertEqual([x.split(":")[0] for x in ch.check_spelling(self.root, self.files())], ["web/lobby.html"])

    def test_a_page_that_reads_a_parsed_player_does_not_read_the_records_key(self):
        # parsePlayer("Green (Mika)") gives { color, name }; the history record's seat key is another thing (the live page slipped once)
        bad = []
        web = os.path.join(ROOT, "web")
        for name in sorted(os.listdir(web)):
            if name.endswith((".html", ".js")):
                text = open(os.path.join(web, name), encoding="utf-8").read()
                if "parsePlayer" in text and re.search(r"\.%s\b" % self.UK, text):
                    bad.append(name)
        self.assertEqual(bad, [])


class RealRepository(unittest.TestCase):
    def test_the_policy_files_are_there_and_say_what_they_should(self):
        with open(os.path.join(ROOT, "SECURITY.md"), encoding="utf-8") as f:
            policy = f.read()
        self.assertIn("Report a vulnerability", policy)
        self.assertNotRegex(policy, r"[\w.+-]+@[\w-]+\.[\w.]+", "no address: a report goes through GitHub's private reporting")
        with open(os.path.join(ROOT, ".github", "CODEOWNERS"), encoding="utf-8") as f:
            rules = [line.split() for line in f.read().splitlines() if line.strip() and not line.startswith("#")]
        self.assertTrue(rules and all(len(r) >= 2 and all(who.startswith("@") for who in r[1:]) for r in rules), "every rule names a reviewer")
        for pattern in ("*", "/.github/", "/docker/", "/Dockerfile*", "/docker-compose*.yml", "/tools/"):
            self.assertIn(pattern, [r[0] for r in rules])

    def test_repository_passes(self):
        done = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_hygiene.py"), "--root", ROOT], capture_output=True, text=True)
        sys.stderr.write(done.stderr)                                                          # (the warnings: under GITHUB_ACTIONS they are annotations of the pull request)
        self.assertEqual((done.returncode, done.stdout), (0, ""), done.stdout + done.stderr)


if __name__ == "__main__":
    unittest.main()
