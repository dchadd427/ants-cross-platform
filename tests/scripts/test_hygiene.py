#!/usr/bin/env python3
"""Tests of tools/check_hygiene.py: each check on small synthetic trees, and the real repository (which must pass)."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import check_hygiene as ch  # noqa: E402


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


class DocSize(Tree):
    def test_over_limit_fails_and_under_passes(self):
        self.put("docs/A.md", size=ch.DOC_LIMIT)
        self.put("docs/B.md", size=ch.DOC_LIMIT + 1)
        self.put("TOP.md", size=ch.DOC_LIMIT + 1)
        out = ch.check_doc_sizes(self.root, self.files(), ceilings={})
        self.assertEqual(sorted(l.split(":")[0] for l in out), ["TOP.md", "docs/B.md"])

    def test_ceiling_holds_grows_and_shrinks(self):
        big = ch.DOC_LIMIT * 2
        self.put("docs/BIG.md", size=big)
        self.assertEqual(ch.check_doc_sizes(self.root, self.files(), ceilings={"docs/BIG.md": big}), [])
        self.assertIn("above its ceiling", ch.check_doc_sizes(self.root, self.files(), ceilings={"docs/BIG.md": big - 1})[0])
        out = ch.check_doc_sizes(self.root, self.files(), ceilings={"docs/BIG.md": int(big * 1.2)})
        self.assertIn("lower the ceiling to %d" % big, out[0])
        self.assertEqual(ch.check_doc_sizes(self.root, self.files(), ceilings={"docs/BIG.md": int(big * 1.04)}), [])

    def test_ceiling_of_a_missing_or_small_page(self):
        self.put("docs/SMALL.md", size=10)
        out = ch.check_doc_sizes(self.root, self.files(), ceilings={"docs/SMALL.md": 10, "docs/GONE.md": 5})
        self.assertTrue(any("docs/SMALL.md" in l and "remove it" in l for l in out))
        self.assertTrue(any("docs/GONE.md" in l and "remove it" in l for l in out))

    def test_other_files_are_not_documents(self):
        self.put("docs/data.json", size=ch.DOC_LIMIT * 2)
        self.put("src/NOTES.md", size=ch.DOC_LIMIT * 2)
        self.assertEqual(ch.check_doc_sizes(self.root, self.files(), ceilings={}), [])


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

    def test_deep_pages_need_only_a_link_from_a_reachable_page(self):
        self.put("README.md", "[s](docs/sub/README.md)\n")
        self.put("docs/sub/README.md", "[d](d/one.md)\n")
        self.put("docs/sub/d/one.md", "one\n")
        self.put("docs/sub/d/two.md", "never linked, and not a folder page\n")
        self.assertEqual(ch.check_reachable(self.root, self.files()), [])


class Forbidden(Tree):
    def test_each_string_is_found(self):
        for i, s in enumerate(["implementation_plan", "ants-plans", "DECISIONS.md", "claude.ai/code", "Co-Authored-By", "Claude-Session", "Project Thread", "THE OTHER THREAD"]):
            self.put("src/f%d.cpp" % i, "// see %s here\n" % s)
        out = ch.check_forbidden(self.root, self.files(), allowed=set())
        self.assertEqual(len(out), 8)
        self.assertTrue(out[0].startswith("src/f0.cpp:1:"))

    def test_allowlist_binary_maps_and_previews_are_skipped(self):
        self.put("AGENTS.md", "DECISIONS.md\n")
        self.put("Community-Maps/m.lvl", "implementation_plan\n")
        self.put("previews/p.txt", "ants-plans\n")
        self.put("third_party/sqlite/sqlite3.c", "/* the other thread */\n")
        with open(os.path.join(self.root, "b.bin"), "wb") as f:
            f.write(b"\0\0claude.ai/code")
        self.put("docs/ok.md", "plain words\n")
        self.assertEqual(ch.check_forbidden(self.root, self.files(), allowed={"AGENTS.md"}), [])


class Lines(Tree):
    def test_limits_by_kind(self):
        self.put("tests/a/test_ok.cpp", lines=ch.TEST_LINES)
        self.put("tests/a/test_big.cpp", lines=ch.TEST_LINES + 1)
        self.put("src/x/ok.cpp", lines=ch.PROD_LINES)
        self.put("src/x/big.cpp", lines=ch.PROD_LINES + 1)
        self.put("web/big.html", lines=ch.PROD_LINES + 1)
        self.put("tools/big.py", lines=ch.PROD_LINES + 1)
        self.put("src/x/generated.inc", lines=ch.PROD_LINES * 3)
        self.put("docs/x.md", lines=ch.PROD_LINES * 3)
        out = ch.check_lines(self.root, self.files(), ceilings={})
        self.assertEqual(sorted(l.split(":")[0] for l in out), ["src/x/big.cpp", "tests/a/test_big.cpp", "tools/big.py", "web/big.html"])

    def test_ratchet(self):
        n = ch.PROD_LINES + 1000
        self.put("src/x/big.cpp", lines=n)
        f = self.files()
        self.assertEqual(ch.check_lines(self.root, f, ceilings={"src/x/big.cpp": n}), [])
        self.assertIn("above its ceiling", ch.check_lines(self.root, f, ceilings={"src/x/big.cpp": n - 1})[0])
        self.assertIn("lower the ceiling to %d" % n, ch.check_lines(self.root, f, ceilings={"src/x/big.cpp": int(n * 1.2)})[0])
        self.assertEqual(ch.check_lines(self.root, f, ceilings={"src/x/big.cpp": int(n * 1.05)}), [])

    def test_table_entries_that_no_longer_apply(self):
        self.put("src/x/small.cpp", lines=10)
        out = ch.check_lines(self.root, self.files(), ceilings={"src/x/small.cpp": 2000, "src/x/gone.cpp": 2000})
        self.assertTrue(any("small.cpp" in l and "remove it from the table" in l for l in out))
        self.assertTrue(any("gone.cpp" in l and "remove it from the table" in l for l in out))

    def test_last_line_without_newline_counts(self):
        self.assertEqual(ch.line_count(b"a\nb"), 2)
        self.assertEqual(ch.line_count(b"a\nb\n"), 2)
        self.assertEqual(ch.line_count(b""), 0)


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


class Cli(Tree):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_hygiene.py")] + list(args), capture_output=True, text=True)

    def test_usage_error_is_2(self):
        self.assertEqual(self.run_tool("--bogus").returncode, 2)
        self.assertEqual(self.run_tool("--root", os.path.join(self.root, "nope")).returncode, 2)

    def test_findings_are_one_line_each_with_exit_1(self):
        self.put("README.md", "hello\n")
        self.put("docs/A.md", "orphan\n")
        self.put("src/x.cpp", "// ants-plans\n")
        done = self.run_tool("--root", self.root)
        self.assertEqual(done.returncode, 1)
        lines = done.stdout.splitlines()
        self.assertTrue(any(l.startswith("docs/A.md:") for l in lines))
        self.assertTrue(any(l.startswith("src/x.cpp:1:") for l in lines))
        self.assertTrue(any("not named in any CMakeLists.txt" in l for l in lines))
        self.assertTrue(any(l.startswith("ASSERT_TRUE:") for l in lines))  # no definitions here: fewer than the constant
        self.assertEqual(len([l for l in lines if "remove it from the table" not in l]), 4)  # the ceiling tables name files that this tree lacks

    def test_list_prints_numbers(self):
        done = self.run_tool("--list")
        self.assertEqual(done.returncode, 0)
        self.assertIn("ASSERT_TRUE definitions", done.stdout)


class RealRepository(unittest.TestCase):
    def test_repository_passes(self):
        done = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_hygiene.py"), "--root", ROOT], capture_output=True, text=True)
        self.assertEqual((done.returncode, done.stdout), (0, ""), done.stdout + done.stderr)


if __name__ == "__main__":
    unittest.main()
