#!/usr/bin/env python3
"""tools/server_paths.py and tools/server_paths.txt: the paths of the game server, the site's services and their tests (run by ./run_tests.sh --fast and by the CI, with the other python
tests of tests/scripts).

What these tests hold:
  - the patterns, on tables: a directory, a glob (one path component at a time, so that * never crosses a /), an exact path, comments and blank lines, and the lines that are refused
  - the list of this tree: it names no path of the game's libraries or clients, it names every file of the server's directories, every pattern of it matches a file (--check), and the game
    files and the server files are the tracked files, each once
  - the tree without the listed paths (--export), from this repository's HEAD: it holds no listed path, holds every other file with its bytes and its executable bit, has CMakeLists.txt
    and the commit's SHA in GAME_ONLY_FROM; and, on a small repository made here, the folder it is given must be new or empty
  - the check can fail: a pattern that matches no file, planted in a list, is a finding that names it (exit 1); the two names that may match no file are not
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOL = os.path.join(REPO, "tools", "server_paths.py")
sys.path.insert(0, os.path.join(REPO, "tools"))
import server_paths as sp  # noqa: E402

GIT_ENV = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.org", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@example.org")
IS_CHECKOUT = os.path.exists(os.path.join(REPO, ".git"))

# The game: its libraries, the native and the web clients, the front page and Home, the docs of the protocol, the maps. None of these is listed.
GAME_LIBRARIES = ("sim", "ai", "app", "assets", "replay", "net")
GAME_ROOTS = (["src/ants_%s/" % n for n in GAME_LIBRARIES] + ["include/ants_%s/" % n for n in GAME_LIBRARIES]
              + ["web/front/", "web/home/", "web/lobby.html", "web/shell.html", "docs/NETWORK_PORT.md", "Community-Maps/", "Original-Ants/"])
# The directories whose every file is the server's
SERVER_DIRECTORIES = ("src/ants_server/", "include/ants_server/", "src/ants_ctl/", "include/ants_ctl/", "src/ants_db/", "include/ants_db/", "third_party/sqlite/", "hub/",
                      "tests/test_server/", "tests/test_db/", "tests/test_ctl/")


def run_tool(*args, root=None, env=None):
    command = [sys.executable, TOOL] + (["--root", root] if root else []) + list(args)
    return subprocess.run(command, capture_output=True, text=True, env=env, timeout=300)


class ThePatterns(unittest.TestCase):
    def test_a_directory_names_everything_under_it_and_nothing_beside_it(self):
        patterns = ["src/ants_server/", "hub/"]
        for rel in ("src/ants_server/room.cpp", "src/ants_server/deep/er/file.txt", "hub/store/sqlite.py", "hub/__init__.py"):
            self.assertTrue(sp.is_server_path(rel, patterns), rel)
        for rel in ("src/ants_server_x/room.cpp", "src/ants_serverx", "src/ants_net/lobby.cpp", "xsrc/ants_server/room.cpp", "hub", "hubs/a.py", "a/hub/b.py"):
            self.assertFalse(sp.is_server_path(rel, patterns), rel)

    def test_a_glob_matches_inside_one_component_and_never_across_a_slash(self):
        patterns = ["tests/scripts/test_hub_*", "tests/data/hub*_mutants.json", "tests/scripts/web_history*_check.js", "tests/data/trusted*"]
        for rel in ("tests/scripts/test_hub_core.py", "tests/scripts/test_hub_", "tests/data/hub_mutants.json", "tests/data/hub_login_mutants.json", "tests/scripts/web_history_check.js",
                    "tests/scripts/web_history_pictures_check.js", "tests/data/trusted_proxies_mutants.json", "tests/data/trusted"):
            self.assertTrue(sp.is_server_path(rel, patterns), rel)
        for rel in ("tests/scripts/test_hub_core/x.py", "tests/scripts/sub/test_hub_core.py", "tests/scripts/test_web_hub_x.py", "tests/data/hub/x_mutants.json", "tests/data/hub_mutants.jsonl",
                    "tests/data/hub.json", "tests/scripts/web_history_check.py", "tests/data/trusted/x.json", "tests/data/untrusted_x"):
            self.assertFalse(sp.is_server_path(rel, patterns), rel)

    def test_the_other_signs_of_fnmatch_work_in_one_component_too(self):
        patterns = ["a/b?.txt", "a/[xy]_c.txt"]
        self.assertTrue(sp.is_server_path("a/b1.txt", patterns))
        self.assertTrue(sp.is_server_path("a/y_c.txt", patterns))
        for rel in ("a/b.txt", "a/b12.txt", "a/b/.txt", "a/z_c.txt", "a/xy_c.txt"):
            self.assertFalse(sp.is_server_path(rel, patterns), rel)

    def test_anything_else_is_one_exact_path(self):
        patterns = ["Dockerfile", "docker/nginx.conf"]
        self.assertTrue(sp.is_server_path("Dockerfile", patterns))
        self.assertTrue(sp.is_server_path("docker/nginx.conf", patterns))
        for rel in ("Dockerfile.hub", "dockerfile", "sub/Dockerfile", "docker/nginx.conf.bak", "docker/nginx_conf", "Dockerfile/x", "docker/resolve_build_id.sh"):
            self.assertFalse(sp.is_server_path(rel, patterns), rel)

    def test_comments_and_blank_lines_are_not_patterns(self):
        text = "# a comment line\n\n   \n  src/ants_server/   \nDockerfile  # the end of a line is a comment too\n#hub/\n\t# an indented comment\ndocs/SERVER.md\n"
        self.assertEqual(sp.parse(text), ["src/ants_server/", "Dockerfile", "docs/SERVER.md"])
        self.assertEqual(sp.parse(""), [])
        self.assertFalse(sp.is_server_path("hub/a.py", sp.parse("#hub/\n")))

    def test_a_line_that_is_no_pattern_of_this_list_is_refused_with_its_number(self):
        for bad in ("/src/ants_server/", "!hub/", "tests/**/x.py", "tests/../hub/", "tests\\scripts\\x.py", "tests//x.py", "tests/*/", "a/../b.txt"):
            with self.subTest(bad):
                with self.assertRaises(ValueError) as caught:
                    sp.parse("hub/\n%s\n" % bad)
                self.assertIn(":2:", str(caught.exception))

    def test_the_real_list_is_read_and_holds_no_comment_or_blank(self):
        patterns = sp.load(REPO)
        self.assertGreater(len(patterns), 50)
        self.assertTrue(all(p == p.strip() and p and "#" not in p for p in patterns))
        self.assertIn("src/ants_server/", patterns)


@unittest.skipUnless(shutil.which("git") and IS_CHECKOUT, "git and a checkout are needed")
class TheListOfThisTree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patterns = sp.load(REPO)
        cls.tracked = sp.tracked(REPO)

    def test_no_path_of_the_game_is_listed(self):
        for root in GAME_ROOTS:
            files = [f for f in self.tracked if f.startswith(root)]
            self.assertTrue(files, "%s: no tracked file: where is it?" % root)
            for f in files:
                self.assertFalse(sp.is_server_path(f, self.patterns), "%s is the game's and the list names it" % f)
            if root.endswith("/"):             # (a file that is not there yet: no pattern is wide enough to take it)
                self.assertFalse(sp.is_server_path(root + "a_new_file.txt", self.patterns), "a pattern names the folder %s as a whole or a glob that reaches into it" % root)

    def test_every_file_of_the_servers_directories_is_listed(self):
        for root in SERVER_DIRECTORIES:
            files = [f for f in self.tracked if f.startswith(root)]
            self.assertTrue(files, "%s: no tracked file: where is it?" % root)
            for f in files:
                self.assertTrue(sp.is_server_path(f, self.patterns), "%s is in a folder of the server and the list does not name it" % f)
        self.assertGreaterEqual(len([f for f in self.tracked if f.startswith("src/ants_server/")]), 30)

    def test_every_pattern_matches_a_tracked_file_and_the_check_passes(self):
        self.assertEqual(sp.dead_patterns(REPO), [])
        done = run_tool("--check", root=REPO, env=os.environ)
        self.assertEqual((done.returncode, done.stderr), (0, ""), done.stdout)
        self.assertIn("every pattern matches", done.stdout)

    def test_the_game_files_and_the_server_files_are_the_tracked_files_each_once(self):
        game, server = sp.split(REPO)
        self.assertEqual(sorted(game + server), self.tracked)
        self.assertEqual(set(game) & set(server), set())
        self.assertGreater(len(server), 200)
        self.assertGreater(len(game), 4000)
        listed = {name: run_tool(name, root=REPO, env=os.environ) for name in ("--list-server", "--list-game")}
        self.assertEqual(listed["--list-server"].stdout.splitlines(), server)
        self.assertEqual(listed["--list-game"].stdout.splitlines(), game)

    def test_the_files_that_both_sides_use_are_the_games(self):
        for rel in ("CMakeLists.txt", "docker/resolve_build_id.sh", "build_web.sh", "run_tests.sh", "tools/check_hygiene.py", "tests/scripts/run_python_tests.py"):
            self.assertIn(rel, self.tracked)
            self.assertFalse(sp.is_server_path(rel, self.patterns), rel)


@unittest.skipUnless(shutil.which("git") and IS_CHECKOUT, "git and a checkout are needed")
class TheTreeWithoutThem(unittest.TestCase):
    """--export of this repository's HEAD, made once."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="server_paths_export.")
        cls.dest = os.path.join(cls.tmp, "game")
        cls.done = run_tool("--export", cls.dest, root=REPO, env=os.environ)
        cls.patterns = sp.load(REPO)
        listing = subprocess.run(["git", "-C", REPO, "ls-tree", "-r", "-z", "HEAD"], capture_output=True, check=True).stdout
        cls.head = {}        # path -> (mode, blob)
        for entry in listing.split(b"\0"):
            if entry:
                meta, path = entry.split(b"\t", 1)
                mode, _, blob = meta.decode().split(" ")
                cls.head[os.fsdecode(path)] = (mode, blob)
        cls.exported = sorted(os.path.relpath(os.path.join(base, n), cls.dest).replace(os.sep, "/") for base, _, names in os.walk(cls.dest) for n in names)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_the_export_worked(self):
        self.assertEqual((self.done.returncode, self.done.stderr), (0, ""), self.done.stdout)
        self.assertIn("files written to", self.done.stdout)

    def test_it_holds_no_listed_path_and_its_folders_are_not_there_either(self):
        self.assertGreater(len(self.exported), 4000)
        for rel in self.exported:
            self.assertFalse(sp.is_server_path(rel, self.patterns), rel)
        for folder in SERVER_DIRECTORIES:
            self.assertFalse(os.path.exists(os.path.join(self.dest, *folder.split("/"))), folder)

    def test_it_holds_what_the_list_does_not_name_and_the_stamp(self):
        wanted = sorted([rel for rel in self.head if not sp.is_server_path(rel, self.patterns)] + [sp.STAMP])        # (the files of HEAD: what is only in the index or the working tree is not exported)
        self.assertEqual(self.exported, wanted)
        self.assertIn("CMakeLists.txt", self.exported)
        self.assertIn("src/ants_sim/sim_engine.cpp", self.exported)

    def test_the_stamp_is_the_sha_of_head_and_there_is_no_git_folder(self):
        sha = subprocess.run(["git", "-C", REPO, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
        with open(os.path.join(self.dest, sp.STAMP), encoding="utf-8") as f:
            self.assertEqual(f.read(), sha + "\n")
        self.assertEqual(len(sha), 40)
        self.assertFalse(os.path.exists(os.path.join(self.dest, ".git")))

    def test_the_bytes_and_the_executable_bit_are_the_commits(self):
        for rel in ("CMakeLists.txt", "Original-Ants/ants.chd", "run_tests.sh", "tools/check_hygiene.py", "tests/scripts/run_python_tests.py"):
            mode, blob = self.head[rel]
            want = subprocess.run(["git", "-C", REPO, "cat-file", "blob", blob], capture_output=True, check=True).stdout
            with open(os.path.join(self.dest, rel), "rb") as f:
                self.assertEqual(f.read(), want, rel)
            if os.name != "nt":
                self.assertEqual(os.access(os.path.join(self.dest, rel), os.X_OK), mode == "100755", rel)


@unittest.skipUnless(shutil.which("git"), "git is needed")
class TheToolOnASmallRepository(unittest.TestCase):
    """A repository made here: a list with a directory, a glob and an exact path, and files on both sides."""

    FILES = {
        "CMakeLists.txt": "game\n", "src/ants_sim/a.cpp": "a\n", "src/ants_server/room.cpp": "r\n", "src/ants_server/deep/x.cpp": "x\n", "hub/chat.py": "c\n",
        "tests/scripts/test_hub_core.py": "t\n", "tests/scripts/test_web_game.py": "g\n", "tests/data/hub_mutants.json": "{}\n", "Dockerfile": "d\n", "Dockerfile.x": "d\n",
        "tools/run.sh": "#!/bin/sh\n", "docs/SERVER.md": "s\n", "docs/GAMEPLAY.md": "p\n",
    }
    LIST = "# a list\nsrc/ants_server/\nhub/\ntests/scripts/test_hub_*\ntests/data/hub*_mutants.json\nDockerfile\ndocs/SERVER.md  # one file\n"

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = tmp.name
        self.root = os.path.join(self.tmp, "repo")
        os.makedirs(self.root)
        self.git("init", "-q", "-b", "main")
        self.write("tools/server_paths.txt", self.LIST)
        for rel, text in self.FILES.items():
            self.write(rel, text)
        os.chmod(os.path.join(self.root, "tools", "run.sh"), 0o755)

    def git(self, *args):
        done = subprocess.run(["git", *args], cwd=self.root, env=GIT_ENV, capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, (args, done.stdout, done.stderr))
        return done.stdout

    def write(self, rel, text):
        path = os.path.join(self.root, *rel.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)

    def commit(self):
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "files")
        return self.git("rev-parse", "HEAD").strip()

    def tool(self, *args, root=None):
        return run_tool(*args, root=self.root if root is None else root, env=GIT_ENV)

    def test_the_two_lists(self):
        self.git("add", "-A")
        server = self.tool("--list-server")
        game = self.tool("--list-game")
        self.assertEqual((server.returncode, game.returncode), (0, 0))
        self.assertEqual(server.stdout.splitlines(), sorted(["Dockerfile", "docs/SERVER.md", "hub/chat.py", "src/ants_server/deep/x.cpp", "src/ants_server/room.cpp", "tests/data/hub_mutants.json",
                                                             "tests/scripts/test_hub_core.py"]))
        self.assertEqual(game.stdout.splitlines(), sorted(["CMakeLists.txt", "Dockerfile.x", "docs/GAMEPLAY.md", "src/ants_sim/a.cpp", "tests/scripts/test_web_game.py", "tools/run.sh",
                                                           "tools/server_paths.txt"]))

    def test_a_list_whose_patterns_all_match_passes(self):
        self.git("add", "-A")
        done = self.tool("--check")
        self.assertEqual((done.returncode, done.stderr), (0, ""), done.stdout)
        self.assertIn("every pattern matches (7 paths listed, 7 not)", done.stdout)

    def test_a_planted_pattern_that_matches_nothing_fails_the_check_and_is_named(self):
        for planted in ("src/ants_gone/", "tests/scripts/test_nothing_*", "docs/MISSING.md", "Dockerfile.hub"):
            with self.subTest(planted):
                self.write("tools/server_paths.txt", self.LIST + planted + "\n")
                self.git("add", "-A")
                done = self.tool("--check")
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(planted, done.stdout)
                self.assertEqual(len(done.stdout.strip().splitlines()), 1, done.stdout)      # (only the planted one)
        self.write("tools/server_paths.txt", self.LIST)
        self.git("add", "-A")
        self.assertEqual(self.tool("--check").returncode, 0)

    def test_the_two_names_that_may_match_no_file_do_not_fail_the_check(self):
        self.write("tools/server_paths.txt", self.LIST + "".join(name + "\n" for name in sp.UNMATCHED_ALLOWED))
        self.git("add", "-A")
        done = self.tool("--check")
        self.assertEqual((done.returncode, done.stderr), (0, ""), done.stdout)
        self.assertEqual(sp.UNMATCHED_ALLOWED, ("tests/test_app_server/", "docs/SERVER_DESIGN.md"))

    def test_export_writes_the_other_files_with_their_bytes_and_the_stamp(self):
        sha = self.commit()
        dest = os.path.join(self.tmp, "out")
        done = self.tool("--export", dest)
        self.assertEqual((done.returncode, done.stderr), (0, ""), done.stdout)
        found = sorted(os.path.relpath(os.path.join(b, n), dest).replace(os.sep, "/") for b, _, names in os.walk(dest) for n in names)
        self.assertEqual(found, sorted(["CMakeLists.txt", "Dockerfile.x", "docs/GAMEPLAY.md", "src/ants_sim/a.cpp", "tests/scripts/test_web_game.py", "tools/run.sh",
                                        "tools/server_paths.txt", sp.STAMP]))
        for folder in ("src/ants_server", "hub", "tests/data", "docs/SERVER.md"):
            self.assertFalse(os.path.exists(os.path.join(dest, *folder.split("/"))), folder)
        with open(os.path.join(dest, "src", "ants_sim", "a.cpp"), encoding="utf-8") as f:
            self.assertEqual(f.read(), "a\n")
        with open(os.path.join(dest, sp.STAMP), encoding="utf-8") as f:
            self.assertEqual(f.read(), sha + "\n")
        if os.name != "nt":
            self.assertTrue(os.access(os.path.join(dest, "tools", "run.sh"), os.X_OK))
            self.assertFalse(os.access(os.path.join(dest, "CMakeLists.txt"), os.X_OK))

    def test_export_takes_the_commit_and_not_the_working_tree(self):
        self.commit()
        self.write("src/ants_sim/a.cpp", "changed\n")
        self.write("src/ants_sim/new.cpp", "new\n")
        dest = os.path.join(self.tmp, "out")
        self.assertEqual(self.tool("--export", dest).returncode, 0)
        with open(os.path.join(dest, "src", "ants_sim", "a.cpp"), encoding="utf-8") as f:
            self.assertEqual(f.read(), "a\n")
        self.assertFalse(os.path.exists(os.path.join(dest, "src", "ants_sim", "new.cpp")))

    def test_export_goes_into_a_new_folder_or_an_empty_one_and_refuses_the_others(self):
        self.commit()
        new = os.path.join(self.tmp, "new", "deeper")
        self.assertEqual(self.tool("--export", new).returncode, 0)
        self.assertTrue(os.path.isfile(os.path.join(new, sp.STAMP)))
        empty = os.path.join(self.tmp, "empty")
        os.makedirs(empty)
        self.assertEqual(self.tool("--export", empty).returncode, 0)
        self.assertTrue(os.path.isfile(os.path.join(empty, sp.STAMP)))
        for refused in (new, os.path.join(new, sp.STAMP)):                       # (a folder that holds files; a file)
            before = sorted(os.listdir(new))
            done = self.tool("--export", refused)
            self.assertEqual(done.returncode, 2, done.stdout)
            self.assertIn("not an empty folder", done.stderr)
            self.assertEqual(sorted(os.listdir(new)), before)
        stray = os.path.join(self.tmp, "stray")
        os.makedirs(stray)
        with open(os.path.join(stray, "keep.txt"), "w", encoding="utf-8") as f:
            f.write("mine\n")
        self.assertEqual(self.tool("--export", stray).returncode, 2)
        self.assertEqual(os.listdir(stray), ["keep.txt"])

    @unittest.skipUnless(hasattr(os, "symlink") and os.name != "nt", "a link is made only where the system has them")
    def test_a_link_that_the_list_does_not_name_is_refused_and_one_that_it_names_is_left_out(self):
        os.symlink("a.cpp", os.path.join(self.root, "src", "ants_sim", "link.cpp"))
        os.symlink("room.cpp", os.path.join(self.root, "src", "ants_server", "link.cpp"))
        self.commit()
        refused = os.path.join(self.tmp, "refused")
        done = self.tool("--export", refused)
        self.assertEqual(done.returncode, 2, done.stdout)
        self.assertIn("src/ants_sim/link.cpp", done.stderr)
        self.assertFalse(os.path.exists(os.path.join(refused, sp.STAMP)))                # (no stamp: the folder is not a finished export)
        self.git("rm", "-q", "src/ants_sim/link.cpp")
        self.git("commit", "-q", "-m", "no link of the game")
        left_out = os.path.join(self.tmp, "left_out")
        done = self.tool("--export", left_out)
        self.assertEqual((done.returncode, done.stderr), (0, ""), done.stdout)
        self.assertFalse(os.path.lexists(os.path.join(left_out, "src", "ants_server", "link.cpp")))

    def test_a_repository_with_no_commit_cannot_be_exported(self):
        self.git("add", "-A")
        dest = os.path.join(self.tmp, "out")
        done = self.tool("--export", dest)
        self.assertEqual(done.returncode, 2, done.stdout)
        self.assertIn("HEAD", done.stderr)
        self.assertFalse(os.path.exists(dest))

    def test_no_list_no_repository_and_no_mode_are_errors_and_never_a_pass(self):
        self.git("add", "-A")
        os.remove(os.path.join(self.root, "tools", "server_paths.txt"))
        done = self.tool("--check")
        self.assertEqual(done.returncode, 2, done.stdout)
        self.assertIn("server_paths.txt", done.stderr)
        nowhere = os.path.join(self.tmp, "no_repository")
        os.makedirs(os.path.join(nowhere, "tools"))
        with open(os.path.join(nowhere, "tools", "server_paths.txt"), "w", encoding="utf-8") as f:
            f.write("hub/\n")
        done = run_tool("--check", root=nowhere, env=dict(GIT_ENV, GIT_CEILING_DIRECTORIES=self.tmp))
        self.assertEqual(done.returncode, 2, done.stdout)
        self.assertIn("git ls-files failed", done.stderr)
        for args in ((), ("--check", "--list-game"), ("--no-such-option",)):
            self.assertEqual(self.tool(*args).returncode, 2, args)


if __name__ == "__main__":
    unittest.main()
