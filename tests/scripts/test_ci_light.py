#!/usr/bin/env python3
"""tools/ci_light.sh, which decides in the first job of the CI whether a pull request needs all of CI or only the light checks (run by ./run_tests.sh --fast and by the CI).

It is run on real git repositories made here (a base commit, a pull request's commits, a base that moved on), made with git's plumbing and no working tree, so that a file system that folds
the case of names (macOS, Windows) cannot make `Web/a.js` and `web/a.js` one file: the decision, the file list it prints, the line it appends to GITHUB_OUTPUT.
The rules are the ones that docs/WORKFLOW.md states; what these tests hold:
  - every changed file must be a page, a document, a list of mutants or a page test for `light`; one other file makes it `full`, whatever else changes with it, and an unknown path is full;
  - of those kinds, the files that the jobs a light pull request skips read or run are full (the C++ tests of ctest, the E2E runner and the macOS script suites read a few pages: they are
    listed here with who reads them, and a scan of the sources finds a read that is not listed);
  - what is compared is the pull request's own change (against the MERGE BASE: a base that moved on, with a change of its own, does not make the pull request full), a renamed file counts under
    both names (a source file renamed into docs/ is full), a deleted file counts;
  - the change is also compared with the branch main (the remote branch origin/main), whatever the base is: a pull request that is retargeted starts no new run, so a pull request against another
    branch that carries code which main lacks must not come out light; no main to compare with is full;
  - anything that is not a pull request with two full commit ids and at least one changed file is full, and the script never fails (a failing git is full);
  - the log cannot be made to carry a workflow command (a path is printed quoted, after a word), and the commit ids are only taken from the environment.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SCRIPT = os.path.join(REPO, "tools", "ci_light.sh")
GIT_ENV = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.org", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@example.org")

LIGHT = ["web/index.js", "web/front/x.css", "web/a/b/c/d.html", "docs/WORKFLOW.md", "docs/legacy/x.c", "docs/img/p.png", "CHANGELOG.md", "README.md", "tests/data/home_chat_mutants.json",
         "tests/data/x_mutants.json", "tests/scripts/test_web_home_chat.py", "tests/scripts/test_web_aspect.sh", "tests/scripts/web_home_chat_check.js", "tests/scripts/web_routes_check.py",
         "tests/scripts/web_home_screen_check.js", "tests/scripts/test_web_lobby_net.py", "web/home/chat.js", "web/home/home.css", "web/home/games.css", "web/front/lobby_chat.css",
         "web/front/mapmaker_app.js", "web/front/bot_names.js", "web/front/preview_notes.txt", "web/matches.html", "web/history_page.js"]
# Of the light kinds and nevertheless full, with who reads them (the jobs that a light pull request skips: ctest on all three systems, and the script suites of the macOS job). The scan in
# TheSkippedJobsReadNoLightFile finds the reads in the sources; this table is what it compares them with, and each file here must be full in tools/ci_light.sh.
RULES_JS, LOBBY_NET_JS = "web/front/lobby_rules.js", "web/front/lobby_net.js"
NATIVE_READS = {
    RULES_JS: "ctest: test_ai_names, test_start_menu_hosting; test_ants_server.sh (a grep, and the page harness)",
    "web/front/preview_tiny.png": "ctest: test_map_list (the six original maps: tiny, small, medium, gauntlet, treasure, islands)",
    "web/front/preview_islands.png": "ctest: test_map_list",
    "web/lobby.html": "test_ants_server.sh (a grep, web_fill_check.js, the page harness)",
    "web/shell.html": "test_ants_server.sh (a grep, web_fill_check.js)",
    "web/shapes.js": "test_ants_server.sh: web_lobby_server_check.js",
    LOBBY_NET_JS: "test_ants_server.sh: web_lobby_server_check.js, web_lobby_handoff_check.js, the page harness",
    "web/front/lobby_chat.js": "test_ants_server.sh: the page harness",
    "web/front/rematch.js": "test_ants_server.sh: the page harness",
    "web/front/game_modes.js": "test_ants_server.sh: the page harness",
    "web/front/game_type.js": "test_ants_server.sh: the page harness",
    "web/front/map_picker.js": "test_ants_server.sh: the page harness (the checks that ask for the picker)",
    "web/home/home.js": "test_ants_server.sh: the page harness",
    "web/home/games.js": "test_ants_server.sh: the page harness (the Games list)",
    "tests/scripts/web_fill_check.js": "test_ants_server.sh",
    "tests/scripts/web_lobby_server_check.js": "test_ants_server.sh",
    "tests/scripts/web_lobby_handoff_check.js": "test_ants_server.sh",
    "tests/scripts/web_page_harness.js": "test_ants_server.sh: web_lobby_server_check.js",
}
FULL = ["VERSION", "CMakeLists.txt", "cmake/x.cmake", "src/ants_sim/a.cpp", "include/ants_sim/a.hpp", "tools/ci_light.sh", "tools/mutate.py", "hub/server.py", "Dockerfile", "Dockerfile.server",
        "Dockerfile.hub", "docker/nginx.conf", "docker-compose.stack.yml", ".github/workflows/ci.yml", ".github/FUNDING.yml", "third_party/sqlite/x.c", "Community-Maps/Map.lvl",
        "Original-Ants/Maps/TINY.LVL", "run_tests.sh", "run_tests.bat", "build_web.sh", "start_game.sh", "AGENTS.md", "LICENSE", "THIRD_PARTY_NOTICES.md", ".gitignore", ".dockerignore", "a_new_file",
        "tests/test_sim/test_x.cpp", "tests/common/x.hpp", "tests/e2e/x.cpp", "tests/CMakeLists.txt",
        "tests/scripts/test_hub_login.py", "tests/scripts/test_ants_server.sh", "tests/scripts/test_start_game.sh", "tests/scripts/e2e_deadlines.sh", "tests/scripts/quick_http.py",
        "tests/scripts/run_python_tests.py", "tests/scripts/test_ci_workflow.py", "tests/scripts/fake_hub.js", "tests/scripts/lvl_core_check.js", "tests/scripts/sub/web_x.js",
        "tests/scripts/sub/test_web_x.py", "tests/scripts/README", "tests/scripts/test_web_x.txt.orig/y", "tests/data/pbkdf2_vectors.json", "tests/data/game_menu_match.json", "tests/data/x.csv",
        "tests/data/lvl_core/x_mutants.json", "tests/data/sub/x_mutants.json", "tests/data/mutants.json", "tests/data/x_mutants.json.bak", "webx/a.js", "docsx/a.md", "README.md.old",
        "CHANGELOG.md/x", "x/web/a.js", "x/docs/a.md", "Web/a.js", "DOCS/a.md", "asset_catalog/x.png", "tools/web/x.js", "tests/scripts/mini_websocket.js"] + sorted(NATIVE_READS)


class Gitlink:
    """The entry of a submodule: the commit that the superproject records (it need not exist here)."""

    def __init__(self, commit):
        self.commit = commit


def run(cmd, cwd, **kwargs):
    return subprocess.run(cmd, cwd=cwd, env=kwargs.pop("env", GIT_ENV), capture_output=True, text=True, **kwargs)


@unittest.skipUnless(shutil.which("bash") and shutil.which("git"), "bash and git are needed")
class Decision(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = os.path.join(tmp.name, "repo")
        os.makedirs(self.root)
        self.out_file = os.path.join(tmp.name, "github_output")
        self.index_file = os.path.join(tmp.name, "index")
        self.git("init", "-q", "-b", "main")
        self.git("config", "core.ignorecase", "false")
        self.base = self.commit(None, {"README.md": "x\n", "src/a.cpp": "int a;\n", "docs/a.md": "a\n", "web/a.js": "a\n", "tools/t.py": "t\n", "old name.cpp": "o\n"}, "base")
        self.git("update-ref", "refs/remotes/origin/main", self.base)                           # (what the checkout of the CI fetches: main is the base commit until a test moves it)

    def git(self, *args, **kwargs):
        env = dict(GIT_ENV, GIT_INDEX_FILE=self.index_file)
        done = subprocess.run(["git", *args], cwd=self.root, env=env, capture_output=True, text=True, **kwargs)
        self.assertEqual(done.returncode, 0, (args, done.stdout, done.stderr))
        return done.stdout.strip()

    def commit(self, parent, changes, message="change"):
        """A commit made without a working tree: the files of the tree of `parent` (None: an empty root) with `changes` applied (a text writes or replaces a file, None deletes it, a Gitlink sets a
        submodule). Returns its id."""
        if os.path.exists(self.index_file):
            os.remove(self.index_file)
        if parent:
            self.git("read-tree", parent)
        lines = []
        for path, text in changes.items():
            if text is None:
                lines.append("0 %s\t%s\0" % ("0" * 40, path))
            elif isinstance(text, Gitlink):
                lines.append("160000 %s\t%s\0" % (text.commit, path))
            else:
                lines.append("100644 %s\t%s\0" % (self.git("hash-object", "-w", "--stdin", input=text), path))
        self.git("update-index", "--add", "-z", "--index-info", input="".join(lines))
        tree = self.git("write-tree")
        return self.git("commit-tree", tree, *(["-p", parent] if parent else []), "-m", message)

    def pull_request(self, changes, base=None):
        """the head of a pull request: one commit of `changes` on the base commit"""
        return self.commit(base or self.base, changes)

    def decide(self, head, base=None, **env):
        """(light?, the output, the lines of GITHUB_OUTPUT) of the script run for a pull request from `base` (default: the first commit) to `head`"""
        environment = dict(GIT_ENV, EVENT="pull_request", BASE_SHA=base or self.base, HEAD_SHA=head, GITHUB_OUTPUT=self.out_file)
        environment.update(env)
        for name in [n for n, v in environment.items() if v is None]:
            del environment[name]
        if os.path.exists(self.out_file):
            os.remove(self.out_file)
        done = subprocess.run(["bash", SCRIPT], cwd=self.root, env=environment, capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, "the script never fails: " + done.stdout + done.stderr)
        lines = []
        if os.path.exists(self.out_file):
            with open(self.out_file, encoding="utf-8") as f:
                lines = f.read().splitlines()
        self.assertEqual(len(lines), 1, "exactly one line for the output: %r" % lines)
        self.assertIn(lines[0], ("light=true", "light=false"))
        return lines[0] == "light=true", done.stdout, lines

    def classes(self, out):
        return {m.group(2): m.group(1) for m in re.finditer(r"(?m)^  (light|FULL) +(\S+)$", out)}

    def test_the_light_kinds_are_light_together(self):
        head = self.pull_request({path: "pr change of %s\n" % path for path in LIGHT})
        light, out, _ = self.decide(head)
        self.assertTrue(light, out)
        self.assertEqual(self.classes(out), {path: "light" for path in LIGHT})
        self.assertIn("decision: LIGHT", out)

    def test_every_other_path_is_full(self):
        head = self.pull_request({path: "pr change of %s\n" % path for path in FULL})   # (a .gitignore that says `x` would hide the files named x)
        light, out, _ = self.decide(head)
        self.assertFalse(light)
        self.assertEqual(self.classes(out), {path: "FULL" for path in FULL}, out)

    def test_a_pull_request_of_one_file_that_is_not_light_is_full(self):
        for path in ("VERSION", "CMakeLists.txt", "src/ants_sim/a.cpp", "tools/mutate.py", ".github/workflows/ci.yml", "tests/scripts/test_hub_login.py", "tests/test_sim/test_x.cpp", "Dockerfile"):
            light, out, _ = self.decide(self.pull_request({path: "x\n"}))
            self.assertFalse(light, path)
            self.assertEqual(self.classes(out), {path: "FULL"}, out)

    def test_a_page_that_the_skipped_jobs_read_makes_a_pull_request_full_whatever_else_it_changes(self):
        for path, readers in sorted(NATIVE_READS.items()):
            with self.subTest(path):
                light, out, _ = self.decide(self.pull_request({"docs/a.md": "b\n", "README.md": "y\n", path: "changed\n"}))
                self.assertFalse(light, "%s is read by %s, and a light pull request skips those jobs:\n%s" % (path, readers, out))
                self.assertEqual(self.classes(out), {"docs/a.md": "light", "README.md": "light", path: "FULL"}, out)
        light, out, _ = self.decide(self.pull_request({"web/home/chat.js": "b\n", "web/front/preview_new.txt": "y\n", "web/matches.html": "z\n"}))
        self.assertTrue(light, "the pages that no skipped job reads stay light:\n" + out)

    def test_one_full_file_among_light_ones_makes_it_full_and_is_named(self):
        head = self.pull_request({"docs/a.md": "b\n", "web/a.js": "b\n", "src/a.cpp": "int b;\n", "README.md": "y\n"})
        light, out, _ = self.decide(head)
        self.assertFalse(light)
        self.assertEqual(self.classes(out), {"docs/a.md": "light", "web/a.js": "light", "src/a.cpp": "FULL", "README.md": "light"})
        self.assertIn("decision: full checks (1 changed file(s) need the full checks)", out)

    def test_a_change_to_the_decision_script_or_the_workflow_is_full(self):
        for path in ("tools/ci_light.sh", ".github/workflows/ci.yml"):
            self.assertFalse(self.decide(self.pull_request({path: "x\n", "docs/a.md": "b\n"}))[0], path)

    def test_a_rename_counts_under_both_names(self):
        head = self.commit(self.base, {"src/a.cpp": None, "docs/a_code.md": "int a;\n"})            # (the same bytes: git would see a rename and name only the new file)
        light, out, _ = self.decide(head)
        self.assertFalse(light, "a source file that becomes a document is full:\n" + out)
        self.assertEqual(self.classes(out), {"src/a.cpp": "FULL", "docs/a_code.md": "light"})
        self.assertFalse(self.decide(self.commit(self.base, {"docs/a.md": None, "src/a_doc.cpp": "a\n"}))[0])
        light, out, _ = self.decide(self.commit(self.base, {"docs/a.md": None, "docs/b.md": "a\n"}))
        self.assertTrue(light, out)
        self.assertEqual(self.classes(out), {"docs/a.md": "light", "docs/b.md": "light"})

    def test_a_deleted_file_counts(self):
        self.assertFalse(self.decide(self.pull_request({"src/a.cpp": None, "docs/a.md": "z\n"}))[0])
        self.assertTrue(self.decide(self.pull_request({"docs/a.md": None}))[0])

    def test_a_bumped_submodule_counts_although_git_is_told_to_ignore_submodules(self):
        """`git diff` obeys diff.ignoreSubmodules and `ignore = all` in .gitmodules, and then leaves a bumped submodule out of the list: a document and a bump of third_party/ came out light."""
        link = "third_party/sub"
        before = self.commit(self.base, {link: Gitlink("1" * 40)})
        self.git("update-ref", "refs/remotes/origin/main", before)                              # (main has the submodule, so that only the bump is a change)
        head = self.commit(before, {link: Gitlink("2" * 40), "docs/a.md": "a bump and a document\n"})
        config = os.path.join(os.path.dirname(self.root), "global_config")
        with open(config, "w", encoding="utf-8") as f:
            f.write("[diff]\n\tignoreSubmodules = all\n")
        gitmodules = os.path.join(self.root, ".gitmodules")
        for how in ("git as it is", "diff.ignoreSubmodules = all", "ignore = all in .gitmodules"):
            with self.subTest(how):
                if how.endswith(".gitmodules"):
                    with open(gitmodules, "w", encoding="utf-8") as f:
                        f.write('[submodule "sub"]\n\tpath = %s\n\turl = https://example.org/sub.git\n\tignore = all\n' % link)
                light, out, _ = self.decide(head, base=before, **({"GIT_CONFIG_GLOBAL": config} if how.startswith("diff.") else {}))
                self.assertFalse(light, out)
                self.assertEqual(self.classes(out), {link: "FULL", "docs/a.md": "light"}, out)
        os.remove(gitmodules)
        # the same pull request without the bump is a document
        light, out, _ = self.decide(self.commit(before, {"docs/a.md": "a document\n"}), base=before, GIT_CONFIG_GLOBAL=config)
        self.assertTrue(light, out)

    def test_it_compares_with_the_merge_base_so_a_base_that_moved_on_changes_nothing(self):
        moved = self.pull_request({"src/a.cpp": "int moved;\n", "VERSION": "9.9.9\n"})        # the base branch got a change of code while the pull request was open
        head = self.pull_request({"docs/a.md": "mine\n"})
        light, out, _ = self.decide(head, base=moved)
        self.assertTrue(light, out)
        self.assertEqual(self.classes(out), {"docs/a.md": "light"})
        self.assertIn(self.base[:12] + " (merge base)", out)
        # the other way round: a pull request that changes code is full whatever the base did
        self.assertFalse(self.decide(self.pull_request({"src/a.cpp": "int mine;\n"}), base=moved)[0])

    def test_a_pull_request_against_another_branch_that_carries_code_main_lacks_is_full(self):
        """A pull request against staging, which holds code that main does not, is light against its base and full against main. Retargeting it to main starts no run, so the verdict
        must already hold for main."""
        staging = self.pull_request({"src/a.cpp": "int staging;\n", "VERSION": "9.9.9\n"})
        head = self.commit(staging, {"docs/a.md": "mine\n"})
        light, out, _ = self.decide(head, base=staging)
        self.assertFalse(light, out)
        self.assertEqual(self.classes(out), {"docs/a.md": "light", "src/a.cpp": "FULL", "VERSION": "FULL"}, out)
        self.assertIn("against main: " + self.base[:12] + " (merge base)", out)
        self.assertIn("pull request: " + staging[:12] + " (merge base)", out)
        # once main has taken what staging holds, only the document is left, and there is one comparison again
        self.git("update-ref", "refs/remotes/origin/main", staging)
        light, out, _ = self.decide(head, base=staging)
        self.assertTrue(light, out)
        self.assertNotIn("against main", out)
        # a pull request against main itself, and one against a branch that main contains, are one comparison too
        light, out, _ = self.decide(self.commit(self.base, {"docs/a.md": "x\n"}), base=self.base)
        self.assertNotIn("against main", out)
        # the other way round: a light pull request against staging stays light for main only if it is light there as well
        self.git("update-ref", "refs/remotes/origin/main", self.base)
        self.assertFalse(self.decide(self.commit(staging, {}), base=staging)[0], "the same commit as staging: nothing against its base, code against main")

    def test_a_main_that_moved_on_changes_nothing_and_a_main_that_is_not_there_is_full(self):
        moved = self.pull_request({"src/a.cpp": "int moved;\n"})
        head = self.pull_request({"docs/a.md": "mine\n"})
        self.git("update-ref", "refs/remotes/origin/main", moved)                            # (a change of code that the pull request does not have)
        for base in (self.base, moved):
            light, out, _ = self.decide(head, base=base)
            self.assertTrue(light, out)
            self.assertEqual(self.classes(out), {"docs/a.md": "light"})
        self.git("update-ref", "refs/remotes/origin/main", self.commit(None, {"f": "1\n"}, "unrelated"))        # (a root commit: no merge base with the head)
        light, out, _ = self.decide(head)
        self.assertFalse(light)
        self.assertIn("no merge base of the pull request and main", out)
        self.git("update-ref", "-d", "refs/remotes/origin/main")
        light, out, _ = self.decide(head)
        self.assertFalse(light)
        self.assertIn("the branch main is not there", out)

    def test_main_is_a_remote_branch_of_origin_of_plain_characters_and_nothing_else(self):
        head = self.pull_request({"docs/a.md": "mine\n"})
        self.git("update-ref", "refs/remotes/origin/probe-main", self.base)
        self.git("update-ref", "refs/heads/main", self.pull_request({"src/a.cpp": "int local;\n"}))
        light, out, _ = self.decide(head, MAIN_REF="refs/remotes/origin/probe-main")
        self.assertTrue(light, out)
        for bad in ("", "main", "origin/main", "refs/heads/main", "refs/remotes/upstream/main", "refs/remotes/origin/", "refs/remotes/origin/-x", "--output=x", "refs/remotes/origin/a b",
                    "refs/remotes/origin/main;touch pwned", "refs/remotes/origin/$(touch pwned)", "refs/remotes/origin/main\nrefs/remotes/origin/probe-main", "refs/remotes/origin/ma*", "refs/remotes/origin/ma\u00efn", "refs/remotes/origin/\u00e9", "refs/remotes/origin/{a,b}",
                    "refs/remotes/origin/a[b]", "refs/remotes/origin/a!b", "refs/remotes/origin/a@b"):
            light, out, _ = self.decide(head, MAIN_REF=bad)
            if bad == "":
                self.assertTrue(light, "an empty MAIN_REF is the default: " + out)
                continue
            self.assertFalse(light, bad)
            self.assertIn("MAIN_REF is not a branch of origin", out, bad)
        self.assertFalse(os.path.exists(os.path.join(self.root, "pwned")))
        light, out, _ = self.decide(head, MAIN_REF="refs/remotes/origin/nothing-here")
        self.assertFalse(light)
        self.assertIn("the branch main is not there", out)

    def test_a_pull_request_of_several_commits_is_judged_as_a_whole(self):
        first = self.pull_request({"src/a.cpp": "int b;\n"})
        head = self.commit(first, {"src/a.cpp": "int a;\n", "docs/a.md": "c\n"})        # the code is changed and changed back: what remains is a document
        light, out, _ = self.decide(head)
        self.assertTrue(light, out)
        first = self.pull_request({"src/a.cpp": "int c;\n"})
        self.assertFalse(self.decide(self.commit(first, {"docs/a.md": "d\n"}))[0])

    def test_what_is_not_a_pull_request_with_a_change_is_full(self):
        head = self.pull_request({"docs/a.md": "x\n"})
        self.assertTrue(self.decide(head)[0])
        for name in ("push", "workflow_dispatch", "schedule", "", None):
            light, out, _ = self.decide(head, EVENT=name)
            self.assertFalse(light, name)
            self.assertIn("not a pull request", out)
        # (capitals without an F, and letters that are not ASCII, whatever the commit is: in a CI run the bash of macOS (3.2) took 7A41A713... for a commit id, as the ranges of its patterns follow the
        # collation of the locale; a commit id has no F one time in 13, so the test used to fail by chance)
        for bad in (None, "", "main", "HEAD", head[:12], head.upper(), head + "0", "--output=x", "-h", head[:39] + "g", "@", "HEAD~1", "refs/heads/main", "ABCDE" * 8, "A" * 40, "e" * 39 + "E",
                    "0" * 39 + "B", "0" * 39 + "\u00e9", "0" * 38 + "\u00e9\u00e9"):
            for name in ("BASE_SHA", "HEAD_SHA"):
                light, out, _ = self.decide(head, **{name: bad})
                self.assertFalse(light, "%s %r" % (name, bad))
                self.assertIn("is not a full commit id", out, "%s %r: git must not be asked about it" % (name, bad))
        self.assertFalse(self.decide(head, HEAD_SHA="0" * 40)[0], "a commit that is not there")
        self.assertFalse(self.decide(head, BASE_SHA="1" * 40)[0])
        empty = self.pull_request({})
        light, out, _ = self.decide(empty)
        self.assertFalse(light, "no changed file")
        self.assertIn("no changed file was found", out)

    def test_a_pull_request_with_no_common_history_or_outside_a_repository_is_full(self):
        head = self.pull_request({"docs/a.md": "x\n"})
        unrelated = self.commit(None, {"f": "1\n"}, "unrelated")                           # (a root commit: no merge base with the base)
        light, out, _ = self.decide(head, base=unrelated)
        self.assertFalse(light)
        self.assertIn("no merge base", out)
        elsewhere = tempfile.mkdtemp(prefix="ci_light_norepo.")
        self.addCleanup(shutil.rmtree, elsewhere, ignore_errors=True)
        environment = dict(GIT_ENV, EVENT="pull_request", BASE_SHA=self.base, HEAD_SHA=head, GITHUB_OUTPUT=self.out_file, GIT_CEILING_DIRECTORIES=os.path.dirname(elsewhere))
        done = subprocess.run(["bash", SCRIPT], cwd=elsewhere, env=environment, capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        with open(self.out_file, encoding="utf-8") as f:
            self.assertEqual(f.read().splitlines()[-1], "light=false")

    def test_a_git_that_cannot_list_the_files_is_full(self):
        head = self.pull_request({"docs/a.md": "x\n"})
        tree = self.git("rev-parse", head + "^{tree}")
        os.chmod(os.path.join(self.root, ".git", "objects", tree[:2]), 0o755)
        os.remove(os.path.join(self.root, ".git", "objects", tree[:2], tree[2:]))            # (the commit is there, so there is a merge base, but its tree is not)
        light, out, _ = self.decide(head)
        self.assertFalse(light)
        self.assertIn("git could not list the changed files", out)

    def test_a_path_cannot_put_a_workflow_command_or_a_control_character_in_the_log(self):
        names = ["::error::pwned", "::set-output name=light::true", "docs/a b.md", "docs/quote'.md", "docs/dollar$x.md", "docs/new\nline.md", "docs/esc\x1b[31m.md", "docs/äö.md", "docs/tab\t.md",
                 "full\n::error::a line of its own", "src/full\n::set-output name=light::true", "src/esc\x1b[31m.cpp"]          # (the last three are full: a path that needs the full checks is printed quoted too)
        head = self.pull_request({name: "x\n" for name in names})
        light, out, _ = self.decide(head)
        self.assertFalse(light, "the names at the root are not light")
        for line in out.splitlines():
            self.assertFalse(line.startswith("::"), "a line of the log begins with a workflow command: " + line)
            self.assertNotIn("\x1b", line)
        self.assertEqual(sum(1 for line in out.splitlines() if re.match(r"  (light|FULL) ", line)), len(names), out)
        only_docs = self.pull_request({name: "x\n" for name in names if name.startswith("docs/")})
        self.assertTrue(self.decide(only_docs)[0], "odd names under docs/ are documents")

    def test_it_runs_without_a_github_output_file_and_a_big_pull_request_is_summarized(self):
        head = self.pull_request(dict({"docs/f%03d.md" % i: "x\n" for i in range(220)}, **{"src/s%03d.cpp" % i: "x\n" for i in range(160)}))
        light, out, _ = self.decide(head)
        self.assertFalse(light)
        self.assertIn("changed files: 380 (220 light, 160 that need the full checks)", out)
        self.assertIn("... and 70 more light files (not shown)", out)
        self.assertIn("... and 10 more files that need the full checks (not shown)", out)
        done = subprocess.run(["bash", SCRIPT], cwd=self.root, env=dict(GIT_ENV, EVENT="pull_request", BASE_SHA=self.base, HEAD_SHA=head), capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("decision: full checks", done.stdout)


LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
# the files of the light kinds (web/, docs/, the two top documents, the lists of mutants, the page tests), as a literal of C++ or a path of a script
LIGHT_KIND = re.compile(r"^/?(web|docs)(/|$)|^/?(CHANGELOG|README)\.md$|^/?tests/data/[^/]*_mutants\.json$|^/?tests/scripts/(test_web_|web_)")
BASES = ("ANTS_SOURCE_DIR", "TEST_DATA_DIR", "ORIGINAL_ASSETS_DIR", "source_dir()")      # (what a test of C++ finds the checkout with: tests/common/ants_test_paths.hpp)
# The C++ that reads a file of a light kind: (the source, the literals of the line after the base folder) -> a file to try the decision on
CPP_READS = {
    ("tests/test_ai/test_ai_names.cpp", "/web/front/lobby_rules.js"): RULES_JS,
    ("tests/test_app/test_start_menu_hosting.cpp", "/web/front/lobby_rules.js"): RULES_JS,
    ("tests/test_server/test_map_list.cpp", "web|front|preview_|.png"): "web/front/preview_tiny.png",
}


def code_of_cpp(line):
    """the line of C++ without its // comment (a // inside a string is none)"""
    in_string, i = False, 0
    while i < len(line):
        c = line[i]
        if in_string:
            if c == "\\":
                i += 1
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif line.startswith("//", i):
            return line[:i]
        i += 1
    return line


