#!/usr/bin/env python3
"""Tests of tools/check_hygiene.py: each check on small synthetic trees, and the real repository (which must pass).

A checkout of the game alone does not have the paths that tools/server_paths.txt lists. What these tests hold for such a tree (ListedPaths, LinksOut, Cli): an entry of a ratchet table for a
file that is not there is skipped when the list names the file and is a finding when it does not; a file that is there is held to its entry like any other; a markdown link in a document to a
listed path that is not there is a warning, printed after the findings, and the exit status stays 0; a link to a listed path that is there is followed."""
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

    def listed(self, *patterns):
        """The list of this tree's paths of the game server, the site's services and their tests (tools/server_paths.txt): one pattern a line."""
        self.put("tools/server_paths.txt", "".join(p + "\n" for p in patterns))


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


class LinksOut(Tree):
    """A link in a document to a path that tools/server_paths.txt lists and the tree does not have leaves the checkout: a warning, one for each such link on a line."""
    LIST = ("docs/SERVER.md", "hub/", "tests/scripts/test_hub_*", "src/ants_server/")

    def warned(self):
        """(file, line, path) of each warning of the tree, in the order printed"""
        rows = []
        for text in ch.run_warnings(self.root):
            m = re.match(r"^(.+):(\d+): warning: the link to (\S+) leaves the checkout \(", text)
            self.assertTrue(m, text)
            rows.append((m.group(1), int(m.group(2)), m.group(3)))
        return rows

    def test_a_link_to_a_listed_path_that_is_not_there_is_a_warning_with_its_line(self):
        self.listed(*self.LIST)
        self.put("README.md", "[ok](docs/A.md) [s](docs/SERVER.md#top) [h](hub/) [h2](hub)\n"                                   # 1: a page, a listed page, a listed folder twice
                              "[t](tests/scripts/test_hub_core.py?x=1) [gone](docs/GONE.md) [web](https://x.org/docs/SERVER.md) [a](#top)\n"    # 2: a listed glob; not listed; web; heading
                              "```\n[code](docs/SERVER.md)\n```\n"                                                                # 3-5: fenced code is no link
                              "[ref]: <src/ants_server/room.cpp>\n")                                                              # 6: a reference-style link
        self.put("docs/A.md", "see [up](../docs/SERVER.md) and [x](../hub/README.md)\n")
        self.assertEqual(self.warned(), [("README.md", 1, "docs/SERVER.md"), ("README.md", 1, "hub"), ("README.md", 2, "tests/scripts/test_hub_core.py"),
                                         ("README.md", 6, "src/ants_server/room.cpp"), ("docs/A.md", 1, "docs/SERVER.md"), ("docs/A.md", 1, "hub/README.md")])
        self.assertIn("tools/server_paths.txt lists it", ch.run_warnings(self.root)[0])

    def test_only_the_documents_are_looked_at(self):
        self.listed(*self.LIST)
        for rel in ("src/NOTES.md", "Community-Maps/README.md", "web/page.md", "tests/scripts/README.md", "docs/page.txt"):
            self.put(rel, "[s](/docs/SERVER.md) [r](../docs/SERVER.md) [d](../../docs/SERVER.md)\n")
        self.assertEqual(self.warned(), [])
        self.put("docs/sub/deep.md", "[s](../SERVER.md)\n")
        self.put("NOTES.md", "[s](docs/SERVER.md)\n")
        self.assertEqual(self.warned(), [("NOTES.md", 1, "docs/SERVER.md"), ("docs/sub/deep.md", 1, "docs/SERVER.md")])

    def test_without_a_list_or_for_a_path_it_does_not_name_there_is_no_warning(self):
        self.put("README.md", "[s](docs/SERVER.md) [h](hub/) [x](docs/OTHER.md)\n")
        self.assertEqual(self.warned(), [])                                       # (a tree that has no list names nothing)
        self.listed("hub/")
        self.assertEqual(self.warned(), [("README.md", 1, "hub")])                # (docs/SERVER.md and docs/OTHER.md are not on this list)

    def test_a_link_to_a_listed_path_that_is_there_is_followed_and_is_no_warning(self):
        self.listed(*self.LIST)
        self.put("README.md", "[s](docs/SERVER.md) [h](hub/)\n")
        self.put("docs/SERVER.md", "[b](B.md)\n")
        self.put("docs/B.md", "reached through the listed page\n")
        self.put("hub/store.py", "x = 1\n")
        self.assertEqual((ch.run_warnings(self.root), ch.check_reachable(self.root, self.files())), ([], []))
        os.remove(os.path.join(self.root, "docs", "SERVER.md"))                  # the same tree without the listed page and the folder
        shutil.rmtree(os.path.join(self.root, "hub"))
        self.assertEqual(self.warned(), [("README.md", 1, "docs/SERVER.md"), ("README.md", 1, "hub")])
        out = ch.check_reachable(self.root, self.files())
        self.assertEqual([x.split(":")[0] for x in out], ["docs/B.md"])           # (the page that only the listed page linked is not reached: a finding, as before)


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


