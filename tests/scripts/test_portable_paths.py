#!/usr/bin/env python3
"""tools/check_portable_paths.py: no tracked path is one that a Windows or macOS checkout cannot hold (run by ./run_tests.sh --fast and by the CI, so by every pull request on Linux).

Linux holds any name. A pull request of documents, pages and their tests skips the macOS and Windows jobs (docs/WORKFLOW.md, "Light checks"), so nothing else would refuse a case twin
(`web/Lobby.html` beside `web/lobby.html`), a name that Windows reserves or forbids (`docs/aux.md`, `a:b.md`, a trailing dot or space) or a path that is too long, and after a merge every checkout
on Windows would fail until a pull request deleted the file. What these tests hold:
  - the rules, on tables: the paths that every system can hold, the paths that cannot be held and the sentence that says why, the groups of paths that are one name on macOS and Windows;
  - this repository passes (exit 0), and a scratch repository with each bad kind fails (exit 1) and names the path, quoted, in ASCII, so that no line of the log can begin with a workflow command;
  - the scratch repositories are made with git's plumbing and no working tree, so that a file system that folds case (macOS, Windows) cannot make two of the names one file;
  - a folder that is no repository, and an option that is none, are an error (exit 2) and not a pass.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOL = os.path.join(REPO, "tools", "check_portable_paths.py")
sys.path.insert(0, os.path.join(REPO, "tools"))
import check_portable_paths as cpp  # noqa: E402

GIT_ENV = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.org", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@example.org")

# Paths that every system can hold (the last two are as long as the rules allow)
GOOD = [
    "README.md", ".gitignore", "LICENSE", ".github/workflows/ci.yml", "docs/reverse_engineering/movement/04_occupancy_passability_and_blocking.md", "Community-Maps/Bombz Away.lvl",
    "Community-Maps/previews/!!!!!!!!!!!!!!!! Danger_World!_II!!!.webp", "tests/data/lvl_core/blank_extended_300x400_g485_e6_m10.read.json", "a b/c d.txt", "x/ leading space.md", "x/a  b.md",
    "x/aux1.md", "x/auxiliary.md", "x/console.md", "x/com10.txt", "x/lpt.md", "x/nul-ls.md", "x/prn_a", "x/.aux", "x/a.aux", "x/xcon.md", "x/con-.md", "x/CONSOLE", "x/lpt10", "x/com.md",
    "docs/caf\u00e9.md", "docs/\u65e5\u672c\u8a9e.md", "docs/\U0001F600.md", "x/-dash", "x/~tilde", "x/#hash", "x/%percent", "x/@at", "x/+plus", "x/a,b;c=d&e'f(g)[h]{i}.md", "x/dots.in.name.md",
    "x/...a", "x/a...b", "x/B", "y/B", "x/" + "a" * 198, "a/" * 95 + "\U0001F600" * 5, "x/" + "\u00e9" * 127 + "a",
]

# (a path that cannot be held by itself, a word of the sentence that says why)
DEVICE, CHARACTER, ENDING, LONG, BYTES = "DOS device", "does not allow", "ends with a space or a dot", "characters long", "bytes long"
BAD = [
    ("docs/aux.md", DEVICE), ("docs/AUX.md", DEVICE), ("docs/Aux", DEVICE), ("docs/nul", DEVICE), ("docs/NUL.tar.gz", DEVICE), ("docs/con", DEVICE), ("docs/con.foo.bar", DEVICE),
    ("docs/prn.txt", DEVICE), ("docs/com1", DEVICE), ("docs/COM9.log", DEVICE), ("docs/com0.md", DEVICE), ("docs/lpt1.txt", DEVICE), ("docs/LPT9", DEVICE), ("docs/lpt0", DEVICE),
    ("docs/aux .md", DEVICE), ("docs/COM\u00b9.txt", DEVICE), ("docs/LPT\u00b3", DEVICE), ("docs/CONIN$", DEVICE), ("docs/conout$.txt", DEVICE), ("aux/readme.md", DEVICE),
    ("a/b/NUL/c.md", DEVICE),
    ("docs/a:b.md", CHARACTER), ("C:/x.md", CHARACTER), ("docs/a.md:stream", CHARACTER), ("docs/q?.md", CHARACTER), ("docs/a*.md", CHARACTER), ('docs/a"b.md', CHARACTER),
    ("docs/a<b.md", CHARACTER), ("docs/a>b.md", CHARACTER), ("docs/a|b.md", CHARACTER), ("docs/a\\b.md", CHARACTER), ("docs/c\x01.md", CHARACTER), ("docs/new\nline.md", CHARACTER),
    ("docs/tab\t.md", CHARACTER), ("docs/us\x1f.md", CHARACTER),
    ("docs/dot.", ENDING), ("docs/space ", ENDING), ("docs/a.md.", ENDING), ("dir./x.md", ENDING), ("dir /x.md", ENDING), ("docs/...", ENDING), ("docs/a. ", ENDING),
    ("x/" + "a" * 199, LONG), ("a/" * 100 + "b", LONG), ("a/" * 95 + "\U0001F600" * 6, LONG), ("x/" + "\u00e9" * 128, BYTES), ("x/" + "\u00e9" * 127 + "ab", BYTES),
]

# Groups of paths that are one name on a file system that ignores case and Unicode forms (macOS, Windows): each of the paths of a group is named
TWINS = [
    ("web/lobby.html", "web/Lobby.html"), ("docs/a.md", "docs/A.md"), ("README.md", "readme.md"), ("Web/a.js", "web/a.js"), ("Web/a.js", "web/b.js"), ("docs/caf\u00e9.md", "docs/cafe\u0301.md"),
    ("docs/\u00c4.md", "docs/\u00e4.md"), ("docs/\u0391.md", "docs/\u03b1.md"), ("docs/Foo", "docs/foo/bar"), ("a/B/c", "a/b/d"), ("x/ab.md", "x/aB.md", "x/AB.md"),
]
# Groups that are not twins
NOT_TWINS = [("web/x.js", "docs/x.js"), ("a/B", "b/B"), ("docs/a.md", "docs/b.md"), ("docs/a.md", "docs/a.md.bak"), ("docs/ab/c", "docs/a/bc"), ("docs/a.md", "docs/a.md")]


def encode(path):
    return path.encode("utf-8")


class TheRules(unittest.TestCase):
    def test_every_path_that_every_system_can_hold_passes(self):
        for path in GOOD:
            with self.subTest(path[:60]):
                self.assertEqual(cpp.problems_of(encode(path)), [])
        self.assertEqual(cpp.check([encode(p) for p in GOOD]), [])
        self.assertEqual(len(set(GOOD)), len(GOOD), "a path twice in the table")

    def test_every_bad_path_gets_the_sentence_that_says_why(self):
        for path, word in BAD:
            with self.subTest(path[:60]):
                reasons = cpp.problems_of(encode(path))
                self.assertTrue(any(word in reason for reason in reasons), (path, reasons))
                found = cpp.check([encode(path)])
                self.assertEqual([p for p, _ in found], [path] * len(reasons))

    def test_a_path_with_two_faults_gets_both(self):
        reasons = cpp.problems_of(b"docs/aux.")
        self.assertEqual(len(reasons), 2, reasons)
        self.assertTrue(any(DEVICE in r for r in reasons) and any(ENDING in r for r in reasons))
        self.assertEqual(len(cpp.problems_of(b"docs/a:b?/CON/x ")), 3)           # (the characters of one name are one sentence: a sentence for each name that has a fault)

    def test_what_is_not_utf8_is_refused_however_it_is_wrong(self):
        for raw in (b"docs/\xff.md", b"docs/\xc0\xaf.md", b"docs/\xed\xa0\x80.md", b"\xfe", b"docs/caf\xe9.md"):
            with self.subTest(raw):
                reasons = cpp.problems_of(raw)
                self.assertEqual(len(reasons), 1, reasons)
                self.assertIn("not valid UTF-8", reasons[0])
                self.assertEqual([p for p, _ in cpp.check([raw])], [raw.decode("utf-8", "surrogateescape")])

    def test_the_limits_are_where_they_are_said_to_be(self):
        self.assertEqual((cpp.MAX_PATH_UNITS, cpp.MAX_NAME_BYTES), (200, 255))
        self.assertEqual(cpp.problems_of(encode("x/" + "a" * (cpp.MAX_PATH_UNITS - 2))), [])
        self.assertEqual(len(cpp.problems_of(encode("x/" + "a" * (cpp.MAX_PATH_UNITS - 1)))), 1)
        emoji = "\U0001F600"                                                            # (one character, two UTF-16 units, as Windows counts: 196 characters are 202 units here)
        self.assertEqual(len("a/" * 95 + emoji * 6), 196)
        self.assertEqual(cpp.problems_of(encode("a/" * 95 + emoji * 5)), [])
        self.assertTrue(any(LONG in r for r in cpp.problems_of(encode("a/" * 95 + emoji * 6))))
        self.assertEqual(cpp.problems_of(encode("x/" + "\u00e9" * 127 + "a")), [])      # (255 bytes of UTF-8)
        self.assertTrue(any(BYTES in r for r in cpp.problems_of(encode("x/" + "\u00e9" * 127 + "ab"))))

    def test_every_dos_device_is_a_device_in_any_case_with_or_without_an_extension(self):
        names = ["CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"] + ["%s%s" % (kind, digit) for kind in ("COM", "LPT") for digit in "0123456789\u00b9\u00b2\u00b3"]
        self.assertEqual(len(names), 32)
        for name in names:
            for spelling in (name, name.lower(), name.capitalize(), name + ".txt", name.lower() + ".tar.gz", name + " .md"):
                with self.subTest(spelling):
                    self.assertTrue(any(DEVICE in r for r in cpp.problems_of(encode("docs/" + spelling))), spelling)
        for name in ("COM10", "LPT10", "COM", "LPT", "CONSOLE", "AUXILIARY", "NULL", "PRN2", "COMX", "XCON", "CON-", "AUX_1"):
            with self.subTest(name):
                self.assertEqual(cpp.problems_of(encode("docs/" + name + ".md")), [])

    def test_names_that_are_one_on_macos_and_windows_are_all_named(self):
        for group in TWINS:
            with self.subTest(group):
                found = cpp.twins(list(group))
                self.assertEqual(sorted(found), sorted(group), found)
                self.assertEqual(sorted(p for p, _ in cpp.check([encode(p) for p in group])), sorted(group))
        for group in NOT_TWINS:
            with self.subTest(group):
                self.assertEqual(cpp.twins(list(group)), {})
                self.assertEqual(cpp.check([encode(p) for p in group]), [])

    def test_a_folder_that_differs_in_case_is_named_as_a_folder_and_a_file_as_a_path(self):
        found = cpp.twins(["Web/a.js", "web/b.js", "docs/a.md", "docs/A.md"])
        self.assertIn('folder "Web"', found["Web/a.js"])
        self.assertIn('folder "web"', found["web/b.js"])
        self.assertIn('path "docs/a.md"', found["docs/a.md"])
        self.assertIn('path "docs/A.md"', found["docs/A.md"])

    def test_a_path_that_is_listed_twice_is_not_its_own_twin_and_is_named_once(self):
        self.assertEqual(cpp.check([b"docs/a.md", b"docs/a.md", b"docs/b.md"]), [])      # (a conflicted index lists a path once for each stage)
        self.assertEqual(len(cpp.check([b"docs/aux.md", b"docs/aux.md", b"docs/b.md"])), 1)


def run_tool(*args, cwd=None):
    return subprocess.run([sys.executable, TOOL] + list(args), capture_output=True, text=True, env=GIT_ENV, cwd=cwd, timeout=120)


@unittest.skipUnless(shutil.which("git"), "git is needed")
class TheTool(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = tmp.name
        self.root = os.path.join(tmp.name, "repo")
        os.makedirs(self.root)
        self.git("init", "-q", "-b", "main")
        self.git("config", "core.ignorecase", "false")                                  # (a file system that folds case would make git fold the case of the names it is given)
        self.blob = self.git("hash-object", "-w", "--stdin", input=b"x\n").decode().strip().encode()

    def git(self, *args, input=None):
        done = subprocess.run(["git", *args], cwd=self.root, env=GIT_ENV, input=input, capture_output=True)
        self.assertEqual(done.returncode, 0, (args, done.stdout, done.stderr))
        return done.stdout

    def track(self, paths):
        """The tracked paths of the scratch repository are exactly these (texts or bytes): the index is made with git's plumbing, there is no working tree."""
        index = os.path.join(self.root, ".git", "index")
        if os.path.exists(index):
            os.remove(index)
        raw = [p if isinstance(p, bytes) else p.encode("utf-8") for p in paths]
        self.git("update-index", "--add", "-z", "--index-info", input=b"".join(b"100644 " + self.blob + b"\t" + p + b"\0" for p in raw))
        self.assertEqual(sorted(self.git("ls-files", "-z").split(b"\0")[:-1]), sorted(set(raw)))

    @unittest.skipUnless(os.path.exists(os.path.join(REPO, ".git")), "this is not a checkout")
    def test_this_repository_has_only_portable_paths(self):
        tracked = subprocess.run(["git", "-C", REPO, "ls-files", "-z"], capture_output=True, env=GIT_ENV).stdout.split(b"\0")[:-1]
        self.assertGreater(len(tracked), 0)
        for args, cwd in (((), REPO), (("--root", REPO), HERE)):
            done = run_tool(*args, cwd=cwd)
            self.assertEqual((done.returncode, done.stderr), (0, ""), done.stdout)
            self.assertEqual(done.stdout, "portable paths: all %d tracked paths can be held by Windows and macOS\n" % len(set(tracked)))

    def test_a_scratch_repository_of_good_paths_passes(self):
        self.track(GOOD)
        done = run_tool("--root", self.root)
        self.assertEqual((done.returncode, done.stderr), (0, ""), done.stdout)
        self.assertIn("all %d tracked paths" % len(GOOD), done.stdout)

    def test_a_scratch_repository_with_every_bad_kind_fails_and_names_every_path(self):
        everything = [path for path, _ in BAD] + [path for group in TWINS for path in group] + [b"docs/\xff.md", b"docs/\xed\xa0\x80.md"]
        self.track(everything)
        expected = cpp.check([p if isinstance(p, bytes) else p.encode("utf-8") for p in everything])
        self.assertLessEqual(len(expected), cpp.MAX_LINES, "the tables have grown past what one run prints: cut them in two")
        done = run_tool("--root", self.root)
        self.assertEqual((done.returncode, done.stderr), (1, ""), done.stdout)
        shown = [p.decode("utf-8", "surrogateescape") if isinstance(p, bytes) else p for p in everything]
        for path in shown:
            self.assertIn(cpp.quote(path), done.stdout, path)
        lines = done.stdout.splitlines()
        self.assertIn("%d of %d tracked paths cannot be held" % (len(set(shown)), len(set(shown))), lines[0])
        self.assertEqual(len(lines), 1 + len(expected))

    def test_a_long_list_is_cut_and_counted(self):
        self.track(["docs/b%03d?.md" % i for i in range(150)] + ["docs/fine.md"])
        done = run_tool("--root", self.root)
        lines = done.stdout.splitlines()
        self.assertEqual((done.returncode, done.stderr), (1, ""), done.stdout)
        self.assertIn("150 of 151 tracked paths cannot be held", lines[0])
        self.assertEqual(len(lines), 1 + cpp.MAX_LINES + 1)
        self.assertEqual(lines[-1], "  ... and %d more" % (150 - cpp.MAX_LINES))

    def test_each_bad_kind_alone_makes_the_exit_status_1_and_the_path_is_named(self):
        kinds = {"invalid UTF-8": [b"docs/\xff.md"], "a forbidden character": ["docs/q?.md"], "a control character": ["docs/new\nline.md"], "a trailing dot": ["docs/dot."],
                 "a trailing space": ["docs/space "], "a device name": ["docs/aux.md"], "a long path": ["a/" * 100 + "b"], "a long name": ["x/" + "\u00e9" * 128],
                 "a case twin of a file": ["docs/a.md", "docs/A.md"], "a case twin of a folder": ["Web/a.js", "web/b.js"], "a Unicode form twin": ["docs/caf\u00e9.md", "docs/cafe\u0301.md"]}
        for kind, paths in kinds.items():
            with self.subTest(kind):
                self.track(paths + ["docs/fine.md"])
                done = run_tool("--root", self.root)
                self.assertEqual((done.returncode, done.stderr), (1, ""), done.stdout)
                for path in paths:
                    self.assertIn(cpp.quote(path.decode("utf-8", "surrogateescape") if isinstance(path, bytes) else path), done.stdout)
                self.assertNotIn("docs/fine.md", done.stdout)

    def test_the_log_cannot_be_made_to_carry_a_workflow_command_or_a_hidden_character(self):
        paths = ["::error::pwned?", "docs/new\n::error::a line of its own?.md", "docs/\u202eevil?.md", "docs/esc\x1b[31m.md", "::set-output name=x::y|"]
        self.track(paths)
        done = run_tool("--root", self.root)
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertTrue(done.stdout.isascii(), done.stdout)
        lines = done.stdout.splitlines()
        for line in lines:
            self.assertFalse(line.startswith("::"), line)
            self.assertNotIn("\x1b", line)
        self.assertEqual(len(lines), 1 + len(cpp.check([p.encode("utf-8") for p in paths])), "a path with a newline in it must stay on its line")
        for path in paths:
            self.assertIn(cpp.quote(path), done.stdout)

    def test_a_folder_that_is_no_repository_and_an_option_that_is_none_are_an_error_not_a_pass(self):
        nothing = os.path.join(self.tmp, "nothing")
        os.makedirs(nothing)
        done = subprocess.run([sys.executable, TOOL, "--root", nothing], capture_output=True, text=True, env=dict(GIT_ENV, GIT_CEILING_DIRECTORIES=self.tmp), timeout=120)
        self.assertEqual(done.returncode, 2, done.stdout)
        self.assertIn("git ls-files failed", done.stderr)
        self.assertEqual(done.stdout, "")
        done = run_tool("--root", os.path.join(self.tmp, "missing"))
        self.assertEqual(done.returncode, 2, done.stdout)
        done = run_tool("--no-such-option")
        self.assertEqual(done.returncode, 2, done.stdout)
        self.assertEqual(run_tool("--help").returncode, 0)

    def test_it_reads_what_git_tracks_and_not_what_lies_in_the_folder(self):
        self.track(["docs/fine.md"])
        with open(os.path.join(self.root, "aux.md"), "w", encoding="utf-8") as f:                 # (a file that is not tracked is not the checkout's business)
            f.write("x\n")
        done = run_tool("--root", self.root)
        self.assertEqual(done.returncode, 0, done.stdout)


if __name__ == "__main__":
    unittest.main()
