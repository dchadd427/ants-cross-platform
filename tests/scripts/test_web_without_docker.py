#!/usr/bin/env python3
"""Tests of tools/web_without_docker.py, the web image without Docker (run by ./run_tests.sh --fast and by the CI).

The tool replays the Dockerfile on this machine and serves the result with nginx, so that a session that has no Docker daemon (a cloud session) can run the browser checks of the web page
(docs/WORKFLOW.md, "Browser checks without Docker"). Groups of tests:

  - the reading of the Dockerfile: the repository's own file is read to the end, the Emscripten version is the one of its FROM, and a form that the tool cannot play is an error, never a
    step that is left out
  - the .dockerignore: what the image's build gets of the repository's own file (the three entries of .git, the archive of the changelog, no scripts, no tests, no program of the original
    game) and the rules of the matching (* inside a name, **, ! brings back, a folder takes its content with it)
  - the replay of small Dockerfiles: COPY (files, folders, patterns, --from), the folders that are moved (nothing is written to this machine's /src), ARG and --build-arg, ENV, WORKDIR,
    the warning for a path that is not moved, and the errors
  - the replay of the REAL Dockerfile with a fake compiler (Linux): every step but the compile runs as in the image, so the pages have what CI's checks of the image look for (the version,
    the build id, no placeholder left, the staging label, the changelog pages, the files of the runner stage)
  - the emsdk: the version that is there is taken as it is, another one or a strange folder is an error, a missing one is cloned at the tag, installed and activated
  - the ports that Emscripten would download (read from the port files of the installed Emscripten) and how they are put in its cache, from a git tag or from the mirror
  - nginx: docker/nginx.conf moved to a port and a folder, and (where nginx is installed) really started and asked for the routes of the site
  - the browser for the checks: Playwright's headless shell before Chromium, a script with --no-sandbox for root, a CHROME that is set is left alone
"""
import contextlib
import io
import os
import re
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import unittest
import urllib.request
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import web_without_docker as wd     # noqa: E402

