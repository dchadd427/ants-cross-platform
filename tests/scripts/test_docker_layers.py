#!/usr/bin/env python3
"""Tests of the order of the layers of Dockerfile (the web image) and Dockerfile.server (run by ./run_tests.sh --fast and by the CI).

The images are built on the host from a clone, and a release that changes only pages, the version, tools or documents must not compile the WebAssembly game or the C++ server again, so that what the
compile reads comes first and what only a later step needs comes after it. There is no Docker daemon in a test, so these tests read the files:

  - the web image: Emscripten's libraries and ports are made in a stage of their own that copies one small program and nothing else; the compile layer's COPY set is the build files and the sources and
    holds no VERSION, no web/, no tools/, no data, no changelog; VERSION, web/shell.html and Original-Ants/ come after the compile and before the link that reads them; the pages, the stamps, the build id
    and the site label come after both; the last stage copies the same builder outputs as before
  - the server image: the same for VERSION; tools/ is not copied (CMake is told not to look for it); the build id is the last step
  - both: a cache mount for ccache on every compile, the BuildKit syntax line, the stub version that the later layer replaces, and the check that stops a build whose header did not change
  - CMakeLists.txt: -DANTS_BUILD_TOOLS=OFF configures a tree that has no tools/ folder (cmake is run for real, configure only), the default needs the folder, and a changed VERSION is a new header
  - the flags of the pre-warming program are those of the real link (the ports and the options of the two Emscripten programs)
  - the final stages are what they were: the builder outputs they copy, the user, the ports, the health check and the command
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import web_without_docker as wd  # noqa: E402


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


class Stages:
    """The steps of a Dockerfile by stage name (tools/web_without_docker.py reads the file; a RUN's cache mount is dropped there, so the raw text is kept for it)."""

    def __init__(self, name):
        self.text = read(name)
        self.steps = wd.parse_dockerfile(self.text)
        self.names = {}
        for step in self.steps:
            if step.op == "FROM":
                self.names[step.stage] = step.words[2] if len(step.words) >= 3 and step.words[1].upper() == "AS" else str(step.stage)

    def stage(self, name):
        return [s for s in self.steps if self.names[s.stage] == name]

    @staticmethod
    def copies(steps):
        """[(sources, destination)] of the COPY steps that read the build context."""
        return [(s.words[:-1], s.words[-1]) for s in steps if s.op == "COPY" and "from" not in s.flags]

    @staticmethod
    def index(steps, predicate, what, after=-1):
        for i, s in enumerate(steps):
            if i > after and predicate(s):
                return i
        raise AssertionError("no step: " + what)


def raw_runs(text):
    """The text of every RUN of a Dockerfile as written, continued lines joined, comment lines dropped (the cache mount flag is still in it)."""
    return [t for _, t in wd.logical_lines(text) if re.match(r"RUN\s", t)]


def sources_before(steps, stop):
    found = []
    for s in steps[:stop]:
        if s.op == "COPY" and "from" not in s.flags:
            found += s.words[:-1]
    return found


class TheWebImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.docker = Stages("Dockerfile")
        cls.warm = cls.docker.stage("emsdk-warm")
        cls.builder = cls.docker.stage("builder")
        cls.runner = cls.docker.stage("runner")

    def build(self, text):
        return text.count("cmake --build build_web")

    def compile_index(self, nth=0):
        hits = [i for i, s in enumerate(self.builder) if s.op == "RUN" and "cmake --build build_web" in s.text]
        self.assertEqual(len(hits), 2, "one compile with the stubs and one link with the real files")
        return hits[nth]

    def test_the_stages_are_the_warm_one_the_builder_and_the_runner_in_that_order(self):
        self.assertEqual([self.docker.names[i] for i in sorted(self.docker.names)], ["emsdk-warm", "builder", "runner"])
        self.assertEqual(self.builder[0].words[0], "emsdk-warm", "the builder starts from the stage that has Emscripten's libraries")
        self.assertRegex(self.warm[0].words[0], r"^emscripten/emsdk:\d+\.\d+\.\d+$")
        self.assertEqual(self.runner[0].words[0], "nginx:alpine")

    def test_the_warm_stage_depends_on_the_version_and_one_program_only(self):
        self.assertEqual(self.docker.copies(self.warm), [(["docker/emsdk_warm.cpp"], "/warm/emsdk_warm.cpp")])
        self.assertEqual([s.op for s in self.warm], ["FROM", "RUN", "COPY", "RUN"])
        self.assertNotIn("ARG", [s.op for s in self.warm], "an ARG here would make the layers depend on a build argument")

    def test_the_warm_stage_builds_both_flavours_and_checks_that_something_came_out(self):
        run = self.warm[-1].text
        self.assertEqual(run.count("em++ "), 2)
        self.assertIn("-DWARM_CORE", run)
        self.assertIn("test -s warm_game.wasm", run)
        self.assertIn("test -s warm_core.wasm", run)
        self.assertTrue(os.path.isfile(os.path.join(REPO, "docker", "emsdk_warm.cpp")))

    def test_the_compile_reads_only_the_build_files_and_the_sources(self):
        compile_at = self.compile_index(0)
        self.assertEqual(sources_before(self.builder, compile_at), ["CMakeLists.txt", "cmake/", "include/", "src/"])
        for step in self.builder[:compile_at + 1]:
            if step.op == "COPY":
                for forbidden in ("VERSION", "web", "tools", "Original-Ants", "CHANGELOG", "asset_catalog", "docs", "third_party", "Community-Maps", "docker"):
                    for source in step.words[:-1]:
                        self.assertFalse(source.startswith(forbidden), "%s is copied before the compile" % source)

    def test_the_stubs_are_made_after_the_sources_and_before_the_compile_and_hold_what_the_link_needs(self):
        stubs = self.docker.index(self.builder, lambda s: s.op == "RUN" and "echo 0.0.0 > VERSION" in s.text, "the stubs")
        self.assertLess(self.docker.index(self.builder, lambda s: s.op == "COPY" and s.words[0] == "src/", "COPY src/"), stubs)
        self.assertLess(stubs, self.compile_index(0))
        text = self.builder[stubs].text
        self.assertIn("web/shell.html", text)
        self.assertIn("{{{ SCRIPT }}}", text)                          # (emcc stops without it in the template)
        self.assertIn("Original-Ants", text)
        self.assertRegex("0.0.0", r"^[0-9]+\.[0-9]+\.[0-9]+$")          # (the first line of VERSION must be MAJOR.MINOR.PATCH: CMakeLists.txt stops otherwise)

    def test_version_the_template_and_the_data_come_after_the_compile_and_before_the_link_that_reads_them(self):
        first, second = self.compile_index(0), self.compile_index(1)
        for source, destination in (("VERSION", "./VERSION"), ("web/shell.html", "./web/shell.html"), ("Original-Ants/", "./Original-Ants/")):
            at = self.docker.index(self.builder, lambda s: s.op == "COPY" and s.words == [source, destination], "COPY " + source)
            self.assertTrue(first < at < second, "COPY %s is between the compile and the link" % source)
        removal = self.docker.index(self.builder, lambda s: s.op == "RUN" and s.text.startswith("rm -rf VERSION web Original-Ants"), "the stubs are removed", after=first)
        self.assertLess(removal, self.docker.index(self.builder, lambda s: s.op == "COPY" and s.words[0] == "VERSION", "COPY VERSION", after=first), "the stubs go before the real files come (a COPY of a folder merges)")

    def test_the_link_is_made_again_with_the_outputs_removed_and_the_version_checked(self):
        text = self.builder[self.compile_index(1)].text
        removal = text[text.index("rm -f "):text.index("emcmake cmake")]
        for output in ("index.html", "index.js", "index.wasm", "index.data"):
            self.assertIn("build_web/src/ants_app/" + output, removal)
        self.assertLess(text.index("rm -f "), text.index("cmake --build"))
        self.assertLess(text.index("emcmake cmake -B build_web"), text.index("grep -q"))
        self.assertLess(text.index("grep -q"), text.index("cmake --build"), "the header is checked before the build, so that a stale header stops the image")
        self.assertIn("build_web/generated/ants_app/version.hpp", text)
        self.assertIn("test -s build_web/src/ants_app/index.wasm", text)

    def test_the_pages_the_stamps_the_build_id_and_the_rest_come_after_the_link(self):
        link = self.compile_index(1)
        late = self.builder[link + 1:]
        for what, predicate in (("gzip", lambda s: s.op == "RUN" and "gzip -9 -n -k" in s.text),
                                ("COPY web/", lambda s: s.op == "COPY" and s.words == ["web/", "./web/"]),
                                ("the stamps", lambda s: s.op == "RUN" and "@@BUILD_TIMESTAMP@@" in s.text),
                                ("the cache buster", lambda s: s.op == "RUN" and "@@DATA_SIZE@@" in s.text),
                                ("ARG ANTS_BUILD_ID", lambda s: s.op == "ARG" and s.text.startswith("ANTS_BUILD_ID")),
                                ("ARG ANTS_SITE_LABEL", lambda s: s.op == "ARG" and s.text.startswith("ANTS_SITE_LABEL")),
                                ("COPY .git*", lambda s: s.op == "COPY" and s.words[0] == "VERSION" and s.words[1] == ".git*"),
                                ("the build id", lambda s: s.op == "RUN" and "resolve_build_id.sh" in s.text),
                                ("the site label", lambda s: s.op == "RUN" and "ANTS_SITE_LABEL" in s.text and "SITE_TITLE" in s.text),
                                ("the changelog", lambda s: s.op == "COPY" and s.words[0] == "CHANGELOG.md"),
                                ("the atlas", lambda s: s.op == "RUN" and "map_atlas.py" in s.text)):
            self.docker.index(late, predicate, what + " is after the link")
        order = [self.docker.index(self.builder, p, w, after=link) for w, p in (
            ("gzip", lambda s: s.op == "RUN" and "gzip -9 -n -k" in s.text),
            ("COPY web/", lambda s: s.op == "COPY" and s.words == ["web/", "./web/"]),
            ("the stamps", lambda s: s.op == "RUN" and "@@BUILD_TIMESTAMP@@" in s.text),
            ("the cache buster", lambda s: s.op == "RUN" and "@@DATA_SIZE@@" in s.text),
            ("ARG ANTS_BUILD_ID", lambda s: s.op == "ARG" and s.text.startswith("ANTS_BUILD_ID")),
            ("the build id", lambda s: s.op == "RUN" and "resolve_build_id.sh" in s.text),
            ("the site label", lambda s: s.op == "RUN" and "SITE_TITLE" in s.text and "ANTS_SITE_LABEL" in s.text),
            ("the changelog", lambda s: s.op == "COPY" and s.words[0] == "CHANGELOG.md"),
            ("the atlas", lambda s: s.op == "RUN" and "map_atlas.py" in s.text))]
        self.assertEqual(order, sorted(order))

    def test_no_build_argument_and_no_page_is_known_before_the_link(self):
        link = self.compile_index(1)
        for step in self.docker.steps[:self.docker.steps.index(self.builder[link])]:
            self.assertNotEqual(step.op, "ARG", "an ARG before the link would put the build argument in the compile layers")
        for step in self.builder[:link]:
            self.assertNotIn("ANTS_BUILD_ID", step.text)
            self.assertNotIn("BUILD_TIME", step.text)
            self.assertNotIn("@@", step.text)
        self.assertEqual([w for step in self.builder[:link] if step.op == "COPY" for w in step.words[:-1] if w.startswith("web")], ["web/shell.html"], "the one page that the link reads")

    def test_the_stamps_go_into_the_game_page_that_the_link_made_not_into_the_template(self):
        stamp = [s for s in self.builder if s.op == "RUN" and "@@BUILD_TIMESTAMP@@" in s.text][0].text
        self.assertIn("build_web/src/ants_app/index.html web/lobby.html web/matches.html web/match.html web/live.html", stamp)
        self.assertNotIn("web/shell.html", stamp)
        self.assertIn("@@GAME_VERSION@@", stamp)
        self.assertIn("MAPMAKER_VERSION", stamp)

    def test_every_compile_has_the_ccache_mount_and_the_wrapper(self):
        runs = [r for r in raw_runs(self.docker.text) if "cmake --build build_web" in r]
        self.assertEqual(len(runs), 2)
        for run in runs:
            self.assertTrue(run.startswith("RUN --mount=type=cache,target=/root/.ccache,id=ants-ccache-web "), run[:80])
            self.assertIn("EM_COMPILER_WRAPPER=ccache", run)
            self.assertIn("command -v ccache", run, "the build must work where ccache is not installed (the tool that replays the Dockerfile on a machine)")
        self.assertIn("CCACHE_DIR=/root/.ccache", self.docker.text)
        self.assertTrue(self.docker.text.startswith("# syntax=docker/dockerfile:1\n"))
        self.assertIn("ccache", self.warm[1].text, "ccache is installed in the stage that is reused")

    def test_the_runner_stage_copies_the_same_builder_outputs_and_runs_as_before(self):
        outputs = sorted(w for s in self.runner if s.op == "COPY" and "from" in s.flags for w in s.words[:-1])
        self.assertEqual(outputs, sorted([
            "/src/build_web/src/ants_app/index.*", "/src/build_web/lvl_core/lvl_core.js", "/src/build_web/lvl_core/lvl_core.wasm", "/src/lobby.html", "/src/matches.html", "/src/match.html", "/src/live.html",
            "/src/mapmaker.html", "/src/mapmaker_atlas/", "/src/changelog/changelog.html"]))
        text = self.docker.text[self.docker.text.index("FROM nginx:alpine AS runner"):]
        self.assertEqual(len(re.findall(r"(?m)^USER ", text)), 0)
        self.assertEqual(re.findall(r"(?m)^EXPOSE .*$", text), ["EXPOSE 80"])
        self.assertEqual(re.findall(r"(?m)^CMD .*$", text), ['CMD ["nginx", "-g", "daemon off;"]'])
        self.assertIn("CMD wget -q -O /dev/null http://127.0.0.1/ || exit 1", text)
        self.assertEqual(len(re.findall(r"(?m)^ENTRYPOINT ", text)), 0)
        self.assertEqual([s.op for s in self.runner if s.op in ("ARG", "ENV")], [], "the runner takes no build argument")


class TheServerImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.docker = Stages("Dockerfile.server")
        cls.builder = cls.docker.stage("builder")
        cls.runner = cls.docker.stage("runner")

    def compiles(self):
        return [i for i, s in enumerate(self.builder) if s.op == "RUN" and "cmake --build build" in s.text and "resolve_build_id.sh" not in s.text]

    def test_the_stages_are_the_builder_and_the_runner(self):
        self.assertEqual([self.docker.names[i] for i in sorted(self.docker.names)], ["builder", "runner"])

    def test_the_first_compile_reads_the_build_files_and_the_sources_only(self):
        compiles = self.compiles()
        self.assertEqual(len(compiles), 2, "the compile with the stub version and the one with the real version")
        self.assertEqual(sources_before(self.builder, compiles[0]), ["CMakeLists.txt", "cmake/", "include/", "src/", "third_party/"])
        self.assertIn("-DANTS_BUILD_TOOLS=OFF", self.builder[compiles[0]].text)
        for step in self.docker.steps:
            if step.op == "COPY":
                for source in step.words[:-1]:
                    self.assertFalse(source.startswith("tools"), "tools/ is not copied: %s" % source)

    def test_the_version_comes_after_the_first_compile_and_before_the_build_id(self):
        first, second = self.compiles()
        stub = self.docker.index(self.builder, lambda s: s.op == "RUN" and s.text == "echo 0.0.0 > VERSION", "the stub version")
        real = self.docker.index(self.builder, lambda s: s.op == "COPY" and s.words == ["VERSION", "./VERSION"], "COPY VERSION")
        arg = self.docker.index(self.builder, lambda s: s.op == "ARG" and s.text.startswith("ANTS_BUILD_ID"), "ARG ANTS_BUILD_ID")
        gitinfo = self.docker.index(self.builder, lambda s: s.op == "COPY" and s.words[:2] == ["VERSION", ".git*"], "COPY VERSION .git*")
        stamp = self.docker.index(self.builder, lambda s: s.op == "RUN" and "resolve_build_id.sh" in s.text, "the build id")
        self.assertLess(stub, first)
        self.assertTrue(first < real < second < arg < gitinfo < stamp, (first, real, second, arg, gitinfo, stamp))
        self.assertIn("ANTS_BUILD_ID", self.builder[stamp].text)
        self.assertIn("--version", self.builder[stamp].text, "the image still runs ants_server --version")

    def test_the_second_compile_checks_that_the_header_has_the_real_version(self):
        text = self.builder[self.compiles()[1]].text
        self.assertLess(text.index("cmake -B build"), text.index("grep -q"))
        self.assertLess(text.index("grep -q"), text.index("cmake --build build"))
        self.assertIn("build/generated/ants_app/version.hpp", text)

    def test_the_compiles_have_the_cache_mount_and_ccache_is_installed(self):
        runs = [r for r in raw_runs(self.docker.text) if "cmake --build build" in r and "--target ants_server" in r]
        self.assertGreaterEqual(len(runs), 2)
        for run in runs[:2]:
            self.assertTrue(run.startswith("RUN --mount=type=cache,target=/root/.ccache,id=ants-ccache-server "), run[:80])
        self.assertIn("ccache", self.builder[0].text if self.builder[0].op == "RUN" else self.builder[1].text)
        self.assertIn("CCACHE_DIR=/root/.ccache", self.docker.text)
        self.assertTrue(self.docker.text.startswith("# syntax=docker/dockerfile:1\n"))
        web = read("Dockerfile")
        self.assertNotEqual(re.findall(r"id=(ants-ccache-\w+)", web)[0], re.findall(r"id=(ants-ccache-\w+)", self.docker.text)[0], "each image has its own cache")

    def test_the_runner_stage_is_what_it_was(self):
        self.assertEqual(sorted(w for s in self.runner if s.op == "COPY" for w in s.words[:-1]),
                         sorted(["Original-Ants/Maps/", "Community-Maps/*.lvl", "Community-Maps/*.LVL", "Community-Maps/maps.json", "Community-Maps/previews.json", "/src/build/src/ants_server/ants_server"]))
        text = self.docker.text[self.docker.text.index("FROM debian:12-slim AS runner"):]
        self.assertEqual(re.findall(r"(?m)^USER .*$", text), ["USER ants"])
        self.assertEqual(re.findall(r"(?m)^EXPOSE .*$", text), ["EXPOSE 4001 4002 4010 4020"])
        self.assertIn('exec 3<>/dev/tcp/127.0.0.1/4001', text)
        self.assertEqual(len(re.findall(r"(?m)^ENTRYPOINT \[", text)), 1)
        self.assertEqual(re.findall(r"(?m)^CMD .*$", text), ['CMD ["--port", "4001", "--ws-port", "4002", "--ctl-port", "4010"]'])


class TheFlagsOfThePreWarming(unittest.TestCase):
    """The program of the warm stage is built with the flags of the two real Emscripten programs: when CMake's change, the stage must follow (or it builds the wrong libraries and the compile makes them again)."""

    @classmethod
    def setUpClass(cls):
        text = read("Dockerfile")
        cls.warm = [t for _, t in wd.logical_lines(text) if t.startswith("RUN cd /warm")][0]
        cls.game, cls.core = [c.strip() for c in re.split(r"\s&&\s", cls.warm) if c.strip().startswith("em++")]
        cls.app_flags = re.search(r'LINK_FLAGS "([^"]*)"', read("src", "ants_app", "CMakeLists.txt")).group(1)
        cls.core_flags = re.search(r'LINK_FLAGS "([^"]*)"', read("CMakeLists.txt")).group(1)

    def test_the_ports_are_the_ones_of_the_game(self):
        for flag in re.findall(r"-sUSE_\w+=\d", self.app_flags):
            self.assertIn(flag, self.game)
        self.assertEqual(sorted(re.findall(r"-sUSE_\w+=\d", self.game)), sorted(re.findall(r"-sUSE_\w+=\d", self.app_flags)))
        self.assertNotIn("-sUSE_", self.core, "the map core uses no port")

    def test_the_options_that_change_the_libraries_are_the_ones_of_the_game(self):
        for flag in re.findall(r"-s(?:ALLOW_MEMORY_GROWTH|WASM|FORCE_FILESYSTEM|INITIAL_MEMORY|MAXIMUM_MEMORY)=\d+", self.app_flags):
            self.assertIn(flag, self.game)
        self.assertIn("-O3 -DNDEBUG", self.game, "a Release build of CMake")
        self.assertNotIn("-flto", self.game)
        self.assertNotIn("-fwasm-exceptions", self.game)
        self.assertNotIn("-flto", self.app_flags)

    def test_the_map_core_flavour_has_the_compile_and_link_options_of_the_core(self):
        for flag in ("-Oz", "-flto", "-fwasm-exceptions", "--no-entry", "-sMODULARIZE=1", "-sFILESYSTEM=0", "-sMALLOC=emmalloc", "-sENVIRONMENT=web,worker,node"):
            self.assertIn(flag, self.core_flags, "CMakeLists.txt no longer has " + flag)
            self.assertIn(flag, self.core)

    def test_the_program_has_both_flavours(self):
        text = read("docker", "emsdk_warm.cpp")
        self.assertIn("#ifdef WARM_CORE", text)
        self.assertIn("warm_core", text)
        self.assertIn("throw", text, "the wasm exceptions' libraries are needed only when something throws")
        self.assertIn("TTF_", text)
        self.assertIn("SDL_OpenAudioDevice", text)


CMAKE = shutil.which("cmake")


@unittest.skipUnless(CMAKE, "cmake is not installed")
class TheTreeOfTheServerImage(unittest.TestCase):
    """CMake itself, configure only: the server image's build files and sources are enough with -DANTS_BUILD_TOOLS=OFF, a changed VERSION is a new header, and the option has its limit."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="ants_layers_")
        cls.tree = os.path.join(cls.tmp, "src")
        for name in ("CMakeLists.txt", "cmake", "include", "src", "third_party"):
            path = os.path.join(REPO, name)
            if os.path.isdir(path):
                shutil.copytree(path, os.path.join(cls.tree, name), ignore=shutil.ignore_patterns("*.md"))
            else:
                os.makedirs(cls.tree, exist_ok=True)
                shutil.copy(path, cls.tree)
        write(os.path.join(cls.tree, "VERSION"), "0.0.0\n")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def configure(self, folder, *options, tree=None):
        return subprocess.run([CMAKE, "-S", tree or self.tree, "-B", os.path.join(self.tmp, folder), "-DCMAKE_BUILD_TYPE=Release", "-DANTS_BUILD_APP=OFF", "-DBUILD_TESTS=OFF", *options], capture_output=True, text=True, timeout=300)

    def test_the_tree_of_the_image_configures_without_tools_and_follows_the_version(self):
        self.assertFalse(os.path.exists(os.path.join(self.tree, "tools")))
        first = self.configure("off", "-DANTS_BUILD_TOOLS=OFF")
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        header = os.path.join(self.tmp, "off", "generated", "ants_app", "version.hpp")
        with open(header, encoding="utf-8") as f:
            self.assertIn('VERSION_STRING = "v0.0.0"', f.read())
        write(os.path.join(self.tree, "VERSION"), "12.34.56\n")
        second = subprocess.run([CMAKE, "-B", os.path.join(self.tmp, "off")], cwd=self.tree, capture_output=True, text=True, timeout=300)
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        with open(header, encoding="utf-8") as f:
            self.assertIn('VERSION_STRING = "v12.34.56"', f.read())
        with open(os.path.join(self.tmp, "off", "CMakeCache.txt"), encoding="utf-8") as f:
            self.assertIn("ANTS_BUILD_TOOLS:BOOL=OFF", f.read(), "the option stays in the cache, so that the later configure of the image keeps it")

    def test_without_the_option_the_folder_of_the_tools_is_needed(self):
        result = self.configure("on")
        self.assertNotEqual(result.returncode, 0, "the default still defines the programs of tools/ (and CMake needs their sources)")
        self.assertIn("tools/", result.stdout + result.stderr)

    def test_the_tools_cannot_be_left_out_of_a_build_with_tests(self):
        result = self.configure("tests", "-DANTS_BUILD_TOOLS=OFF", "-DBUILD_TESTS=ON")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ANTS_BUILD_TOOLS=OFF needs BUILD_TESTS=OFF", result.stdout + result.stderr)

    def test_the_option_is_on_by_default(self):
        self.assertIn('option(ANTS_BUILD_TOOLS "', read("CMakeLists.txt"))
        self.assertRegex(read("CMakeLists.txt"), r'option\(ANTS_BUILD_TOOLS "[^"]*" ON\)')


if __name__ == "__main__":
    unittest.main()
