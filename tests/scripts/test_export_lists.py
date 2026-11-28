#!/usr/bin/env python3
"""The two lists of the export (export/allowlist.txt, export/private.txt) and their checks (export/classify.py, export/check_export.py; export/README.md): suite 5.3 of ./run_tests.sh and the CI.

  - the pattern rules and both failures of the check, on small trees made for each test (a file on neither list, a file on both, `*` against `**`, `?`, brackets as plain letters, a line that
    starts with `!`, the overlay's public names, the dead patterns, the path regexes of the first export);
  - the real lists on this repository: every tracked file is on exactly one list, no server or site path is public, no game path is private, nothing of export/ is exported;
  - the scan of words on made-up trees: a public file that names something private is counted, a private one is not, the exit code is 0 until --strict.
"""
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXPORT = os.path.join(REPO, "export")
CLASSIFY = os.path.join(EXPORT, "classify.py")
CHECK = os.path.join(EXPORT, "check_export.py")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


classify = load("export_classify", CLASSIFY)


def run(script, *args):
    done = subprocess.run([sys.executable, script, *args], capture_output=True, text=True)
    return done.returncode, done.stdout


class Tree(tempfile.TemporaryDirectory):
    """A folder `tree` with the files `files` (path -> text), a git index of them (as the real check reads it; `git=False`: no checkout, which the script then walks), and the two lists `allow`
    and `private` (lists of lines) beside it, outside the tree."""

    def __init__(self, files, allow, private, git=True):
        super().__init__()
        self.tree = os.path.join(self.name, "tree")
        os.makedirs(self.tree)
        for path, text in files.items():
            full = os.path.join(self.tree, *path.split("/"))
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "w", encoding="utf-8") as f:
                f.write(text)
        if git:
            subprocess.run(["git", "init", "-q", self.tree], check=True)
            subprocess.run(["git", "-C", self.tree, "add", "-A", "-f"], check=True, capture_output=True)
        self.allow = self.write_list("allow.txt", allow)
        self.private = self.write_list("private.txt", private)

    def __enter__(self):
        return self

    def write_list(self, name, lines):
        path = os.path.join(self.name, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        return path

    def args(self, *more):
        return ["--repo", self.tree, "--allow", self.allow, "--private", self.private, *more]


def tracked_in_repo():
    out = subprocess.run(["git", "-C", REPO, "ls-files", "-z"], capture_output=True)
    return [p for p in out.stdout.decode("utf-8", "surrogateescape").split("\0") if p]


class ThePatternRules(unittest.TestCase):
    def test_a_file_on_exactly_one_list_passes(self):
        with Tree({"a.txt": "", "b/c.txt": ""}, ["a.txt"], ["b/**"]) as t:
            code, out = run(CLASSIFY, *t.args())
        self.assertEqual(code, 0)
        self.assertIn("public 1, private 1, unclassified 0, on both lists 0", out)

    def test_a_file_on_neither_list_fails_and_is_named(self):
        with Tree({"a.txt": "", "new.txt": ""}, ["a.txt"], ["b/**"]) as t:
            code, out = run(CLASSIFY, *t.args())
        self.assertEqual(code, 1)
        self.assertIn("UNCLASSIFIED  new.txt", out)
        self.assertNotIn("UNCLASSIFIED  a.txt", out)

    def test_a_file_on_both_lists_fails_and_is_named(self):
        with Tree({"a.txt": "", "b.txt": ""}, ["a.txt", "b.txt"], ["b.txt"]) as t:
            code, out = run(CLASSIFY, *t.args())
        self.assertEqual(code, 1)
        self.assertIn("ON BOTH LISTS b.txt", out)
        self.assertNotIn("ON BOTH LISTS a.txt", out)

    def test_quiet_keeps_the_count_and_the_exit_code(self):
        with Tree({"new.txt": ""}, ["a.txt"], ["b.txt"]) as t:
            code, out = run(CLASSIFY, *t.args("--quiet"))
        self.assertEqual(code, 1)
        self.assertNotIn("UNCLASSIFIED", out)
        self.assertIn("unclassified 1", out)

    def test_a_star_stays_in_its_folder_and_two_stars_do_not(self):
        rx = classify.glob_to_regex
        self.assertTrue(rx("docs/BOT_*").match("docs/BOT_FIGHT.md"))
        self.assertFalse(rx("docs/BOT_*").match("docs/BOT_X/deep.md"))
        self.assertTrue(rx("docs/**").match("docs/a/b/c.md"))
        self.assertFalse(rx("docs/**").match("docsx/a.md"))
        self.assertTrue(rx("a/?.txt").match("a/b.txt"))
        self.assertFalse(rx("a/?.txt").match("a//.txt"))

    def test_brackets_and_dots_are_plain_letters(self):
        rx = classify.glob_to_regex
        self.assertTrue(rx("Community-Maps/a [1].lvl").match("Community-Maps/a [1].lvl"))
        self.assertFalse(rx("Community-Maps/a [1].lvl").match("Community-Maps/a 1.lvl"))
        self.assertFalse(rx("a.b").match("axb"))

    def test_the_last_line_that_matches_wins_inside_one_list(self):
        with Tree({"w/a.txt": "", "w/keep.txt": "", "w/b.txt": ""}, ["w/**", "!w/a.txt", "w/b.txt"], ["w/a.txt"]) as t:
            code, out = run(CLASSIFY, *t.args("--list", "public"))
            self.assertEqual(code, 0)
            self.assertEqual(sorted(out.split()), ["w/b.txt", "w/keep.txt"])
            code, out = run(CLASSIFY, *t.args("--list", "private"))
            self.assertEqual(out.split(), ["w/a.txt"])

    def test_an_overlay_file_is_exported_under_its_public_name(self):
        files = {"README.md": "private", "export/overlay/README.md": "public", "export/overlay/.github/ci.yml": "", "export/overlay/.gitkeep": ""}
        allow = ["export/overlay/**", "!export/overlay/.gitkeep"]
        private = ["README.md", "export/**", "!export/overlay/**", "export/overlay/.gitkeep"]
        with Tree(files, allow, private) as t:
            code, out = run(CLASSIFY, *t.args("--list", "public"))
            self.assertEqual(code, 0)
            self.assertEqual(sorted(out.split()), [".github/ci.yml", "README.md"])
            code, out = run(CLASSIFY, *t.args("--list", "private"))
            self.assertEqual(sorted(out.split()), ["README.md", "export/overlay/.gitkeep"])

    def test_dead_patterns_are_listed_and_do_not_fail_the_check(self):
        with Tree({"a.txt": ""}, ["a.txt", "later/**"], ["b.txt"]) as t:
            code, out = run(CLASSIFY, *t.args("--dead"))
        self.assertEqual(code, 0)
        self.assertIn("DEAD (allowlist)  later/**", out)
        self.assertIn("DEAD (private)    b.txt", out)
        self.assertNotIn("DEAD (allowlist)  a.txt", out)

    def test_the_regexes_of_the_first_export_select_what_the_list_selects(self):
        files = {"g/a.txt": "", "g/deep/b.txt": "", "p/c.txt": "", "export/overlay/R.md": "", "export/x.txt": ""}
        allow = ["g/**", "export/overlay/**"]
        private = ["p/**", "export/**", "!export/overlay/**"]
        with Tree(files, allow, private) as t:
            code, out = run(CLASSIFY, *t.args("--filter-repo-paths"))
            self.assertEqual(code, 0)
            regexes = [re.compile(line[len("regex:"):]) for line in out.splitlines()]
        self.assertEqual(sorted(f for f in files if any(r.match(f) for r in regexes)), ["g/a.txt", "g/deep/b.txt"])


class TheRealLists(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = tracked_in_repo()
        cls.allow = classify.read_list(os.path.join(EXPORT, "allowlist.txt"))
        cls.private = classify.read_list(os.path.join(EXPORT, "private.txt"))
        cls.pub, cls.priv, cls.none, cls.both, _a, _p = classify.classify(cls.files, cls.allow, cls.private)

    def test_every_tracked_file_is_on_exactly_one_list(self):
        self.assertGreater(len(self.files), 1000)
        self.assertEqual(self.none, [], "files on neither list: put each on one (export/README.md)")
        self.assertEqual(self.both, [], "files on both lists")
        self.assertEqual(len(self.pub) + len(self.priv), len(self.files))

    def test_the_command_agrees_with_the_function(self):
        code, out = run(CLASSIFY, "--repo", REPO)
        self.assertEqual(code, 0, out)

    def test_nothing_of_the_server_or_the_site_is_public(self):
        public = set(self.pub)
        for prefix in ("hub/", "imgconv/", "ai_skills/", "src/ants_server/", "include/ants_server/", "src/ants_db/", "include/ants_db/", "src/ants_ctl/", "include/ants_ctl/",
                       "src/ants_history/", "include/ants_history/", "src/ants_thumb/", "third_party/sqlite/", "tests/test_server/", "tests/test_db/", "tests/test_ctl/", "web/home/", "export/",
                       "Dockerfile", "docker-compose", ".github/workflows/"):
            self.assertEqual([f for f in public if f.startswith(prefix)], [], prefix)
        for name in ("docker/nginx.conf", "web/lobby.html", "web/matches.html", "web/admin.html", "web/legal.html", "web/site_nav.js", "AGENTS.md", "README.md", "SECURITY.md"):
            if name in self.files:
                self.assertNotIn(name, public, name)

    def test_the_game_is_public(self):
        public = set(self.pub)
        for prefix in ("src/ants_sim/", "src/ants_net/", "src/ants_ai/", "src/ants_replay/", "src/ants_assets/", "src/ants_app/", "include/ants_sim/", "include/ants_net/", "include/ants_ai/",
                       "include/ants_replay/", "include/ants_assets/", "include/ants_app/", "Original-Ants/", "Community-Maps/", "asset_catalog/", "assets/", "tests/test_sim/", "tests/test_net/"):
            files = [f for f in self.files if f.startswith(prefix)]
            self.assertGreater(len(files), 0, prefix)
            self.assertEqual([f for f in files if f not in public], [], prefix)
        for name in ("web/shell.html", "web/mapmaker.html", "CMakeLists.txt", "CHANGELOG.md", "VERSION", "LICENSE", "run_tests.sh"):
            self.assertIn(name, public, name)

    def test_the_export_folder_is_never_exported_and_its_overlay_is_exported_without_its_prefix(self):
        listed = subprocess.run([sys.executable, CLASSIFY, "--repo", REPO, "--list", "public"], capture_output=True, text=True).stdout.split("\n")
        self.assertEqual([p for p in listed if p.startswith("export/")], [])
        self.assertNotIn(".gitkeep", [os.path.basename(p) for p in listed])
        for f in self.files:
            if f.startswith("export/overlay/") and f != "export/overlay/.gitkeep":
                self.assertIn(f[len("export/overlay/"):], listed)

    def test_the_path_regexes_select_the_public_files_that_are_not_overlay(self):
        out = subprocess.run([sys.executable, CLASSIFY, "--repo", REPO, "--filter-repo-paths"], capture_output=True, text=True).stdout
        regexes = [re.compile(line[len("regex:"):]) for line in out.splitlines()]
        wanted = {f for f in self.pub if not f.startswith("export/")}
        picked = {f for f in self.files if any(r.match(f) for r in regexes)}
        self.assertEqual(sorted(picked - wanted)[:5], [])
        self.assertEqual(sorted(wanted - picked)[:5], [])


class TheScanOfWords(unittest.TestCase):
    ALLOW = ["pub/**", "export/overlay/**"]
    PRIVATE = ["priv/**", "export/**", "!export/overlay/**"]

    def scan(self, files, *more):
        with Tree(files, self.ALLOW, self.PRIVATE) as t:
            return run(CHECK, *t.args(*more))

    def test_a_public_file_that_names_something_private_is_counted_and_the_kind_is_named(self):
        code, out = self.scan({"pub/a.cpp": '#include "ants_server/room.hpp"\n', "pub/b.md": "see docs/SERVER.md\n", "pub/clean.md": "the game server is fine\n"}, "--list")
        self.assertEqual(code, 0)
        self.assertIn("2 of 3 public text files", out)
        self.assertRegex(out, r"pub/a\.cpp .*include-private-header=1")
        self.assertRegex(out, r"pub/b\.md .*private-doc=1")
        self.assertNotIn("pub/clean.md", out)

    def test_a_private_file_may_name_anything(self):
        code, out = self.scan({"priv/a.cpp": '#include "ants_server/room.hpp"\n', "pub/clean.txt": "nothing\n"})
        self.assertEqual(code, 0)
        self.assertIn("0 of 1 public text files", out)

    def test_an_overlay_file_is_scanned_because_it_is_public(self):
        code, out = self.scan({"export/overlay/README.md": "Run it behind nginx.\n", "export/notes.md": "nginx\n"}, "--list")
        self.assertIn("1 of 1 public text files", out)
        self.assertIn("export/overlay/README.md", out)

    def test_the_exit_code_is_zero_until_strict(self):
        files = {"pub/a.cpp": "ants_server\n"}
        self.assertEqual(self.scan(files)[0], 0)
        self.assertEqual(self.scan(files, "--strict")[0], 1)
        self.assertEqual(self.scan({"pub/a.cpp": "nothing\n"}, "--strict")[0], 0)

    def test_the_tsv_has_a_row_for_each_file(self):
        with Tree({"pub/a.cpp": "ants_server\n"}, self.ALLOW, self.PRIVATE) as t:
            out_path = os.path.join(t.name, "rows.tsv")
            run(CHECK, *t.args("--tsv", out_path))
            with open(out_path, encoding="utf-8") as f:
                rows = f.read().splitlines()
        self.assertEqual(rows[0], "area\tfile\thits\tkinds\texamples")
        self.assertEqual([r.split("\t")[1] for r in rows[1:]], ["pub/a.cpp"])

    def test_the_real_repository_is_scanned_without_a_failure(self):
        code, out = run(CHECK, "--repo", REPO)
        self.assertEqual(code, 0, out)
        self.assertIn("public text files", out)


if __name__ == "__main__":
    unittest.main()