LINUX = sys.platform.startswith("linux")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def read_path(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text="", mode=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    if mode:
        os.chmod(path, mode)


def quiet(function, *args, **kwargs):
    """(the result, what the tool said): the tool's progress lines are not the test's output."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        return function(*args, **kwargs), out.getvalue()


class Scratch(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_wwd_")
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def path(self, *parts):
        return os.path.join(self.tmp, *parts)


class TheDockerfileIsRead(unittest.TestCase):
    def setUp(self):
        self.text = read("Dockerfile")
        self.steps = wd.parse_dockerfile(self.text)

    def test_the_repositorys_dockerfile_is_read_to_the_end_and_no_step_is_lost(self):
        for op in ("FROM", "COPY", "RUN"):
            self.assertEqual(len([s for s in self.steps if s.op == op]), len(re.findall(r"^%s " % op, self.text, re.M)), op)
        self.assertEqual([s.stage for s in self.steps if s.op == "FROM"], [0, 1])
        self.assertTrue(all(s.text.strip() for s in self.steps))

    def test_the_emscripten_version_is_the_one_of_the_from_line(self):
        self.assertEqual(wd.emscripten_version(self.steps), re.search(r"^FROM emscripten/emsdk:(\S+) AS builder", self.text, re.M).group(1))

    def test_a_dockerfile_without_the_emsdk_image_is_an_error(self):
        with self.assertRaises(wd.ToolError):
            wd.emscripten_version(wd.parse_dockerfile("FROM ubuntu\nRUN true\n"))

    def test_continued_lines_are_joined_and_comments_and_blank_lines_are_dropped(self):
        steps = wd.parse_dockerfile("# one\nFROM a AS b\n\n# two\nRUN echo one && \\\n    # inside\n    echo two\nEXPOSE 80\nHEALTHCHECK --interval=5s \\\n  CMD true\nCMD [\"x\"]\n")
        self.assertEqual([s.op for s in steps], ["FROM", "RUN"])
        self.assertEqual(steps[1].text.split(), ["echo", "one", "&&", "echo", "two"])
        self.assertEqual(steps[1].line, 5)
        self.assertEqual(steps[0].words, ["a", "AS", "b"])

    def test_the_copy_flag_is_read(self):
        step = wd.parse_dockerfile("FROM a\nCOPY --from=builder /x/a /y/\n")[1]
        self.assertEqual((step.flags, step.words), ({"from": "builder"}, ["/x/a", "/y/"]))

    def test_what_the_tool_cannot_play_is_refused(self):
        for text in ("ADD x y", "ONBUILD RUN x", "SHELL [\"/bin/bash\", \"-c\"]", "RUN [\"a\", \"b\"]", "COPY [\"a\", \"b\"]", "RUN --mount=type=cache,target=/x true", "RUN cat <<EOF\nx\nEOF"):
            with self.subTest(text):
                with self.assertRaises(wd.ToolError):
                    wd.parse_dockerfile("FROM a\n" + text + "\n")
        with self.assertRaises(wd.ToolError):
            wd.parse_dockerfile("RUN true\n")

    def test_a_shell_form_that_starts_with_a_bracket_is_a_shell_form(self):
        self.assertEqual(wd.parse_dockerfile("FROM a\nRUN [ -f x ] && true\n")[1].text, "[ -f x ] && true")


class TheDockerIgnore(unittest.TestCase):
    def test_what_the_image_build_gets_of_the_repository(self):
        ignore = wd.DockerIgnore(read(".dockerignore"))
        for path in (".git/HEAD", ".git/packed-refs", ".git/refs/heads/main", ".gitignore", ".gitattributes", "CHANGELOG.md", "docs/CHANGELOG_ARCHIVE.md", "docker/resolve_build_id.sh",
                     "docker/nginx.conf", "Original-Ants/ants.chd", "src/ants_app/main.cpp", "web/shell.html", "web/front/logo.png", "tools/changelog_to_html.py", "asset_catalog/index.html", "VERSION"):
            self.assertFalse(ignore.excluded(path), path)
        for path in (".git/config", ".git/objects/ab/cdef", ".github/workflows/ci.yml", "tests/scripts/x.py", "docs/WORKFLOW.md", "README.md", "AGENTS.md", "run_tests.sh", "start_game.sh",
                     "build_web/index.js", "build/x", "dist/x", "Original-Ants/Ants.exe", "Original-Ants/ddraw.dll", "Original-Ants/Shaders/a.glsl", "Original-Ants/chat.txt", "scratch/web/x",
                     ".claude/settings.json", ".DS_Store", "web/.DS_Store"):
            self.assertTrue(ignore.excluded(path), path)

    def test_which_folders_can_hold_something_of_the_context(self):
        ignore = wd.DockerIgnore(read(".dockerignore"))
        for folder in (".git", ".git/refs", ".git/refs/heads", "docs", "src", "src/ants_app"):
            self.assertTrue(ignore.may_contain_included(folder), folder)
        for folder in (".git/objects", "tests", "docs/audit", "scratch", "build_web", ".github", "Original-Ants/Shaders"):
            self.assertFalse(ignore.may_contain_included(folder), folder)

    def test_the_matching_rules(self):
        ignore = wd.DockerIgnore("# a comment\n\n*.md\n!keep.md\nlogs/**\n**/tmp\nsub/?x\n/rooted\nlast\n!last\nlast\n")
        self.assertTrue(ignore.excluded("a.md"))
        self.assertFalse(ignore.excluded("keep.md"))
        self.assertFalse(ignore.excluded("docs/a.md"), "* does not cross a folder")
        self.assertTrue(ignore.excluded("logs/a/b"))
        self.assertTrue(ignore.excluded("a/b/tmp") and ignore.excluded("tmp"))
        self.assertTrue(ignore.excluded("tmp/inside"), "a folder takes its content with it")
        self.assertTrue(ignore.excluded("sub/ax") and not ignore.excluded("sub/aax"))
        self.assertTrue(ignore.excluded("rooted") and ignore.excluded("rooted/x"))
        self.assertTrue(ignore.excluded("last"), "the last pattern that matches wins")
        self.assertFalse(wd.DockerIgnore("").excluded("anything"))


class TheReplay(Scratch):
    def play(self, dockerfile, files=None, build_args=None, ignore=None, env=None):
        """(the folder that / is, what the tool said): a new context of `files` and a new root every time."""
        context, fs = self.path("context"), self.path("fs")
        shutil.rmtree(context, ignore_errors=True)
        shutil.rmtree(fs, ignore_errors=True)
        os.makedirs(context)
        os.makedirs(fs)
        for name, text in (files or {}).items():
            write(os.path.join(context, name), text)
        if ignore is not None:
            write(os.path.join(context, ".dockerignore"), ignore)
        replay = wd.Replay(wd.parse_dockerfile(dockerfile), context, fs, dict(env or os.environ), build_args, output=subprocess.DEVNULL)
        _, said = quiet(replay.play)
        return fs, said

    def seen(self, fs, *parts):
        return os.path.join(fs, *parts)

    def test_copy_puts_files_folders_and_patterns_where_docker_does(self):
        fs, _ = self.play("FROM a AS one\nWORKDIR /src\nCOPY a.txt b.txt ./\nCOPY dir/ ./dir/\nCOPY web/f1.* /usr/share/nginx/html/\nCOPY a.txt /src/renamed.txt\nCOPY dir/sub /etc/nginx/conf.d/\n",
                          {"a.txt": "A", "b.txt": "B", "dir/x.txt": "X", "dir/sub/y.txt": "Y", "web/f1.ico": "I", "web/f2.png": "P"})
        for path, text in (("src/a.txt", "A"), ("src/b.txt", "B"), ("src/dir/x.txt", "X"), ("src/dir/sub/y.txt", "Y"), ("usr/share/nginx/html/f1.ico", "I"), ("src/renamed.txt", "A"),
                           ("etc/nginx/conf.d/y.txt", "Y")):
            with open(self.seen(fs, path), encoding="utf-8") as f:
                self.assertEqual(f.read(), text, path)
        self.assertFalse(os.path.exists(self.seen(fs, "usr/share/nginx/html/f2.png")))

    def test_the_dockerignore_leaves_files_out_and_a_folder_keeps_what_is_brought_back(self):
        fs, _ = self.play("FROM a\nCOPY data/ /src/data/\nCOPY secret/ /src/secret/\n", {"data/a.txt": "a", "data/b.log": "b", "top.log": "t", "secret/x": "x", "secret/keep.txt": "k"},
                          ignore="**/*.log\nsecret\n!secret/keep.txt\n")
        self.assertTrue(os.path.isfile(self.seen(fs, "src/data/a.txt")))
        self.assertFalse(os.path.exists(self.seen(fs, "src/data/b.log")))
        self.assertTrue(os.path.isfile(self.seen(fs, "src/secret/keep.txt")))
        self.assertFalse(os.path.exists(self.seen(fs, "src/secret/x")))
        with self.assertRaises(wd.ToolError):
            self.play("FROM a\nCOPY top.log /src/\n", {"top.log": "t"}, ignore="*.log\n")
        fs, _ = self.play("FROM a\nCOPY data/ /src/\n", {"data/b.log": "b"}, ignore="*.log\n")
        self.assertTrue(os.path.isfile(self.seen(fs, "src/b.log")), "*.log is a pattern of the context's top folder only, as Docker's")

    def test_a_pattern_for_the_git_folder_takes_the_three_entries_that_the_dockerignore_lets_through(self):
        fs, _ = self.play("FROM a\nCOPY VERSION .git* /src/gitinfo/\n", {"VERSION": "1.2.3\n", ".git/HEAD": "ref: refs/heads/main\n", ".git/config": "secret", ".git/refs/heads/main": "0" * 40 + "\n", ".git/objects/ab/cd": "o",
                                                                          ".gitignore": "x", ".github/workflows/ci.yml": "w"}, ignore=".git\n!.git/HEAD\n!.git/refs\n.github\n")
        found = sorted(os.path.relpath(os.path.join(root, f), self.seen(fs, "src/gitinfo")) for root, _, files in os.walk(self.seen(fs, "src/gitinfo")) for f in files)
        self.assertEqual(found, [".gitignore", "HEAD", "VERSION", os.path.join("refs", "heads", "main")])

    def test_copy_from_a_stage_takes_what_the_stage_made_by_name_or_by_number(self):
        fs, _ = self.play("FROM a AS builder\nWORKDIR /src\nRUN mkdir out && echo h > out/a.html && echo j > out/b.js\nFROM b\nCOPY --from=builder /src/out/*.html /usr/share/nginx/html/\n"
                          "COPY --from=0 /src/out/b.js /usr/share/nginx/html/b2.js\n")
        self.assertEqual(sorted(os.listdir(self.seen(fs, "usr/share/nginx/html"))), ["a.html", "b2.js"])

    def test_run_works_in_the_moved_folders_and_never_in_this_machines(self):
        marker = "ants_wwd_%d.txt" % os.getpid()
        fs, _ = self.play("FROM a\nWORKDIR /src/app\nRUN pwd > /src/pwd.txt && echo hello > /src/%s && cp /src/%s /usr/share/nginx/html/g.txt\n" % (marker, marker))
        with open(self.seen(fs, "src/pwd.txt"), encoding="utf-8") as f:
            self.assertEqual(os.path.realpath(f.read().strip()), os.path.realpath(self.seen(fs, "src", "app")))
        self.assertTrue(os.path.isfile(self.seen(fs, "usr/share/nginx/html/g.txt")))
        self.assertFalse(os.path.exists("/src/" + marker), "a RUN wrote to this machine's /src")

    def test_the_text_of_run_is_moved_only_where_a_path_starts_with_the_folder(self):
        replay = wd.Replay([], self.path("c"), "/fs", {})
        self.assertEqual(replay.remap("cd /src/x && cat /src && echo build_web/src/a ./src/b /srcs /usr/share/nginx/html/* a/etc/nginx"),
                         "cd /fs/src/x && cat /fs/src && echo build_web/src/a ./src/b /srcs /fs/usr/share/nginx/html/* a/etc/nginx")
        self.assertEqual(replay.remap('PAGE=/src/build_web/index.html; x="/src/y"'), 'PAGE=/fs/src/build_web/index.html; x="/fs/src/y"')

    def test_arg_env_and_the_build_argument(self):
        dockerfile = "FROM a\nARG WHO=world\nARG EMPTY\nENV GREETING=hi\nWORKDIR /src\nRUN echo \"$GREETING ${WHO}[$EMPTY]\" > /src/out.txt\n"
        fs, said = self.play(dockerfile)
        with open(self.seen(fs, "src/out.txt"), encoding="utf-8") as f:
            self.assertEqual(f.read().strip(), "hi world[]")
        self.assertNotIn("warning", said)
        fs, said = self.play(dockerfile, build_args={"WHO": "ants", "NOT_DECLARED": "1"})
        with open(self.seen(fs, "src/out.txt"), encoding="utf-8") as f:
            self.assertEqual(f.read().strip(), "hi ants[]")
        self.assertIn("--build-arg NOT_DECLARED is not an ARG", said)

    def test_a_path_that_is_not_moved_is_a_warning_and_the_harmless_ones_are_not(self):
        _, said = self.play("FROM a\nRUN test -d /usr && true > /dev/null && echo '</strong>' > /dev/null && test -d /src && test -d /usr/share/nginx/html\n")
        warnings = [line for line in said.splitlines() if "warning" in line]
        self.assertEqual(len(warnings), 1, warnings)
        self.assertIn("RUN uses /usr, which this tool does not move", warnings[0])

    def test_the_errors(self):
        for dockerfile, files, wanted in (
                ("FROM a\nCOPY a.txt /etc/passwd\n", {"a.txt": "x"}, "outside the folders"),
                ("FROM a\nCOPY a.txt /src/\n", {}, "nothing of it is in the build context"),
                ("FROM a\nCOPY --from=nope /src/a /src/\n", {}, "not an earlier stage"),
                ("FROM a AS one\nFROM b\nCOPY --from=one /tmp/x /src/\n", {}, "outside the folders"),
                ("FROM a AS one\nFROM b\nCOPY --from=one /src/nothing /src/\n", {}, "nothing of it is in the stage"),
                ("FROM a\nRUN exit 3\n", {}, "RUN failed with status 3"),
                ("FROM a\nCOPY onlyone\n", {}, "needs a source and a destination")):
            with self.subTest(dockerfile):
                with self.assertRaises(wd.ToolError) as caught:
                    self.play(dockerfile, files)
                self.assertIn(wanted, str(caught.exception))


@unittest.skipUnless(LINUX and shutil.which("sed") and shutil.which("python3"), "the Dockerfile's RUN lines use GNU tools: Linux")
class TheRealDockerfileReplayed(Scratch):
    """The Dockerfile of the repository, step by step, with a fake Emscripten: `emcmake cmake -B build_web` makes the folder and `cmake --build build_web` makes what the compile makes (the page
    is web/shell.html with its script tag, and the game's three files). Everything else is the Dockerfile's own."""

    @classmethod
    def setUpClass(cls):
        cls.base = tempfile.mkdtemp(prefix="ants_wwd_real_")
        write(os.path.join(cls.base, "bin", "emcmake"), "#!/bin/sh\nmkdir -p build_web\n", 0o755)
        write(os.path.join(cls.base, "bin", "cmake"),
              "#!/bin/sh\nout=build_web/src/ants_app\nmkdir -p \"$out\"\n"
              "sed 's|{{{ SCRIPT }}}|<script async type=\"text/javascript\" src=\"index.js\"></script>|' web/shell.html > \"$out/index.html\"\n"
              "echo game > \"$out/index.js\"; echo wasm > \"$out/index.wasm\"; head -c 4096 /dev/zero > \"$out/index.data\"\n", 0o755)
        env = dict(os.environ, PATH=os.path.join(cls.base, "bin") + os.pathsep + os.environ["PATH"])
        steps = wd.parse_dockerfile(read("Dockerfile"))
        cls.site, cls.said = {}, {}
        for label, build_args in (("plain", {"ANTS_BUILD_ID": "abc1234"}), ("staging", {"ANTS_SITE_LABEL": "staging"})):
            fs = os.path.join(cls.base, label)
            os.makedirs(fs)
            with open(os.path.join(cls.base, label + ".log"), "w+", encoding="utf-8") as log:
                with contextlib.redirect_stdout(io.StringIO()):
                    wd.Replay(steps, REPO, fs, env, build_args, output=log).play()
                log.seek(0)
                cls.said[label] = log.read()
            cls.site[label] = fs

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.base, ignore_errors=True)

    def html(self, label, *parts):
        return os.path.join(self.site[label], "usr", "share", "nginx", "html", *parts)

    def page(self, label, name):
        with open(self.html(label, name), encoding="utf-8") as f:
            return f.read()

    def test_the_files_that_the_image_has_are_there(self):
        for name in ("index.html", "play.html", "lobby.html", "index.js", "index.wasm", "index.data", "changelog.html", "changelog_archive.html", "favicon.png", os.path.join("front", "logo.png"),
                     os.path.join("front", "LibreFranklin-Medium.ttf"), os.path.join("front", "classic.css")):
            self.assertTrue(os.path.isfile(self.html("plain", name)), name)
        self.assertTrue(os.path.isdir(self.html("plain", "asset_catalog")))
        with open(self.html("plain", "index.html"), "rb") as a, open(self.html("plain", "play.html"), "rb") as b:
            self.assertEqual(a.read(), b.read(), "play.html is a copy of index.html")
        with open(os.path.join(self.site["plain"], "etc", "nginx", "conf.d", "default.conf"), encoding="utf-8") as a:
            self.assertEqual(a.read(), read("docker", "nginx.conf"))

    def test_the_pages_name_the_version_and_the_build_and_hold_no_placeholder(self):
        version = "v" + read("VERSION").strip()
        for name in ("index.html", "play.html", "lobby.html"):
            text = self.page("plain", name)
            self.assertRegex(text, r'id="?game-version"?>%s<' % re.escape(version), name)
            self.assertRegex(text, r'id="?game-build-id"?>abc1234<', name)
            self.assertNotIn("@@", text, name)
            self.assertNotIn("staging", text.lower(), name)

    def test_the_build_id_comes_from_the_build_argument_else_from_the_checkout_else_from_the_date(self):
        self.assertIn("build id from the build argument", self.said["plain"])
        self.assertRegex(self.said["staging"], r"build id from the (git commit|build date)")
        self.assertNotIn("build id from the build argument", self.said["staging"])

    def test_the_game_page_has_the_cache_buster_once_and_the_size_of_the_data(self):
        text = self.page("plain", "index.html")
        self.assertEqual(len(re.findall(r'<script[^>]* src="?index\.js\?v=\d+"?', text)), 1)
        self.assertNotRegex(text, r"index\.js\?v=\d+\?v=")
        self.assertIn("parseInt('4096', 10)", text)

    def test_the_staging_label_is_in_the_title_and_the_footer(self):
        for name in ("index.html", "play.html", "lobby.html"):
            text = self.page("staging", name)
            self.assertIn("(staging)</title>", text, name)
            self.assertIn('id="site-label">staging<', text, name)

    def test_the_changelog_pages_are_made_from_the_changelogs(self):
        self.assertIn("v" + read("VERSION").strip(), self.page("plain", "changelog.html"))
        self.assertGreater(os.path.getsize(self.html("plain", "changelog_archive.html")), os.path.getsize(self.html("plain", "changelog.html")))

    def test_the_builder_got_what_the_dockerignore_lets_through_and_nothing_else(self):
        src = os.path.join(self.site["plain"], "src")
        for name in ("CMakeLists.txt", "VERSION", "include", "src", "web", "Original-Ants", "docker", "changelog"):
            self.assertTrue(os.path.exists(os.path.join(src, name)), name)
        for name in ("tests", "docs", "README.md", "run_tests.sh", ".github"):
            self.assertFalse(os.path.exists(os.path.join(src, name)), name)
        self.assertEqual(sorted(os.listdir(os.path.join(src, "changelog")))[:2], ["CHANGELOG.md", "CHANGELOG_ARCHIVE.md"])
        self.assertFalse(os.path.exists(os.path.join(src, "gitinfo", "config")), "a clone's .git/config is never part of the build")


class TheEmsdk(Scratch):
    def made(self, version):
        write(os.path.join(self.path("emsdk"), "upstream", "emscripten", "emscripten-version.txt"), '"%s"\n' % version)
        return self.path("emsdk")

    def test_the_version_that_is_there_is_taken_as_it_is(self):
        emsdk = self.made("3.1.58")
        with mock.patch.object(wd, "run") as run:
            wd.ensure_emsdk("3.1.58", emsdk)
        run.assert_not_called()
        self.assertEqual(wd.installed_emscripten_version(emsdk), "3.1.58")
        self.assertIsNone(wd.installed_emscripten_version(self.path("nothing")))

    def test_another_version_is_an_error_and_nothing_is_changed(self):
        emsdk = self.made("3.1.50")
        with mock.patch.object(wd, "run") as run:
            with self.assertRaises(wd.ToolError) as caught:
                wd.ensure_emsdk("3.1.58", emsdk)
        run.assert_not_called()
        self.assertIn("3.1.50", str(caught.exception))

    def test_a_folder_that_holds_something_else_is_an_error(self):
        write(os.path.join(self.path("emsdk"), "something"), "x")
        with mock.patch.object(wd, "run") as run:
            with self.assertRaises(wd.ToolError):
                wd.ensure_emsdk("3.1.58", self.path("emsdk"))
        run.assert_not_called()

    def test_a_missing_emsdk_is_cloned_at_the_tag_then_installed_and_activated(self):
        emsdk = self.path("deep", "emsdk")
        with mock.patch.object(wd, "run") as run:
            quiet(wd.ensure_emsdk, "3.1.58", emsdk)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual(len(commands), 3)
        self.assertEqual(commands[0][commands[0].index("--branch") + 1:], ["3.1.58", wd.EMSDK_URL, emsdk])
        self.assertEqual(commands[1:], [[os.path.join(emsdk, "emsdk"), "install", "3.1.58"], [os.path.join(emsdk, "emsdk"), "activate", "3.1.58"]])

    @unittest.skipUnless(shutil.which("bash"), "bash is needed")
    def test_the_environment_is_what_emsdk_env_sh_makes_of_this_one(self):
        write(os.path.join(self.path("emsdk"), "emsdk_env.sh"), 'export FROM_EMSDK="$(basename "$EMSDK_TEST")"\nexport PATH="/fake/bin:$PATH"\necho noise\n')
        with mock.patch.dict(os.environ, {"EMSDK_TEST": "/x/y", "KEEP_ME": "1"}):
            env = wd.emsdk_environment(self.path("emsdk"))
        self.assertEqual((env["FROM_EMSDK"], env["KEEP_ME"]), ("y", "1"))
        self.assertTrue(env["PATH"].startswith("/fake/bin:"))
        with self.assertRaises(wd.ToolError):
            wd.emsdk_environment(self.path("no emsdk"))

    def test_the_ports_go_to_the_cache_of_the_emsdk_or_to_the_one_that_is_named(self):
        emsdk = self.made("3.1.58")
        with mock.patch.object(wd, "ports_to_get", return_value=[("a", "https://github.com/o/a/archive/t.zip"), ("b", "https://github.com/o/b/archive/t.zip")]), mock.patch.object(wd, "fetch_port", side_effect=[True, False]) as fetched:
            _, said = quiet(wd.prepare_ports, REPO, emsdk, {})
        self.assertEqual([call.args[2] for call in fetched.call_args_list], [os.path.join(emsdk, "upstream", "emscripten", "cache", "ports")] * 2)
        self.assertIn("port a: put in Emscripten's cache from git", said)
        self.assertIn("port b: already in Emscripten's cache", said)
        with mock.patch.object(wd, "ports_to_get", return_value=[("a", "https://github.com/o/a/archive/t.zip")]), mock.patch.object(wd, "fetch_port", return_value=False) as fetched:
            quiet(wd.prepare_ports, REPO, emsdk, {"EM_CACHE": self.path("cache")})
        self.assertEqual(fetched.call_args.args[2], self.path("cache", "ports"))


class ThePorts(Scratch):
    PORT_FILES = {
        "sdl2": "TAG = 'release-2.28.4'\nHASH = 'x'\nSUBDIR = 'SDL-' + TAG\n\ndef get(ports, settings, shared):\n  ports.fetch_project('sdl2', f'https://github.com/libsdl-org/SDL/archive/{TAG}.zip', sha512hash=HASH)\n",
        "sdl2_ttf": "TAG = 'release-2.20.2' # Latest\nHASH = 'x'\n\ndeps = ['freetype', 'sdl2', 'harfbuzz']\n\n\ndef get(ports, settings, shared):\n"
                    "  ports.fetch_project('sdl2_ttf', f'https://github.com/libsdl-org/SDL_ttf/archive/{TAG}.zip', sha512hash=HASH)\n",
        "freetype": "TAG = 'version_1'\nHASH = 'x'\n\ndef get(ports, settings, shared):\n  ports.fetch_project('freetype', f'https://github.com/emscripten-ports/FreeType/archive/{TAG}.zip', sha512hash=HASH)\n",
        "harfbuzz": "VERSION = '3.2.0'\nHASH = 'x'\n\ndeps = ['freetype']\nvariants = {'harfbuzz-mt': {'PTHREADS': 1}}\n\n\ndef get(ports, settings, shared):\n"
                    "  ports.fetch_project('harfbuzz', f'https://github.com/harfbuzz/harfbuzz/releases/download/{VERSION}/harfbuzz-{VERSION}.tar.xz', sha512hash=HASH)\n",
    }

    def emsdk(self):
        for name, text in self.PORT_FILES.items():
            write(wd.port_file(self.path("emsdk"), name), text)
        return self.path("emsdk")

    def test_the_ports_come_from_the_flags_of_the_cmake_files_and_the_deps_of_the_port_files(self):
        found, said = quiet(wd.ports_to_get, REPO, self.emsdk())
        self.assertEqual(dict(found), {
            "sdl2": "https://github.com/libsdl-org/SDL/archive/release-2.28.4.zip",
            "sdl2_ttf": "https://github.com/libsdl-org/SDL_ttf/archive/release-2.20.2.zip",
            "freetype": "https://github.com/emscripten-ports/FreeType/archive/version_1.zip",
            "harfbuzz": "https://github.com/harfbuzz/harfbuzz/releases/download/3.2.0/harfbuzz-3.2.0.tar.xz"})
        self.assertNotIn("warning", said, "every flag of the CMake files has its port")

    def test_a_flag_that_the_tool_has_no_port_for_is_a_warning(self):
        write(self.path("ctx", "CMakeLists.txt"), 'set(F "-sUSE_SDL=2 -sUSE_ZLIB=1 -sALLOW_MEMORY_GROWTH=1")\n')
        found, said = quiet(wd.ports_to_get, self.path("ctx"), self.emsdk())
        self.assertEqual([name for name, _ in found], ["sdl2"])
        self.assertIn("-sUSE_ZLIB=1", said)

    def test_a_port_file_that_cannot_be_read_is_an_error(self):
        write(wd.port_file(self.path("emsdk"), "odd"), "def get(ports, settings, shared):\n  pass\n")
        write(wd.port_file(self.path("emsdk"), "odder"), "def get(ports, settings, shared):\n  ports.fetch_project('odder', f'https://x/{OTHER}.zip')\n")
        for name in ("odd", "odder", "missing"):
            with self.assertRaises(wd.ToolError):
                wd.port_info(self.path("emsdk"), name)

    def test_where_each_port_comes_from_without_a_github_archive(self):
        self.assertEqual(wd.source_of("https://github.com/libsdl-org/SDL/archive/release-2.28.4.zip"), ("git", "https://github.com/libsdl-org/SDL.git", "release-2.28.4", "SDL-release-2.28.4"))
        self.assertEqual(wd.source_of("https://github.com/emscripten-ports/FreeType/archive/version_1.zip"), ("git", "https://github.com/emscripten-ports/FreeType.git", "version_1", "FreeType-version_1"))
        self.assertEqual(wd.source_of("https://github.com/harfbuzz/harfbuzz/releases/download/3.2.0/harfbuzz-3.2.0.tar.xz"),
                         ("mirror", "https://storage.googleapis.com/webassembly/emscripten-ports/harfbuzz-3.2.0.tar.gz"))
        with self.assertRaises(wd.ToolError):
            wd.source_of("https://example.org/port.zip")

    @unittest.skipUnless(shutil.which("git"), "git is needed")
    def test_a_port_from_a_git_tag_is_put_in_the_cache_with_its_marker_and_taken_as_it_is_the_next_time(self):
        repo = self.path("repo")
        os.makedirs(repo)
        git = ["git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@example.org", "-c", "commit.gpgsign=false", "-c", "init.defaultBranch=main"]
        subprocess.run(git + ["init", "-q"], check=True)
        write(os.path.join(repo, "file.txt"), "one\n")
        write(os.path.join(repo, "sub", "inner.txt"), "two\n")
        subprocess.run(git + ["add", "."], check=True)
        subprocess.run(git + ["commit", "-q", "-m", "one"], check=True)
        subprocess.run(git + ["tag", "v1"], check=True)
        write(os.path.join(repo, "file.txt"), "later\n")
        subprocess.run(git + ["commit", "-qam", "two"], check=True)
        ports = self.path("ports")
        os.makedirs(ports)
        with mock.patch.object(wd, "source_of", return_value=("git", "file://" + repo, "v1", "Repo-v1")):
            self.assertTrue(wd.fetch_port("thing", "https://example.org/thing.zip", ports))
            with open(os.path.join(ports, "thing", "Repo-v1", "file.txt"), encoding="utf-8") as f:
                self.assertEqual(f.read(), "one\n", "the tag, not the branch")
            self.assertTrue(os.path.isfile(os.path.join(ports, "thing", "Repo-v1", "sub", "inner.txt")))
            self.assertFalse(os.path.exists(os.path.join(ports, "thing", "Repo-v1", ".git")))
            with open(os.path.join(ports, "thing", ".emscripten_url"), encoding="utf-8") as f:
                self.assertEqual(f.read(), "https://example.org/thing.zip\n")
            self.assertFalse(wd.fetch_port("thing", "https://example.org/thing.zip", ports), "the marker names the address: nothing is fetched again")
            self.assertTrue(wd.fetch_port("thing", "https://example.org/other.zip", ports), "another address is another port")

    def test_a_port_from_the_mirror_is_unpacked_into_the_cache(self):
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode="w:gz") as tar:
            payload = b"int x;\n"
            info = tarfile.TarInfo("harfbuzz-9.9.9/src/hb.h")
            info.size = len(payload)
            tar.addfile(info, io.BytesIO(payload))
        answer = mock.MagicMock()
        answer.__enter__.return_value.read.return_value = data.getvalue()
        ports = self.path("ports")
        os.makedirs(ports)
        with mock.patch.object(wd, "source_of", return_value=("mirror", "https://mirror.invalid/harfbuzz-9.9.9.tar.gz")), mock.patch.object(urllib.request, "urlopen", return_value=answer) as opened:
            self.assertTrue(wd.fetch_port("harfbuzz", "https://example.org/harfbuzz-9.9.9.tar.xz", ports))
        self.assertEqual(opened.call_args[0][0], "https://mirror.invalid/harfbuzz-9.9.9.tar.gz")
        self.assertTrue(os.path.isfile(os.path.join(ports, "harfbuzz", "harfbuzz-9.9.9", "src", "hb.h")))
        with mock.patch.object(wd, "source_of", return_value=("mirror", "https://mirror.invalid/x")), mock.patch.object(urllib.request, "urlopen", side_effect=OSError("no route")):
            with self.assertRaises(wd.ToolError):
                wd.fetch_port("other", "https://example.org/other.tar.xz", ports)


class TheSiteConfiguration(Scratch):
    def test_nginx_conf_goes_to_a_port_and_a_folder(self):
        text = wd.site_config(read("docker", "nginx.conf"), "/w/fs", "/w/cache", 1234)
        self.assertEqual(re.findall(r"^\s*listen\s+[^;]*;", text, re.M), ["    listen 127.0.0.1:1234;"])
        self.assertIn("root /w/fs/usr/share/nginx/html;", text)
        self.assertIn("alias /w/fs/usr/share/nginx/html/asset_catalog/sprites/;", text)
        self.assertIn("proxy_cache_path /w/cache/ants_stats ", text)
        self.assertNotIn("/usr/share/nginx", text.replace("/w/fs/usr/share/nginx/html", ""))
        self.assertNotIn("/var/cache/nginx", text)
        self.assertIn("location = /busy", text, "the rest of the file is as it was")

    def test_a_file_that_the_tool_cannot_move_is_an_error(self):
        for conf, wanted in (("server {\n    listen 80;\n    listen 80;\n}\n", "exactly one `listen 80;`"), ("server {\n    root /usr/share/nginx/html;\n}\n", "exactly one `listen 80;`"),
                             ("server {\n    listen 80;\n    access_log /var/log/nginx/x.log;\n}\n", "/var/log/nginx/x.log"), ("server {\n    listen 80;\n    include /var/run/x.conf;\n}\n", "/var/run/x.conf")):
            with self.subTest(conf):
                with self.assertRaises(wd.ToolError) as caught:
                    wd.site_config(conf, "/w/fs", "/w/cache", 1)
                self.assertIn(wanted, str(caught.exception))
        moved = wd.site_config("server {\n    listen 80;\n    listen [::]:80;\n    # the log is /var/log/nginx/x.log\n}\n", "/w/fs", "/w/cache", 1)
        self.assertEqual(re.findall(r"listen[^\n]*", moved), ["listen 127.0.0.1:1;"], "the IPv6 listen goes, a path in a comment is no use of it")

    def test_the_main_file_writes_only_under_its_prefix(self):
        text = wd.main_config("/w/nginx", "/w/nginx/site.conf", "/etc/nginx/mime.types", False)
        self.assertNotIn("user root", text)
        self.assertIn("user root;", wd.main_config("/w/nginx", "/w/nginx/site.conf", "/etc/nginx/mime.types", True))
        self.assertIn("include /w/nginx/site.conf;", text)
        paths = re.findall(r"(?:pid|error_log|access_log|_temp_path)\s+(/\S+?)[; ]", text)
        self.assertEqual(len(paths), 8)
        self.assertTrue(all(p.startswith("/w/nginx/") for p in paths), paths)

    def fake_site(self):
        work = self.path("work")
        write(os.path.join(work, "fs", "etc", "nginx", "conf.d", "default.conf"), read("docker", "nginx.conf"))
        html = os.path.join(work, "fs", "usr", "share", "nginx", "html")
        write(os.path.join(html, "lobby.html"), "<title>the lobby</title>")
        write(os.path.join(html, "index.html"), "<title>the game</title>")
        write(os.path.join(html, "index.wasm"), "\0asm")
        return work

    def nginx_ready(self):
        try:
            wd.mime_types()
        except wd.ToolError:
            self.skipTest("nginx is not installed")
        if not shutil.which("nginx") or not LINUX:
            self.skipTest("nginx is not installed")

    def test_nginx_serves_the_routes_of_the_site_from_the_moved_folders(self):
        self.nginx_ready()
        work = self.fake_site()
        server = wd.Nginx(work, wd.free_port())
        self.addCleanup(server.stop)
        url = server.start()
        for path, expected in (("", "the lobby"), ("?join=ABC", "the game"), ("?embed=1", "the game"), ("index.html", "the game"), ("lobby.html", "the lobby")):
            with urllib.request.urlopen(url + path, timeout=5) as answer:
                self.assertIn(expected, answer.read().decode("utf-8"), path)
        with urllib.request.urlopen(url + "index.wasm", timeout=5) as answer:
            self.assertEqual(answer.headers["Content-Type"], "application/wasm")
            self.assertEqual(answer.headers["Cache-Control"], "no-cache, must-revalidate")
            self.assertEqual(answer.headers["Cross-Origin-Embedder-Policy"], "require-corp")
        with open(os.path.join(work, "fs", "etc", "nginx", "conf.d", "default.conf"), encoding="utf-8") as f:
            self.assertEqual(f.read(), read("docker", "nginx.conf"), "the copy of the image's file is left as it is")
        server.stop()
        self.assertFalse(server.running())

    def test_the_command_after_the_dashes_gets_the_address_and_its_status_is_the_exit_status(self):
        self.nginx_ready()
        work = self.fake_site()
        out = self.path("address.txt")
        status = quiet(wd.main, ["x", "--reuse", "--work", work, "--port", "0", "--", "sh", "-c", 'echo "$ANTS_WEB_URL" > "$0"; exit 7', out])[0]
        self.assertEqual(status, 7)
        with open(out, encoding="utf-8") as f:
            self.assertRegex(f.read().strip(), r"^http://127\.0\.0\.1:\d+/$")
        self.assertFalse(wd.Nginx(work, 1).running(), "nginx is stopped after the command")


class TheBrowser(Scratch):
    def browsers(self, *entries):
        self.count = getattr(self, "count", 0) + 1
        base = self.path("browsers%d" % self.count)
        for entry in entries:
            write(os.path.join(base, entry), "#!/bin/sh\n", 0o755)
        return base

    def test_the_headless_shell_comes_before_chromium_and_chromium_before_the_path(self):
        base = self.browsers("chromium-1194/chrome-linux/chrome", "chromium_headless_shell-1194/chrome-linux/headless_shell", "ffmpeg-1011/ffmpeg-linux")
        with mock.patch.dict(os.environ, {"PLAYWRIGHT_BROWSERS_PATH": base}):
            self.assertEqual(wd.find_chromium(), os.path.join(base, "chromium_headless_shell-1194", "chrome-linux", "headless_shell"))
        base = self.browsers("chromium-1194/chrome-linux/chrome", "chromium-1100/chrome-linux/chrome")
        with mock.patch.dict(os.environ, {"PLAYWRIGHT_BROWSERS_PATH": base}):
            self.assertEqual(wd.find_chromium(), os.path.join(base, "chromium-1194", "chrome-linux", "chrome"), "the newest")
        with mock.patch.dict(os.environ, {"PLAYWRIGHT_BROWSERS_PATH": self.path("none")}), mock.patch.object(wd.shutil, "which", side_effect=lambda n: "/usr/bin/chromium" if n == "chromium" else None):
            self.assertEqual(wd.find_chromium(), "/usr/bin/chromium")
            with mock.patch.object(wd.shutil, "which", return_value=None):
                self.assertIsNone(wd.find_chromium())

    def test_a_browser_that_is_named_is_left_alone_and_none_found_changes_nothing(self):
        self.assertEqual(wd.browser_environment(self.path("w"), {"CHROME": "/mine/chrome"}), {"CHROME": "/mine/chrome"})
        with mock.patch.object(wd, "find_chromium", return_value=None):
            self.assertEqual(wd.browser_environment(self.path("w"), {"A": "1"}), {"A": "1"})

    def started_as(self, user, *entries):
        base = self.browsers(*entries)
        with mock.patch.dict(os.environ, {"PLAYWRIGHT_BROWSERS_PATH": base}), mock.patch.object(wd.os, "geteuid", create=True, return_value=user):
            env = wd.browser_environment(self.path("work"), {"A": "1"})
        self.assertEqual(env["A"], "1")
        self.assertEqual(env["CHROME"], self.path("work", "chrome-for-the-checks"))
        self.assertTrue(os.access(env["CHROME"], os.X_OK))
        return base, read_path(env["CHROME"])

    def test_the_browser_is_started_through_a_script_that_gives_it_a_desktops_mouse(self):
        base, text = self.started_as(1000, "chromium_headless_shell-1194/chrome-linux/headless_shell")
        self.assertIn("'%s'" % os.path.join(base, "chromium_headless_shell-1194", "chrome-linux", "headless_shell"), text)
        self.assertIn("--blink-settings=primaryPointerType=4,availablePointerTypes=4,primaryHoverType=2,availableHoverTypes=2", text)
        self.assertNotIn("--no-sandbox", text)
        self.assertTrue(text.rstrip().endswith('"$@"'))

    def test_as_root_it_also_has_no_sandbox(self):
        _, text = self.started_as(0, "chromium-1194/chrome-linux/chrome")
        self.assertIn("--no-sandbox", text)
        self.assertIn("--blink-settings=", text)


class TheCommandLine(Scratch):
    def run_main(self, *argv):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            status, said = quiet(wd.main, ["x"] + list(argv))
        return status, said, err.getvalue()

    @unittest.skipUnless(LINUX, "the tool runs on Linux")
    def test_a_build_argument_without_a_value_is_refused(self):
        with self.assertRaises(SystemExit) as caught:
            self.run_main("--build-arg", "oops")
        self.assertEqual(caught.exception.code, 2)

    @unittest.skipUnless(LINUX, "the tool runs on Linux")
    def test_stop_with_nothing_running_is_fine_and_reuse_with_nothing_built_is_not(self):
        self.assertEqual(self.run_main("--stop", "--work", self.path("w"))[0], 0)
        with mock.patch.object(wd.shutil, "which", side_effect=lambda name: "/usr/sbin/nginx"):
            status, _, err = self.run_main("--reuse", "--work", self.path("w"))
        self.assertEqual(status, 1)
        self.assertIn("nothing was built", err)

    @unittest.skipUnless(LINUX, "the tool runs on Linux")
    def test_a_machine_without_nginx_or_a_tool_says_so_and_builds_nothing(self):
        real = shutil.which
        for missing, status in (("nginx", 3), ("cmake", 3)):
            with mock.patch.object(wd.shutil, "which", side_effect=lambda name, m=missing: None if name == m else real(name) or "/bin/true"):
                got, _, err = self.run_main("--work", self.path("w"), "--emsdk", self.path("e"))
            self.assertEqual(got, status, missing)
            self.assertIn(missing + " is not installed", err)
        self.assertFalse(os.path.exists(self.path("w", "fs")))

    def test_macos_and_windows_are_sent_to_docker(self):
        with mock.patch.object(wd.sys, "platform", "darwin"):
            status, _, err = self.run_main()
        self.assertEqual(status, 3)
        self.assertIn("docker build", err)

    def test_the_documents_name_the_tool(self):
        for document in (("docs", "WORKFLOW.md"), ("AGENTS.md",)):
            self.assertTrue("tools/web_without_docker.py" in read(*document), "%s does not name tools/web_without_docker.py" % os.path.join(*document))


if __name__ == "__main__":
    unittest.main()