def cpp_reads():
    """{(source, literals): line} for every line of C++ in src/, include/, tests/ and tools/ that names a file of a light kind: after the folder of the checkout (ANTS_SOURCE_DIR and the like), or as a
    literal that begins with web/ or docs/. A path that is built over several lines, or by a function, is not found: the review of a test that reads a page must look for it."""
    found = {}
    for folder in ("src", "include", "tests", "tools"):
        for base, _, names in os.walk(os.path.join(REPO, folder)):
            for name in names:
                if not name.endswith((".cpp", ".hpp", ".h", ".inc")):
                    continue
                path = os.path.join(base, name)
                with open(path, encoding="utf-8", errors="replace") as f:
                    for number, line in enumerate(f, 1):
                        code = code_of_cpp(line)
                        starts = [code.index(b) for b in BASES if b in code]
                        after = LITERAL.findall(code[min(starts):]) if starts else []
                        if after and LIGHT_KIND.match(after[0]):
                            found[(os.path.relpath(path, REPO).replace(os.sep, "/"), "|".join(after))] = number
                        elif not starts:
                            for literal in LITERAL.findall(code):
                                if re.match(r"^/?(web|docs)/\S|^/?(CHANGELOG|README)\.md$", literal):
                                    found[(os.path.relpath(path, REPO).replace(os.sep, "/"), "(no folder) " + literal)] = number
    return found