class ListedPaths(Tree):
    """An entry of a ratchet table (page ceilings, line ceilings, kept spellings) for a file that is not there: skipped when tools/server_paths.txt names the file, a finding when it does not."""
    LIST = ("src/ants_server/", "tests/scripts/test_hub_*", "docs/SERVER.md")

    def names(self, out):
        return [line.split(":")[0] for line in out]

    def test_a_page_ceiling_for_a_listed_page_that_is_not_there_is_skipped_and_for_another_is_a_finding(self):
        self.listed(*self.LIST)
        out = ch.check_doc_sizes(self.root, self.files(), ceilings={"docs/SERVER.md": 150000, "docs/GONE.md": 150000})
        self.assertEqual(self.names(out), ["docs/GONE.md"])
        self.assertIn("in the ceiling table but not a tracked file", out[0])

    def test_a_line_ceiling_for_a_listed_file_that_is_not_there_is_skipped_and_for_another_is_a_finding(self):
        self.listed(*self.LIST)
        out = ch.check_lines(self.root, self.files(), ceilings={"src/ants_server/room.cpp": 2000, "src/ants_net/gone.cpp": 2000})
        self.assertEqual(self.names(out), ["src/ants_net/gone.cpp"])
        self.assertIn("in the line ceiling table but not a tracked file", out[0])

    def test_a_kept_spelling_for_a_listed_file_that_is_not_there_is_skipped_and_for_another_is_a_finding(self):
        self.listed(*self.LIST)
        kept = {"src/ants_server/history_record.cpp": [r"\bx\b"], "web/gone.js": [r"\bx\b"]}
        out = ch.check_spelling_table(self.root, self.files(), kept)
        self.assertEqual(self.names(out), ["web/gone.js"])
        self.assertIn("in the spelling table but not tracked", out[0])

    def test_a_listed_file_that_is_there_is_held_to_its_entry_like_any_other(self):
        self.listed(*self.LIST)
        big = ch.DOC_LIMIT * 2
        self.put("docs/SERVER.md", size=big)
        self.put("src/ants_server/small.cpp", lines=10)
        self.put("src/ants_server/room.cpp", "nothing to keep\n")
        files = self.files()
        self.assertIn("above its ceiling", ch.check_doc_sizes(self.root, files, ceilings={"docs/SERVER.md": big - 1})[0])
        self.assertIn("no longer exceeds", ch.check_lines(self.root, files, ceilings={"src/ants_server/small.cpp": 2000})[0])
        self.assertIn("the kept spelling pattern", ch.check_spelling_table(self.root, files, {"src/ants_server/room.cpp": [r"\bx\b"]})[0])

    def test_the_list_names_a_path_by_directory_glob_or_file_as_the_module_that_reads_it_does(self):
        self.listed(*self.LIST)
        named = ["src/ants_server/deep/room.cpp", "tests/scripts/test_hub_core.py", "docs/SERVER.md"]
        not_named = ["src/ants_serverx/room.cpp", "tests/scripts/sub/test_hub_core.py", "tests/scripts/test_web_x.py", "docs/SERVER.md.bak"]
        out = ch.check_lines(self.root, self.files(), ceilings={rel: 2000 for rel in named + not_named})
        self.assertEqual(sorted(self.names(out)), sorted(not_named))

    def test_a_tree_that_has_no_list_skips_nothing(self):
        out = ch.check_lines(self.root, self.files(), ceilings={"src/ants_server/room.cpp": 2000})
        self.assertEqual(self.names(out), ["src/ants_server/room.cpp"])

    def test_a_line_of_the_list_that_is_no_pattern_is_refused_by_the_module_that_reads_it(self):
        self.listed("src/ants_server/", "/etc/x")
        with self.assertRaises(ValueError) as caught:
            ch.server_matcher(self.root)
        self.assertIn(":2:", str(caught.exception))


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
        self.assertEqual(len([l for l in lines if "remove it from the table" not in l]), 4)  # the ceiling tables name files that this tree lacks

    def test_list_prints_numbers(self):
        done = self.run_tool("--list")
        self.assertEqual(done.returncode, 0)
        self.assertIn("ASSERT_TRUE definitions", done.stdout)

    def test_a_tree_without_the_listed_paths_passes_with_a_warning_for_each_link_that_leaves_it(self):
        # (every entry of the tables names a path that the list names and the tree lacks, and the two ASSERT_TRUE definitions that the check counts are there: nothing else is left to find)
        self.listed("docs/SERVER.md", *(list(ch.DOC_CEILINGS) + list(ch.LINE_CEILINGS) + list(ch.SPELLING_KEPT)))
        self.put("tests/common/test_assert.hpp", "#define ASSERT_TRUE(x) a\n#define ASSERT_TRUE(x) b\n")
        self.put("README.md", "[server](docs/SERVER.md)\n")
        done = self.run_tool("--root", self.root)
        lines = done.stdout.splitlines()
        self.assertEqual((done.returncode, len(lines)), (0, 1), done.stdout + done.stderr)
        self.assertTrue(lines[0].startswith("README.md:1: warning: the link to docs/SERVER.md leaves the checkout"), lines[0])
        self.put("docs/A.md", "no link reaches this page\n")                       # a finding: it comes first, the warning after it, and the status is 1
        done = self.run_tool("--root", self.root)
        lines = done.stdout.splitlines()
        self.assertEqual((done.returncode, [x.split(":")[0] for x in lines]), (1, ["docs/A.md", "README.md"]), done.stdout + done.stderr)
        self.assertNotIn(": warning: ", lines[0])
        self.assertIn(": warning: ", lines[1])


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
        findings = [line for line in done.stdout.splitlines() if ": warning: " not in line]
        self.assertEqual((done.returncode, findings), (0, []), done.stdout + done.stderr)
        server = ch.server_matcher(ROOT)
        if any(server(rel) for rel in ch.tracked_files(ROOT)):                        # (a tree that has the listed files: its links are followed, and it prints nothing)
            self.assertEqual(done.stdout, "")


if __name__ == "__main__":
    unittest.main()