class TheSkippedJobsReadNoLightFile(unittest.TestCase):
    """A light pull request skips the macOS job and the Windows jobs. What these build and run: the game, the server and the test programs, ctest (all of it, on all three systems), the tool
    self-tests, the E2E runner and, on macOS, the two script suites (tests/scripts/test_start_game.sh, test_ants_server.sh with the node checks it runs). The files of the light kinds that any of
    these reads are full in tools/ci_light.sh (NATIVE_READS at the top says which, and by whom); these tests find the reads in the sources, so that a test that starts to read another page fails
    here until the file is in that list. (The python tests that macOS runs read pages and documents too, as its document, version and spec checks do; Linux runs the same ones.)"""

    def test_the_c_plus_plus_of_ctest_the_tools_and_the_e2e_runner_read_these_pages_and_no_others(self):
        found = cpp_reads()
        self.assertEqual(sorted(found), sorted(CPP_READS), "a C++ source names a file of web/ or docs/ (or the like) that is not listed: make the file full in is_light of tools/ci_light.sh, "
                         "add it to NATIVE_READS and CPP_READS in this test, and say so in docs/WORKFLOW.md")
        for (source, _), representative in CPP_READS.items():
            self.assertIn(representative, NATIVE_READS, source)

    def test_the_shell_suites_the_node_checks_and_the_page_harness_read_these_files_and_no_others(self):
        found = set()
        shell = re.compile(r'(?:\$ROOT|\$\{ROOT\})/((?:web|docs)/[A-Za-z0-9_./*-]+|CHANGELOG\.md|README\.md|tests/data/[a-z_]+_mutants\.json|tests/scripts/(?:test_web_|web_)[A-Za-z0-9_.]+)')
        for name in ("test_ants_server.sh", "test_start_game.sh", "e2e_deadlines.sh"):
            with open(os.path.join(HERE, name), encoding="utf-8") as f:
                for line in f:
                    if not line.lstrip().startswith("#"):
                        found |= set(shell.findall(line))
        with open(os.path.join(HERE, "web_page_harness.js"), encoding="utf-8") as f:
            harness = f.read()
        found |= {"web/front/" + n for n in re.findall(r"loadFront\('([a-z_]+\.js)'", harness)}
        found |= {"web/" + n for n in re.findall(r"loadFile\('((?:home|front)/[a-z_]+\.js)'", harness)}
        found |= {"web/" + n for n in re.findall(r"'(home/[a-z_]+\.js)'", harness)}
        for name in ("web_lobby_server_check.js", "web_lobby_handoff_check.js", "web_fill_check.js"):         # (the node checks that the script suite runs, and what they load)
            with open(os.path.join(HERE, name), encoding="utf-8") as f:
                text = f.read()
            found |= {"web/" + n for n in re.findall(r"path\.join\(webDir, '([^']+)'\)", text)}
            found |= {"tests/scripts/" + n for n in re.findall(r"require\('\./(web_[a-z_]+\.js)'\)", text)}
        self.assertEqual(sorted(found), sorted(k for k in NATIVE_READS if not k.startswith("web/front/preview_")),
                         "a script of the macOS job (or the page harness that it runs) reads a file of a light kind that is not listed, or a listed one is not read any more: tools/ci_light.sh, NATIVE_READS here")

    def test_every_listed_file_is_of_a_light_kind_and_says_who_reads_it(self):
        self.assertGreater(len(NATIVE_READS), 15)
        for path, readers in NATIVE_READS.items():
            self.assertTrue(LIGHT_KIND.match(path), "%s is not of a light kind: it needs no entry" % path)
            self.assertTrue(readers)
        for path in NATIVE_READS:                                    # (the decision itself is tried on each of them in Decision)
            self.assertTrue(os.path.exists(os.path.join(REPO, path)), "%s is listed and not in the repository any more" % path)


class TheRulesAreWritten(unittest.TestCase):
    def test_the_script_names_what_the_documents_name(self):
        with open(SCRIPT, encoding="utf-8") as f:
            lines = f.read().splitlines()
        header = "\n".join(line for line in lines[:next(i for i, line in enumerate(lines) if not line.startswith("#"))])
        for needle in ("web/**", "docs/**", "CHANGELOG.md, README.md", "tests/data/*_mutants.json", "tests/scripts/test_web_*", "tests/scripts/web_*", "merge base", "Unknown is full", "ctest",
                       "the two shell suites", "python tests", "MAIN_REF", "retargeted"):
            self.assertIn(needle, header)

    def test_the_script_is_executable_and_text(self):
        self.assertTrue(os.access(SCRIPT, os.X_OK))
        with open(SCRIPT, "rb") as f:
            data = f.read()
        self.assertNotIn(b"\r", data)
        self.assertTrue(data.startswith(b"#!/usr/bin/env bash\n"))


if __name__ == "__main__":
    unittest.main()
