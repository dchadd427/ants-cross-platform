#!/usr/bin/env python3
"""The CI workflow (.github/workflows/ci.yml) keeps its promises (run by ./run_tests.sh --fast and by the CI).

main is protected: a change merges only through a pull request whose five checks pass, and the checks are named by their JOB NAMES. A renamed or split job would make every
pull request unmergeable. Read as text (no YAML library is needed; with PyYAML the structure
is checked too):

  - the five required job names (each is a small job: linux, macos, windows_2022, windows_2026, web), MSVC 2022 for pull requests, pushes to main and manual runs and MSVC 2026 always, the
    triggers (no push to other branches; a pull request is also judged again when it is edited)
  - the small jobs behind the five names: each passes only when the job `changes` succeeded and every job behind it passed, or (macOS, the two Windows names) when `changes` said the change needs
    none of them and they were skipped; they are RUN here in bash against every combination of results (a failed, cancelled or skipped job, a failed `changes`, a light answer that is not true)
  - the legs: ctest in three parts that are every test once, the tool self-tests and script suites, the python tests in slices that are every test once; macOS: the system-dependent python tests;
    the web job in two legs (the web image; the server and hub images and the stack files) behind the name
  - the image of the Linux jobs: Ubuntu 24.04 for the legs and for the small job behind "Linux (GCC)" (so that the compiler under the required check changes only with the workflow)
  - the light checks of a pull request that changes pages, documents and page tests only: what skips (the macOS and Windows legs, at job level; their small jobs report the names), what does
    not (Linux, the web legs, a push, a manual run), and that no text of a pull request reaches a script
  - CI does not deploy: no deploy job, no secret and no repository variable in the workflow, and no concurrency group of a job (only the workflow's own)
  - the same suites as ./run_tests.sh: every test program of its table is registered with ctest (which Linux, macOS and Windows run), its other suites (tool self-tests, script
    suites, E2E runner, repository checks) are steps of the legs of the Linux and macOS test jobs, and ctest has no test that the table lacks
  - the players' maps: the Linux and macOS step that sweeps Community-Maps and runs `tools/community_maps.py verify` (and suite 2.18.1 of the runner) is run here against made-up
    programs: a sweep or a check that fails fails the step or the suite, every finding of a failed sweep is shown, and the other tools of the step still run
  - every action is a first-party `actions/` one at a major version or a commit-pinned one; the sccache download is pinned and checked by its SHA-256
  - with PyYAML, the workflow is read as a document and each leg of the test jobs is RUN in the head, as GitHub would (the conditions of its steps are evaluated with every step succeeding): no
    leg can pass without running a test, no condition names a part or a step that is not there, every test step is run by the legs of one part, every name of a check and of a log
    artifact exists once, the five required names are each reported once, the two small jobs need every job of their system, and every ctest has --no-tests=error. A copy of the
    workflow with each of these breakages is judged too (TheChecksCanFail), and tests/data/ci_workflow_mutants.json breaks the real file the same ways for tools/mutate.py
"""
import glob
import itertools
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WORKFLOW = os.path.join(REPO, ".github", "workflows", "ci.yml")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


TEXT = read(WORKFLOW)
LINES = TEXT.splitlines()


def job_block(job_id):
    """The lines of one job (from `  job_id:` to the next job or the end)."""
    start = next(i for i, l in enumerate(LINES) if l == "  %s:" % job_id)
    end = next((i for i in range(start + 1, len(LINES)) if re.match(r"^  [a-z0-9_-]+:\s*$", LINES[i])), len(LINES))
    return LINES[start:end]


def job_ids():
    jobs_at = LINES.index("jobs:")
    return [m.group(1) for l in LINES[jobs_at + 1:] for m in [re.match(r"^  ([a-z0-9_-]+):\s*$", l)] if m]


class RequiredChecks(unittest.TestCase):
    def test_the_five_required_job_names(self):
        names = {}
        for job in job_ids():
            names[job] = next(re.match(r"^    name: (.*)$", l).group(1) for l in job_block(job) if re.match(r"^    name: ", l))
        self.assertEqual(names["linux"], "Linux (GCC)")
        self.assertEqual(names["macos"], "macOS (Apple clang)")
        self.assertEqual(names["windows_2022"], "Windows (MSVC 2022)")
        self.assertEqual(names["windows_2026"], "Windows (MSVC 2026)")
        self.assertEqual(names["web"], "Web (Emscripten, Docker image)")
        self.assertEqual(names["windows_tests"], "Windows tests (${{ matrix.name }})")                  # (the legs do not carry a required name)

    def test_both_msvc_compilers_run_for_every_event_so_that_neither_name_passes_without_its_compiler(self):
        block = "\n".join(job_block("windows_tests"))
        self.assertNotIn("fromJSON", block)
        self.assertIn("        include:\n          - name: MSVC 2022\n            os: windows-2022\n          - name: MSVC 2026\n            os: windows-latest\n", block)
        self.assertEqual(len(re.findall(r"(?m)^          - name: ", block)), 2)

    def test_every_job_has_a_time_limit_and_the_matrices_do_not_stop_at_the_first_failure(self):
        for job in job_ids():
            self.assertTrue(any(re.match(r"^    timeout-minutes: \d+$", l) for l in job_block(job)), job)
        for job in ("windows_tests", "linux_tests", "macos_tests"):
            self.assertTrue(any(l.strip() == "fail-fast: false" for l in job_block(job)), job)

    def test_triggers(self):
        head = "\n".join(LINES[:LINES.index("jobs:")])
        self.assertRegex(head, r"(?m)^on:\n  pull_request:\n(?:    #.*\n)*    types: \[opened, synchronize, reopened, edited\]\n  push:\n    branches: \[main\]\n  workflow_dispatch:\n")
        self.assertNotRegex(head, r"(?m)^  push:\s*$\n  pull_request")                   # no bare `push:` (every branch)
        self.assertIn("cancel-in-progress: true", head)


class NoDeploy(unittest.TestCase):
    """CI does not deploy: the site follows main through the host's own automatic update (docs/WORKFLOW.md)."""

    def test_there_is_no_deploy_job_no_secret_and_no_repository_variable(self):
        self.assertNotIn("deploy", job_ids())
        self.assertEqual(job_ids()[-1], "web")
        code = "\n".join(l for l in LINES if not l.lstrip().startswith("#"))
        self.assertNotIn("secrets.", code)
        self.assertNotIn("vars.", code)
        self.assertNotIn("webhook", code.lower())
        self.assertNotIn("deploy", code.lower())

    def test_only_the_workflow_has_a_concurrency_group_and_it_is_the_runs_own(self):
        for job in job_ids():
            self.assertFalse(any(l == "    concurrency:" for l in job_block(job)), job)
        workflow_group = re.search(r"(?m)^concurrency:\n  group: (.+)\n", TEXT).group(1)
        self.assertTrue(workflow_group.startswith("ci-"))
        # one run per pull request (two pull requests of one branch, into two bases, must not cancel each other), per ref for a push or a manual run, and a newer one cancels the older
        self.assertEqual(workflow_group, "ci-${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}")
        self.assertIn("  cancel-in-progress: true\n", TEXT)


class SameSuitesAsTheRunner(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = read(os.path.join(REPO, "run_tests.sh"))
        cls.registered = set()
        for path in glob.glob(os.path.join(REPO, "tests", "*", "CMakeLists.txt")):
            if os.path.basename(os.path.dirname(path)) != "e2e":                           # (the E2E runner is a project of its own: its own build and step)
                cls.registered |= set(re.findall(r"add_test\(NAME (\w+)", read(path)))
        cls.table = re.findall(r'^\s*suite "([^"]+)"\s+\w+\s+[01]\s+"([^"]*)"\s+"[^"]*"\s+"[^"]*"\s+(.*)$', cls.runner, re.M)

    def programs_of_the_table(self):
        return set(re.findall(r'\./\$BUILD_DIR/tests/\w+/(\w+)"', self.runner))        # (the worker bot suite is a function of the script: it names its program there)

    def test_every_test_program_of_the_runner_is_registered_with_ctest_and_the_other_way_round(self):
        programs = self.programs_of_the_table()
        self.assertGreater(len(programs), 40)
        self.assertEqual(programs - self.registered, set(), "run_tests.sh runs these test programs, but ctest (the CI) does not know them")
        self.assertEqual(self.registered - programs, set(), "ctest (the CI) runs these, but run_tests.sh does not")

    def test_the_other_suites_of_the_runner_are_steps_of_the_linux_and_macos_test_jobs(self):
        for job in ("linux_tests", "macos_tests"):
            block = "\n".join(job_block(job))
            for needle in ("build/map_sweep --selftest", "build/map_sweep Community-Maps --ticks 600", "python3 tools/community_maps.py verify Community-Maps", "build/bot_arena --selftest", "build/replay_tool --selftest", "build/lvl_tool --selftest", "e2e_runner --all", "bash tests/scripts/test_start_game.sh",
                           "tests/scripts/test_ants_server.sh --list-parts", "tests/scripts/test_ants_server.sh --part", "python3 tools/check_version_consistency.py",
                           "python3 tests/scripts/run_python_tests.py", "ctest --test-dir build"):
                self.assertIn(needle, block, "%s: no step runs `%s`" % (job, needle))
        windows = "\n".join(job_block("windows_tests"))
        for needle in ("ctest --test-dir build", "map_sweep.exe --selftest", "bot_arena.exe --selftest", "replay_tool.exe --selftest", "lvl_tool.exe --selftest", "e2e_runner.exe --all"):
            self.assertIn(needle, windows)

    def test_every_optional_tool_of_cmake_is_built_by_the_three_build_jobs_and_the_image_check_lists_the_map_makers_files(self):
        tools = re.findall(r"add_executable\((\w+) EXCLUDE_FROM_ALL tools/", read(os.path.join(REPO, "CMakeLists.txt")))
        self.assertGreaterEqual(len(tools), 6)
        for job in ("linux_tests", "macos_tests", "windows_tests"):
            lines = [l for l in job_block(job) if "--target" in l]
            self.assertEqual(len(lines), 1, job)
            self.assertEqual(sorted(lines[0].split("--target", 1)[1].replace("-- -k 0", "").split()), sorted(tools), "%s builds the optional tools that CMake has" % job)
        web = "\n".join(job_block("web_image"))
        for name in sorted(os.path.basename(f) for f in glob.glob(os.path.join(REPO, "web", "front", "mapmaker_*.js"))):
            self.assertTrue(("front/" + name) in web, "the image check does not list the map maker's script " + name)

    def test_the_commands_of_the_runners_script_and_python_suites_are_what_the_ci_runs(self):
        self.assertIn("python3 tests/scripts/run_python_tests.py", self.runner)
        self.assertIn("python3 tools/check_version_consistency.py", self.runner)
        self.assertIn("test_start_game.sh", self.runner)
        self.assertIn("map_sweep\" --selftest", self.runner)
        self.assertIn("map_sweep\" Community-Maps", self.runner)
        self.assertIn("python3 tools/community_maps.py verify Community-Maps", self.runner)
        self.assertIn("bot_arena\" --selftest", self.runner)
        self.assertIn("replay_tool\" --selftest", self.runner)
        self.assertIn("lvl_tool\" --selftest", self.runner)
        self.assertIn("e2e_runner", self.runner)

    def test_the_web_legs_build_the_three_images_and_check_the_stack_file(self):
        image, stack = "\n".join(job_block("web_image")), "\n".join(job_block("web_stack"))
        block = image + "\n" + stack
        for needle in (" -t ants-beta ", " -f Dockerfile.server -t ants-server ", " -f Dockerfile.hub -t ants-hub .", "nginx -t", "docker compose -f docker-compose.stack.yml config --quiet", ):
            self.assertIn(needle, block)
        self.assertNotIn("--target", block)          # (the images are built whole: the server is the last stage of Dockerfile.server, see the next test)
        # the legs split the work and neither does the other's
        for needle in ("-t ants-beta ", "nginx -t", "web_routes_check.py", "lvl_core_check.js"):
            self.assertIn(needle, image)
            self.assertNotIn(needle, stack)
        for needle in ("Dockerfile.server", "Dockerfile.hub", "docker compose"):
            self.assertIn(needle, stack)
            self.assertNotIn(needle, image)
        self.assertEqual([l for l in job_block("web_image") + job_block("web_stack") if l.startswith("    needs:")], [], "the two legs start together: neither waits for the other")

    def test_the_images_are_built_by_buildx_with_a_layer_cache_in_a_folder_and_built_whole(self):
        """The cache changes the time and not the image: the same Dockerfile and context, `docker buildx build --load` into the local daemon (so that every later step runs the same image), the cache in a
        folder that the first-party actions/cache keeps (no other action), written to a new folder and swapped in (so that it does not grow), and keyed by the files that the Dockerfile copies."""
        for job, folder, dockerfile, key_files in (("web_image", "web", "", ("'Dockerfile'", "'web/**'", "'src/**'", "'CMakeLists.txt'", "'VERSION'", "'cmake/**'", "'include/**'", "'Original-Ants/**'")),
                                                   ("web_stack", "server", "-f Dockerfile.server ", ("'Dockerfile.server'", "'src/**'", "'tools/**'", "'third_party/**'", "'CMakeLists.txt'", "'VERSION'", "'cmake/**'", "'include/**'"))):
            block = "\n".join(job_block(job))
            self.assertIn("docker buildx create --name ants-builder --driver docker-container --driver-opt image=moby/buildkit:v0.19.0 --use", block, job)      # (a release of the builder, not a moving tag)
            self.assertIn("uses: actions/cache@v6", block, job)
            self.assertIn("path: /tmp/docker-cache/%s\n" % folder, block, job)
            self.assertIn("restore-keys: docker-%s-\n" % folder, block, job)
            key = next(l for l in job_block(job) if l.strip().startswith("key: docker-%s-" % folder))
            for needle in key_files:
                self.assertIn(needle, key, job)
            self.assertIn("--cache-from type=local,src=/tmp/docker-cache/%s" % folder, block, job)
            self.assertIn("--cache-to type=local,dest=/tmp/docker-cache/%s-new,mode=max" % folder, block, job)
            self.assertIn("rm -rf /tmp/docker-cache/%s && mv /tmp/docker-cache/%s-new /tmp/docker-cache/%s" % (folder, folder, folder), block, job)
            for line in job_block(job):
                if "docker buildx build" in line:
                    self.assertIn("--load", line, "an image that is not loaded cannot be run by the next step: " + line)
                    self.assertNotIn("--push", line)
                    self.assertNotIn("--platform", line, "the image is the runner's own")
            self.assertNotIn("docker build ", block, "every build of the leg goes through the builder with --load")
            self.assertNotIn("docker/build-push-action", block)
            self.assertNotIn("docker/setup-buildx-action", block)

    def test_the_server_is_the_last_stage_of_its_dockerfile_because_the_ci_and_the_stacks_build_it_without_a_target(self):
        stages = re.findall(r"(?m)^FROM\s+\S+\s+AS\s+(\S+)\s*$", read(os.path.join(REPO, "Dockerfile.server")))
        self.assertEqual(stages[-1], "runner")
        for name in ("docker-compose.stack.yml",):
            code = "\n".join(l for l in read(os.path.join(REPO, name)).splitlines() if not l.lstrip().startswith("#"))
            self.assertNotIn("target:", code, name)


def step_of(job, needle):
    """(the name, the shell script) of the step of `job` whose text holds `needle`; the script is what `run: |` holds, as GitHub writes it to a file."""
    block = job_block(job)
    starts = [i for i, l in enumerate(block) if re.match(r"^      - name: ", l)] + [len(block)]
    for first, end in zip(starts, starts[1:]):
        step = block[first:end]
        if any(needle in l for l in step):
            run_at = next(i for i, l in enumerate(step) if re.match(r"^        run: \|\s*$", l))
            return step[0].split("name: ", 1)[1], "\n".join(l[10:] for l in step[run_at + 1:]).rstrip() + "\n"
    raise AssertionError("%s: no step holds `%s`" % (job, needle))


def function_of(script, name):
    """The text of a shell function of a script (from `name() {` to the closing brace at the start of a line)."""
    start = script.index("\n%s() {\n" % name) + 1
    return script[start:script.index("\n}\n", start) + 3]


def sweep_log(findings):
    """What `map_sweep` prints for a folder: a line of progress for each finding, a table with a row for each play, the findings, the summary."""
    lines = ["[%d/600] finding: Map %d.lvl" % (i + 1, i) for i in range(findings)]
    lines += ["map                 bytes    w x h name loads team"] + ["Table row %d.lvl      19000    31x31 ok   yes   R15" % i for i in range(700)]
    if findings:
        lines += ["", "Findings"] + ["  Map %d.lvl: does not load: the file ends inside the tile dictionary" % i for i in range(findings)]
    lines += ["", "Summary"] + ["  summary line %d" % i for i in range(25)]
    return "\n".join(lines) + "\n"


@unittest.skipUnless(shutil.which("bash"), "bash is needed")
class TheCommunityMapsFailTheirStep(unittest.TestCase):
    """The step of the Linux and macOS jobs and suite 2.18.1 of ./run_tests.sh are run against made-up programs: a sweep or a check that fails fails them, and the other tools still run."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = tmp.name
        for folder in ("build", "tools", "bin", "bd"):
            os.makedirs(os.path.join(self.root, folder))
        self.log = os.path.join(self.root, "fake_sweep.log")
        os.symlink(sys.executable, os.path.join(self.root, "bin", "python3"))              # (the steps say python3)
        sweep = ("#!/bin/sh\necho \"map_sweep $*\" >> ran.txt\n"
                 "if [ \"$1\" = \"--selftest\" ]; then exit \"${FAKE_SELFTEST_EXIT:-0}\"; fi\n"
                 "cat \"$FAKE_SWEEP_LOG\"\nexit \"${FAKE_SWEEP_EXIT:-0}\"\n")
        tool = "#!/bin/sh\necho \"%s $*\" >> ran.txt\nexit \"${FAKE_TOOL_EXIT:-0}\"\n"
        for folder in ("build", "bd"):
            self.write_program("%s/map_sweep" % folder, sweep)
        self.write_program("build/bot_arena", tool % "bot_arena")
        self.write_program("build/replay_tool", tool % "replay_tool")
        self.write_program("build/lvl_tool", tool % "lvl_tool")
        self.write_program("tools/community_maps.py", "import os, sys\nopen('ran.txt', 'a').write('community_maps ' + ' '.join(sys.argv[1:]) + '\\n')\nsys.exit(int(os.environ.get('FAKE_VERIFY_EXIT', '0')))\n")

    def write_program(self, path, text):
        full = os.path.join(self.root, path)
        with open(full, "w", encoding="utf-8") as f:
            f.write(text)
        os.chmod(full, 0o755)

    def run_script(self, script, **env):
        """(exit status, output, the lines of ran.txt) of a script run the way GitHub runs one (`bash -e FILE`) in the folder of the made-up programs."""
        path = os.path.join(self.root, "script.sh")
        with open(path, "w", encoding="utf-8") as f:
            f.write(script)
        env = dict(os.environ, PATH=os.path.join(self.root, "bin") + os.pathsep + os.environ["PATH"], FAKE_SWEEP_LOG=self.log, **env)
        done = subprocess.run(["bash", "-e", path], cwd=self.root, env=env, capture_output=True, text=True, timeout=120)
        ran, lines = os.path.join(self.root, "ran.txt"), []
        if os.path.exists(ran):
            lines = read(ran).splitlines()
            os.remove(ran)
        return done.returncode, done.stdout + done.stderr, lines

    def with_log(self, text):
        with open(self.log, "w", encoding="utf-8") as f:
            f.write(text)

    def steps(self):
        for job in ("linux_tests", "macos_tests"):
            yield job, step_of(job, "tools/community_maps.py verify")[1]

    def test_both_steps_are_named_for_the_community_maps_and_the_windows_one_is_not(self):
        for job in ("linux_tests", "macos_tests"):
            self.assertIn("community maps", step_of(job, "tools/community_maps.py verify")[0], job)
        self.assertNotIn("ommunity", step_of("windows_tests", "map_sweep.exe --selftest")[0])
        self.assertEqual(step_of("linux_tests", "tools/community_maps.py verify")[0], step_of("macos_tests", "tools/community_maps.py verify")[0])
        self.assertEqual(step_of("linux_tests", "tools/community_maps.py verify")[1], step_of("macos_tests", "tools/community_maps.py verify")[1])

    def test_a_step_where_everything_passes_runs_every_tool_and_passes(self):
        self.with_log(sweep_log(0))
        for job, script in self.steps():
            status, out, ran = self.run_script(script)
            self.assertEqual(status, 0, (job, out))
            self.assertEqual([r.split()[0] for r in ran], ["map_sweep", "map_sweep", "community_maps", "bot_arena", "replay_tool", "lvl_tool"], job)
            self.assertIn("map_sweep Community-Maps --ticks 600 --jobs 2 --out build/community_sweep.json", ran[1])
            self.assertEqual(ran[2], "community_maps verify Community-Maps build/community_sweep.json")

    def test_a_sweep_that_fails_fails_the_step_and_shows_every_finding(self):
        self.with_log(sweep_log(60))
        for job, script in self.steps():
            status, out, ran = self.run_script(script, FAKE_SWEEP_EXIT="1")
            self.assertNotEqual(status, 0, job)
            for i in (0, 1, 30, 59):                                                      # (not the last seven only)
                self.assertIn("Map %d.lvl: does not load" % i, out, (job, i))
            self.assertIn("summary line 24", out, job)
            self.assertNotIn("Table row", out, job)                                       # (the table of every play is not poured into the log)
            self.assertEqual([r.split()[0] for r in ran], ["map_sweep", "map_sweep", "community_maps", "bot_arena", "replay_tool", "lvl_tool"], "the other tools still run: " + job)

    def test_a_sweep_that_stops_before_its_findings_shows_the_end_of_its_log(self):
        self.with_log("".join("line %d\n" % i for i in range(100)))
        for job, script in self.steps():
            status, out, _ = self.run_script(script, FAKE_SWEEP_EXIT="2")
            self.assertNotEqual(status, 0, job)
            self.assertIn("line 99", out, job)
            self.assertIn("line 60", out, job)
            self.assertNotIn("line 30\n", out, job)

    def test_a_check_that_fails_fails_the_step_and_the_other_tools_still_run(self):
        self.with_log(sweep_log(0))
        for job, script in self.steps():
            status, out, ran = self.run_script(script, FAKE_VERIFY_EXIT="1")
            self.assertNotEqual(status, 0, job)
            self.assertEqual([r.split()[0] for r in ran], ["map_sweep", "map_sweep", "community_maps", "bot_arena", "replay_tool", "lvl_tool"], job)

    def test_a_tool_that_fails_its_selftest_still_fails_the_step(self):
        self.with_log(sweep_log(0))
        for job, script in self.steps():
            self.assertNotEqual(self.run_script(script, FAKE_SELFTEST_EXIT="1")[0], 0, job)
            self.assertNotEqual(self.run_script(script, FAKE_TOOL_EXIT="1")[0], 0, job)

    def test_the_suite_of_the_runner_fails_when_the_sweep_or_the_check_fails(self):
        runner = read(os.path.join(REPO, "run_tests.sh"))
        function = function_of(runner, "run_community_maps_suite")
        self.assertIn("run_community_maps_suite", runner[runner.index(function) + len(function):])        # (a suite of the table calls it)

        def script_ok(asan):                                                               # (the function's status is told, as the runner's table gets it)
            return "set -eo pipefail\nBUILD_DIR=bd\nRUN_ASAN=%d\n%s\nstatus=0\nrun_community_maps_suite || status=$?\necho \"suite status $status\"\n" % (asan, function)
        self.with_log(sweep_log(0))
        status, out, ran = self.run_script(script_ok(0))
        self.assertIn("suite status 0", out)
        self.assertEqual(ran, ["map_sweep Community-Maps --ticks 600 --jobs 2 --out bd/community_sweep.json", "community_maps verify Community-Maps bd/community_sweep.json"])
        status, out, ran = self.run_script(script_ok(1))
        self.assertEqual(ran[0], "map_sweep Community-Maps --ticks 100 --jobs 2 --out bd/community_sweep.json")
        self.with_log(sweep_log(60))
        status, out, ran = self.run_script(script_ok(0), FAKE_SWEEP_EXIT="1")
        self.assertIn("suite status 1", out)
        for i in (0, 1, 30, 59):
            self.assertIn("Map %d.lvl: does not load" % i, out)
        self.assertIn("summary line 24", out)
        self.assertNotIn("Table row", out)
        self.assertEqual([r.split()[0] for r in ran], ["map_sweep"], "a failed sweep ends the suite: the check has nothing to read")
        self.with_log(sweep_log(0))
        status, out, ran = self.run_script(script_ok(0), FAKE_VERIFY_EXIT="1")
        self.assertIn("suite status 1", out)
        self.assertEqual([r.split()[0] for r in ran], ["map_sweep", "community_maps"])
        self.with_log("".join("line %d\n" % i for i in range(100)))
        status, out, ran = self.run_script(script_ok(0), FAKE_SWEEP_EXIT="2")
        self.assertIn("suite status 1", out)
        self.assertIn("line 99", out)


class Windows(unittest.TestCase):
    def test_ninja_under_the_msvc_environment_with_sccache(self):
        block = "\n".join(job_block("windows_tests"))
        self.assertIn("-G Ninja", block)
        self.assertIn("-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl", block)
        self.assertIn("-DCMAKE_CXX_COMPILER_LAUNCHER=sccache", block)
        self.assertIn("-DCMAKE_C_COMPILER_LAUNCHER=sccache", block)
        self.assertIn("ilammy/msvc-dev-cmd@", block)
        code = "\n".join(l for l in job_block("windows_tests") if not l.lstrip().startswith("#"))
        self.assertNotRegex(code, r"Visual Studio 1\d")                                    # (no Visual Studio generator: its projects cannot use a compiler launcher)
        self.assertNotIn("-A x64", code)
        self.assertNotIn("CXXFLAGS", block)                                                # (-MP made cl start its own processes: Ninja already runs the files side by side)
        self.assertNotIn("--config Release", block)
        self.assertNotIn("Release/", block)                                                # Ninja is single-configuration: the programs are not in a Release folder

    def test_the_cache_is_keyed_like_the_linux_ones_and_the_server_is_stopped_before_it_is_saved(self):
        block = "\n".join(job_block("windows_tests"))
        self.assertIn("key: sccache-${{ matrix.os }}-${{ github.sha }}", block)
        self.assertIn("restore-keys: sccache-${{ matrix.os }}-", block)
        self.assertIn("path: .sccache", block)
        self.assertIn("SCCACHE_DIR: ${{ github.workspace }}/.sccache", block)
        self.assertIn("sccache --show-stats", block)
        self.assertIn("sccache --stop-server", block)
        self.assertLess(block.index("sccache --stop-server"), block.index("Test programs (ctest)"))
        for job in ("linux_tests", "macos_tests"):
            self.assertIn("key: ccache-${{ runner.os }}-${{ github.sha }}", "\n".join(job_block(job)))
            self.assertIn("restore-keys: ccache-${{ runner.os }}-", "\n".join(job_block(job)))

    def test_the_sccache_download_is_pinned_and_checked(self):
        self.assertRegex(TEXT, r"\$version = '\d+\.\d+\.\d+'")
        self.assertRegex(TEXT, r"\$sha256 = '[0-9a-f]{64}'")
        self.assertIn("https://github.com/mozilla/sccache/releases/download/v$version/sccache-v$version-x86_64-pc-windows-msvc.zip", TEXT)
        self.assertIn("Get-FileHash -Algorithm SHA256", TEXT)
        self.assertIn("throw 'sccache: the checksum", TEXT)
        version = re.search(r"\$version = '(\d+\.\d+\.\d+)'", TEXT).group(1)
        self.assertIn("name: Install sccache %s" % version, TEXT)


class Actions(unittest.TestCase):
    def test_every_action_is_first_party_at_a_major_version_or_pinned_to_a_commit(self):
        uses = re.findall(r"^\s+- uses: (\S+)|^\s+uses: (\S+)", TEXT, re.M)
        names = [a or b for a, b in uses]
        self.assertGreater(len(names), 5)
        for name in names:
            if name.startswith("actions/"):
                self.assertRegex(name, r"^actions/[a-z-]+@v\d+$", name)
            else:
                self.assertRegex(name, r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+@[0-9a-f]{40}$", "an action that is not first-party must be pinned to a commit: " + name)

    def test_the_workflow_asks_for_no_more_permissions_than_it_needs(self):
        self.assertRegex(TEXT, r"(?m)^permissions:\n  contents: read\n")
        extra = re.findall(r"(?m)^\s+(?:actions|contents|pull-requests|issues|packages|id-token|checks|statuses|deployments): (read|write)$", TEXT)
        self.assertNotIn("write", extra)


def run_blocks():
    """[(job, the lines of the script)] of every `run:` of the workflow (a one-line `run: x` or the lines of a `run: |` block)"""
    found = []
    for job in job_ids():
        block = job_block(job)
        for i, line in enumerate(block):
            m = re.match(r"^(\s+)(?:- )?run: (.*)$", line)
            if not m:
                continue
            if m.group(2).strip() in ("|", ">", "|-"):
                body = []
                for follow in block[i + 1:]:
                    if follow.strip() and len(follow) - len(follow.lstrip()) <= len(m.group(1)):
                        break
                    body.append(follow)
                found.append((job, body))
            else:
                found.append((job, [m.group(2)]))
    return found


def steps_of(job):
    """[the lines of each step] of a job (a step begins with `      - ` at six blanks)"""
    block = job_block(job)
    starts = [i for i, l in enumerate(block) if re.match(r"^      - ", l)] + [len(block)]
    return [block[a:b] for a, b in zip(starts, starts[1:])]


def step_title(step):
    return next(l.strip() for l in step if "name: " in l or "uses: " in l)


def step_if(step):
    return next((l.split("if: ", 1)[1] for l in step if re.match(r"^        if: ", l) or re.match(r"^      - if: ", l)), None)


class TheJobsBehindTheRequiredNames(unittest.TestCase):
    """"Linux (GCC)" and "macOS (Apple clang)" are small jobs behind which the legs of linux_tests and macos_tests run: they must fail when ANY leg did not pass, because a job that is
    skipped counts as passing for a required check, and so would a name that nothing reports."""

    @classmethod
    def setUpClass(cls):
        cls.registered = set()
        for path in glob.glob(os.path.join(REPO, "tests", "*", "CMakeLists.txt")):
            if os.path.basename(os.path.dirname(path)) != "e2e":
                cls.registered |= set(re.findall(r"add_test\(NAME (\w+)", read(path)))

    def test_each_name_is_a_job_that_looks_at_the_result_of_its_legs_and_of_the_first_job_and_runs_whatever_they_did(self):
        for job, (behind, _) in AGGREGATES.items():
            block = job_block(job)
            self.assertIn("    needs: [%s]" % ", ".join(["changes"] + behind), block, job)
            self.assertIn("    if: ${{ always() }}", block, job)                              # (a plain `if` would skip the job when a leg failed, and a skipped job counts as passing)
            self.assertIn("          CHANGES: ${{ needs.changes.result }}", block, job)
            for leg in behind:
                self.assertTrue(any(re.match(r"^          [A-Z]+: \$\{\{ needs\.%s\.result \}\}$" % leg, l) for l in block), (job, leg))
            self.assertEqual(len([l for l in block if "uses:" in l]), 0, job)                # (no checkout, no action: nothing to fail but the test)
            self.assertEqual(len(steps_of(job)), 1, job)
            self.assertEqual([l for l in block if l.startswith("    runs-on:")], ["    runs-on: ubuntu-24.04" if job == "linux" else "    runs-on: ubuntu-latest"], job)
        for job in ("macos", "windows_2022", "windows_2026"):                                # (only these may be skipped, and only for a light answer)
            self.assertIn("          LIGHT: ${{ needs.changes.outputs.light }}", job_block(job), job)
        for job in ("linux", "web"):
            self.assertNotIn("light", "\n".join(l for l in job_block(job) if not l.lstrip().startswith("#")), job)

    @unittest.skipUnless(shutil.which("bash"), "bash is needed")
    def test_each_check_passes_only_when_the_first_job_and_every_job_behind_it_succeeded_or_were_rightly_skipped(self):
        """Every combination of results (the first job and each job behind the check: success, failure, cancelled, skipped, none) and of the first job's answer (true, false, none), each small job
        run in bash as GitHub runs it: it passes for exactly the combinations that the rule lets through."""
        if not yaml:
            self.skipTest("PyYAML is needed (the Linux jobs of the CI have it, and TheCiMachinesHavePyYAML checks that)")
        doc = yaml.safe_load(TEXT)
        self.assertEqual(problems_of_how_the_aggregates_decide(doc), [])
        # the rule itself, for the reader: a job that failed to decide never passes; the skip passes only for a light answer, for the three names that can be skipped
        self.assertTrue(aggregate_passes(doc, "macos", "success", "true", {"macos_tests": "skipped"}))
        self.assertFalse(aggregate_passes(doc, "macos", "failure", "true", {"macos_tests": "skipped"}))
        self.assertFalse(aggregate_passes(doc, "macos", "success", "false", {"macos_tests": "skipped"}))
        self.assertFalse(aggregate_passes(doc, "macos", "success", "", {"macos_tests": "skipped"}))
        self.assertFalse(aggregate_passes(doc, "linux", "failure", "", {"linux_tests": "success"}))
        self.assertFalse(aggregate_passes(doc, "web", "success", "true", {"web_image": "success", "web_stack": "skipped"}))
        self.assertTrue(aggregate_passes(doc, "windows_2026", "success", "false", {"windows_tests": "success"}))

    def test_the_legs_of_the_linux_job_are_every_test_of_ctest_once_the_scripts_and_the_python_slices(self):
        text = "\n".join(job_block("linux_tests"))
        alone = re.search(r'(?m)^  CTEST_ALONE: "(.*)"$', TEXT).group(1)
        long = re.search(r'(?m)^  CTEST_LONG: "(.*)"$', TEXT).group(1)
        first, second = [n for n in self.registered if re.search(alone, n)], [n for n in self.registered if re.search(long, n)]
        rest = [n for n in self.registered if not re.search(alone + "|" + long, n)]
        self.assertEqual(first, ["test_engine_lookups"], "the test that takes as long as the others of its leg together runs alone")
        self.assertEqual(len(second), 4, second)
        self.assertEqual(set(first) & set(second), set())
        self.assertEqual(len(first) + len(second) + len(rest), len(self.registered))
        self.assertGreater(len(rest), 80, "every other test, among them the ones to come")
        self.assertIn('-R "$CTEST_ALONE"', text)
        self.assertIn('-R "$CTEST_LONG"', text)
        self.assertIn('-E "${CTEST_ALONE}|${CTEST_LONG}"', text)
        self.assertEqual(text.count("--no-tests=error"), 3, "a part that matches no test (a renamed test) must fail, not pass with nothing run")
        parts = re.findall(r"(?m)^            part: (\S+)$", text)
        slices = re.findall(r"(?m)^            slice: (\d+)/(\d+)$", text)
        self.assertEqual(sorted(p for p in parts if p != "python"), ["ctest-long", "ctest-lookups", "ctest-rest", "scripts"])
        self.assertEqual(parts.count("python"), len(slices), "a leg for each slice of the python tests")
        self.assertGreaterEqual(len(slices), 2)
        self.assertEqual({n for _, n in slices}, {str(len(slices))}, "one number of slices")
        self.assertEqual(sorted(k for k, _ in slices), [str(k) for k in range(1, len(slices) + 1)], "every slice of the python tests is run, once")
        self.assertIn('run_python_tests.py --shard "$SLICE"', text)
        self.assertNotIn("--skip-linux-only", text, "Linux runs every python test")
        self.assertNotIn(" -R ", "\n".join(job_block("macos_tests")), "macOS runs all of ctest")

    def test_macos_runs_all_of_ctest_and_the_python_tests_that_depend_on_the_system(self):
        text = "\n".join(job_block("macos_tests"))
        self.assertEqual(sorted(re.findall(r"(?m)^            part: (\S+)$", text)), ["ctest", "scripts-python"])
        self.assertIn("python3 tests/scripts/run_python_tests.py --skip-linux-only", text)
        self.assertNotIn("--shard", text)
        self.assertIn("ctest --test-dir build --output-on-failure --timeout 900 --parallel 4 --no-tests=error\n", text)

    def test_every_leg_builds_and_every_step_of_a_leg_says_which_legs_run_it(self):
        for job in ("linux_tests", "macos_tests"):
            steps = steps_of(job)
            names = [step_title(s) for s in steps]
            self.assertTrue(any("Build the game, the server and the test programs" in n for n in names), job)
            for step in steps:
                head = "\n".join(step)
                if "matrix.part" not in head and "steps.build" not in head:
                    # only the setup (checkout, packages, caches, configure, build) and the upload of the logs may run in every leg
                    self.assertTrue(re.search(r"name: (Install|Restore|Configure|Build|Keep the test logs)|uses: actions/checkout", head), head)


class TheLinuxImage(unittest.TestCase):
    """The Linux jobs run on Ubuntu 24.04 (GCC 13), not on `ubuntu-latest`, so that the compiler under the required check changes only with the workflow: the default image is Ubuntu 26.04 with GCC 15
    now, which the code also builds with. The jobs that build and the small job behind "Linux (GCC)" stay on one image; moving to another is a change of the workflow and of this test."""

    def test_the_linux_jobs_run_on_ubuntu_24_04_and_not_on_the_default_image(self):
        for job in ("linux_tests", "linux"):
            self.assertEqual([l for l in job_block(job) if l.startswith("    runs-on:")], ["    runs-on: ubuntu-24.04"], job)


class LightChecks(unittest.TestCase):
    """A pull request that changes pages, documents and page tests only (tools/ci_light.sh decides, tests/scripts/test_ci_light.py tests it) skips the macOS and Windows builds."""

    def test_the_first_job_runs_the_script_with_the_ids_of_the_commits_and_nothing_of_the_text_of_the_pull_request(self):
        block = "\n".join(job_block("changes"))
        self.assertEqual([l for l in job_block("changes") if l.startswith("    needs:") or l.startswith("    if:")], [], "it runs for every event, first")
        self.assertIn("          fetch-depth: 0\n          persist-credentials: false\n", block)       # (the merge base needs the history; no credentials are kept)
        self.assertIn("      light: ${{ steps.decide.outputs.light }}", block)
        step = block[block.index("      - name: Light checks or all of them?"):]
        self.assertIn("        id: decide\n", step)
        self.assertIn("          EVENT: ${{ github.event_name }}\n          BASE_REF: ${{ github.base_ref }}\n          BASE_SHA: ${{ github.event.pull_request.base.sha }}\n          HEAD_SHA: ${{ github.event.pull_request.head.sha }}\n        run:", step)
        self.assertNotIn("MAIN_REF", block)              # (the change is judged against the base of the pull request and nothing else: a retarget is an `edited` run with the new base commit)
        self.assertNotIn("origin/main", block)
        self.assertIn("        run: bash tools/ci_light.sh\n", step)
        self.assertTrue(os.path.isfile(os.path.join(REPO, "tools", "ci_light.sh")))

    def test_no_text_of_a_pull_request_or_of_the_event_is_put_in_a_script(self):
        scripts = run_blocks()
        self.assertGreater(len(scripts), 20)
        for job, body in scripts:
            for line in body:
                self.assertNotIn("${{", line, "%s: an expression inside a script (put it in `env:` and read the variable): %s" % (job, line))
        code = "\n".join(l for l in LINES if not l.lstrip().startswith("#"))
        events = sorted(set(re.findall(r"github\.event\.[A-Za-z_.]+", code)))
        self.assertEqual(events, ["github.event.pull_request.base.sha", "github.event.pull_request.head.sha", "github.event.pull_request.number"])      # (the number: in the group of the concurrency only)
        self.assertEqual([l.strip() for l in code.splitlines() if "github.base_ref" in l], ["BASE_REF: ${{ github.base_ref }}"], "the name of the base branch reaches the script through one variable, which the script compares with a literal")
        self.assertEqual([l for l in code.splitlines() if "pull_request.number" in l], ["  group: ci-${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}"])
        for name in ("github.head_ref", "pull_request.title", "pull_request.body", "head.ref", "head_commit", "head.label"):
            self.assertEqual([l for l in code.splitlines() if name in l and not l.startswith("  group: ")], [], name)       # (the group of the concurrency is the one place for the branch's name)
        self.assertNotIn("pull_request_target", TEXT)
        self.assertNotIn("workflow_run", TEXT)

    def test_the_macos_and_windows_legs_are_skipped_at_job_level_and_nothing_else_looks_at_the_decision(self):
        for job in ("macos_tests", "windows_tests"):
            block = job_block(job)
            self.assertIn("    needs: changes", block, job)
            self.assertIn("    if: ${{ !cancelled() && needs.changes.result == 'success' && needs.changes.outputs.light != 'true' }}", block, job)       # (they do not run when `changes` failed)
            self.assertEqual("\n".join(block).count("needs.changes.outputs.light"), 1, job + ": no step of a leg looks at the decision again")
        self.assertEqual([l for l in job_block("windows_tests") if l.startswith("    runs-on:")], ["    runs-on: ${{ matrix.os }}"])
        for job in ("linux_tests", "web_image", "web_stack"):
            block = "\n".join(l for l in job_block(job) if not l.lstrip().startswith("#"))
            self.assertNotIn("light", block, job)
            self.assertNotIn("needs.changes", block, job)

    def test_the_windows_legs_have_no_step_that_is_there_for_a_skipped_job(self):
        """The legs are skipped as a whole (the small jobs carry the names), so no step needs a condition on the decision, and every step that runs a test waits for the build."""
        for step in steps_of("windows_tests"):
            condition = step_if(step) or ""
            self.assertNotIn("light", condition, step_title(step))
            self.assertNotIn("ubuntu-latest", "\n".join(step))
        text = "\n".join(job_block("windows_tests"))
        self.assertEqual(len(re.findall(r"(?m)^      - (?:name|uses): ", text)) > 8, True)

    def test_the_skipped_jobs_run_only_the_scripts_that_the_light_decision_knows(self):
        """tools/ci_light.sh calls full the pages that the two macOS script suites read, and tests/scripts/test_ci_light.py scans these (and the C++ of ctest) for the reads: another script of tests/scripts
        in a step of the macOS or Windows jobs would read files that nobody has listed."""
        macos = "\n".join(l for l in job_block("macos_tests") if not l.lstrip().startswith("#"))
        self.assertEqual(sorted(set(re.findall(r"tests/scripts/[A-Za-z0-9_]+\.(?:sh|py|js)", macos))),
                         ["tests/scripts/run_python_tests.py", "tests/scripts/test_ants_server.sh", "tests/scripts/test_start_game.sh"])
        self.assertEqual(sorted(set(re.findall(r"\btools/[A-Za-z0-9_]+\.(?:sh|py)", macos))), ["tools/check_version_consistency.py", "tools/community_maps.py"])
        windows = "\n".join(l for l in job_block("windows_tests") if not l.lstrip().startswith("#"))
        self.assertEqual(re.findall(r"tests/scripts/|\btools/[A-Za-z0-9_]+\.(?:sh|py)", windows), [], "the Windows jobs run ctest, the tool self-tests and the E2E runner")

    def test_a_push_and_a_manual_run_are_never_light(self):
        script = read(os.path.join(REPO, "tools", "ci_light.sh"))
        self.assertIn('if [ "${EVENT:-}" != pull_request ]; then', script)
        self.assertIn("finish false", script.split('if [ "${EVENT:-}" != pull_request ]; then', 1)[1].splitlines()[1])


class Documents(unittest.TestCase):
    """What the documents say about the pipeline is what the files do (the documents name them instead of restating them where they can; these are the facts that they must state)."""

    @classmethod
    def setUpClass(cls):
        cls.workflow = read(os.path.join(REPO, "docs", "WORKFLOW.md"))
        cls.agents = read(os.path.join(REPO, "AGENTS.md"))
        cls.testing = read(os.path.join(REPO, "docs", "TESTING.md"))

    def test_the_required_checks_are_named_where_a_person_decides_about_merging(self):
        for name in ("Linux (GCC)", "macOS (Apple clang)", "Windows (MSVC 2022)", "Windows (MSVC 2026)", "Web (Emscripten, Docker image)"):
            self.assertIn(name, self.workflow)
        self.assertIn("(Linux GCC, macOS Apple clang, Windows MSVC 2022, Windows MSVC 2026, Web)", self.agents)   # (AGENTS.md names the same five in short form, in this order)
        for name in ("Linux (GCC)", "macOS (Apple clang)", "Windows (MSVC 2022)", "Windows (MSVC 2026)", "Web (Emscripten, Docker image)"):   # (the table of docs/TESTING.md has a row for each)
            self.assertIn("| " + name + " |", self.testing)

    def test_the_documents_say_that_ci_does_not_deploy(self):
        self.assertIn("CI does not deploy", self.workflow)
        self.assertIn("automatic update", self.workflow)
        self.assertNotIn("deploy hook", self.testing)

    def test_the_agents_rules_say_what_the_workflow_says(self):
        for needle in ("pull request", "CI is the full gate", "merge commit", "tools/mutate.py", "`./run_tests.sh --asan`, CI has none", "Documents and comments say what and why, briefly"):
            self.assertIn(needle, self.agents)
        for stale in ("Push to `origin main`", "fast-forward", "Mandatory Dual Local", "Mandatory Docker Web Build"):
            self.assertNotIn(stale, self.agents)
            self.assertNotIn(stale, self.workflow)
        self.assertIn("no sanitizer job", self.workflow)

    def test_every_tool_that_the_workflow_page_names_exists(self):
        for path in re.findall(r"`(tools/[a-z_]+\.(?:py|sh))`", self.workflow) + re.findall(r"`(tests/scripts/[a-z_]+\.(?:py|sh))`", self.workflow):
            self.assertTrue(os.path.isfile(os.path.join(REPO, path)), path)


try:
    import yaml
except ImportError:                                                                        # (macOS runners have no PyYAML: the text checks above are the guard there)
    yaml = None


@unittest.skipUnless(yaml, "PyYAML is needed")
class Structure(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = yaml.safe_load(TEXT)

    def test_it_parses_and_the_jobs_are_these(self):
        self.assertEqual(list(self.doc["jobs"]), ["changes", "linux_tests", "linux", "macos_tests", "macos", "windows_tests", "windows_2022", "windows_2026", "web_image", "web_stack", "web"])

    def test_the_triggers(self):
        on = self.doc[True] if True in self.doc else self.doc["on"]                       # (YAML 1.1 reads `on` as true)
        self.assertEqual(sorted(on), ["pull_request", "push", "workflow_dispatch"])
        self.assertEqual(on["pull_request"], {"types": ["opened", "synchronize", "reopened", "edited"]})
        self.assertEqual(on["push"], {"branches": ["main"]})


# ---- the legs of the test jobs, run in the head ---------------------------------------------------------------------------------------------------------------------------------------------
# What a typo does: a step of a leg that asks for `matrix.part == 'ctest-longg'` never runs, and the leg passes with no test run; two legs that name their log artifact alike make the upload
# of the second fail; a renamed step id makes every test step wait for a build that "is not there". None of that fails a job, so it is found here, in the file, before a run shows it (or not).

REQUIRED_NAMES = ("Linux (GCC)", "macOS (Apple clang)", "Windows (MSVC 2022)", "Windows (MSVC 2026)", "Web (Emscripten, Docker image)")
# The small job of each required name (the key), the jobs behind it, and whether the change's light answer may skip them (then the name passes without them)
AGGREGATES = {"linux": (["linux_tests"], False), "macos": (["macos_tests"], True), "windows_2022": (["windows_tests"], True), "windows_2026": (["windows_tests"], True),
              "web": (["web_image", "web_stack"], False)}
RESULTS = ("success", "failure", "cancelled", "skipped")
TEST_COMMAND = re.compile(r"\bctest\b|--selftest|\be2e_runner|test_start_game\.sh|test_ants_server\.sh|run_python_tests\.py|check_version_consistency\.py")
EXPRESSION_TOKEN = re.compile(r"&&|\|\||==|!=|!|\(|\)|'(?:[^']|'')*'|[A-Za-z_][\w.\-]*\(\)|[A-Za-z_][\w.\-]*|\S")


def truthy(value):
    return value not in (False, None, "", 0)


def evaluate(expression, lookup, status="success"):
    """The value of an expression of the workflow, in the part of GitHub's language that ci.yml uses: ||, &&, !, ==, != (strings are equal without regard to case), strings, the status functions
    (`status` is that of the job so far) and the contexts, which `lookup(path)` gives (it raises for a reference that is nothing). `${{ }}` around the expression is taken off."""
    text = expression.strip()
    if text.startswith("${{") and text.endswith("}}"):
        text = text[3:-2]
    tokens = EXPRESSION_TOKEN.findall(text)
    at = [0]

    def peek():
        return tokens[at[0]] if at[0] < len(tokens) else None

    def take():
        at[0] += 1
        return tokens[at[0] - 1]

    def primary():
        token = take()
        if token == "(":
            value = disjunction()
            if take() != ")":
                raise ValueError("a bracket is not closed: " + expression)
            return value
        if token.startswith("'"):
            return token[1:-1].replace("''", "'")
        if token.endswith("()"):
            return {"always": True, "success": status == "success", "failure": status == "failure", "cancelled": status == "cancelled"}[token[:-2]]
        return lookup(token)

    def unary():
        if peek() == "!":
            take()
            return not truthy(unary())
        return primary()

    def comparison():
        left = unary()
        if peek() in ("==", "!="):
            operator = take()
            equal = str(left).lower() == str(unary()).lower()
            return equal if operator == "==" else not equal
        return left

    def conjunction():
        value = comparison()
        while peek() == "&&":
            take()
            right = comparison()
            value = right if truthy(value) else value
        return value

    def disjunction():
        value = conjunction()
        while peek() == "||":
            take()
            right = conjunction()
            value = value if truthy(value) else right
        return value

    value = disjunction()
    if peek() is not None:
        raise ValueError("something is left over: " + expression)
    return value


def expand(template, leg):
    """A name with `${{ matrix.KEY }}` in it, for one leg (a reference to anything else is an error)"""
    def one(match):
        path = match.group(1)
        if not path.startswith("matrix.") or path[len("matrix."):] not in leg:
            raise KeyError("%s: no such value in the leg %s" % (path, sorted(leg)))
        return str(leg[path[len("matrix."):]])
    return re.sub(r"\$\{\{\s*([^}]*?)\s*\}\}", one, str(template))


def legs_of(job, event="pull_request"):
    """The legs of a job: the dictionaries of its matrix, [{}] for a job without one (the Windows jobs choose the list in an expression: the list of a pull request, or of a push to another branch)."""
    include = (job.get("strategy") or {}).get("matrix", {}).get("include")
    if include is None:
        return [{}]
    if isinstance(include, str):
        lists = re.findall(r"'(\[\{.*?\}\])'", include)
        return json.loads(lists[0] if event == "push to another branch" else lists[1])
    return include


def steps_that_run(job, leg, light="", status="success"):
    """[the steps of the job that run for this leg], as GitHub decides it when every step that runs succeeds: the condition of a step is evaluated with the conclusions of the steps before it
    (a step that is skipped has the conclusion `skipped`); `light` is what the first job said (needs.changes.outputs.light)."""
    conclusions, ran = {}, []

    def lookup(path):
        parts = path.split(".")
        if parts[0] == "matrix" and len(parts) == 2 and parts[1] in leg:
            return leg[parts[1]]
        if parts[0] == "steps" and len(parts) == 3 and parts[2] == "conclusion" and parts[1] in conclusions:
            return conclusions[parts[1]]
        if parts[0] == "steps" and len(parts) >= 4 and parts[2] == "outputs" and parts[1] in conclusions:
            return ""                                                                          # (an output that the step did not set)
        if path == "needs.changes.outputs.light":
            return light
        raise KeyError("%s: nothing of this leg or of the steps before this one" % path)

    for step in job["steps"]:
        does = truthy(evaluate(str(step.get("if", "success()")), lookup, status))
        if "id" in step:
            conclusions[step["id"]] = "success" if does else "skipped"
        if does:
            ran.append(step)
    return ran


def is_test_step(step):
    return bool(TEST_COMMAND.search(str(step.get("run", ""))))


def title(step):
    return step.get("name") or step.get("uses")


def problems_of_the_legs(doc):
    """What is wrong with the legs of linux_tests and macos_tests, as a list of sentences (none: nothing)."""
    problems = []
    for job_id in ("linux_tests", "macos_tests"):
        job = doc["jobs"][job_id]
        legs = legs_of(job)
        parts = {leg["part"] for leg in legs}
        for key in ("id", "name"):
            values = [leg.get(key) for leg in legs]
            if None in values or len(set(values)) != len(values):
                problems.append("%s: the legs do not each have their own %s: %s" % (job_id, key, values))
        before = set()
        for step in job["steps"]:
            condition = str(step.get("if", ""))
            for part in re.findall(r"matrix\.part\s*[=!]=\s*'([^']*)'", condition):
                if part not in parts:
                    problems.append("%s: %r asks for the part %r, which no leg has (%s)" % (job_id, title(step), part, sorted(parts)))
            for other in re.findall(r"\bsteps\.([A-Za-z0-9_-]+)\.", condition):
                if other not in before:
                    problems.append("%s: %r asks for the step %r, which is not before it" % (job_id, title(step), other))
            before.add(step.get("id"))
        running = {}
        for leg in legs:
            try:
                running[leg["id"]] = steps_that_run(job, leg)
            except (KeyError, ValueError) as error:
                problems.append("%s: the leg %s: %s" % (job_id, leg["id"], error))
                continue
            tests = [step for step in running[leg["id"]] if is_test_step(step)]
            if not tests:
                problems.append("%s: the leg %s runs no test, so it passes with nothing tested" % (job_id, leg["id"]))
            if len([step for step in tests if re.search(r"\bctest\b", step["run"])]) > 1:
                problems.append("%s: the leg %s runs ctest twice" % (job_id, leg["id"]))
            for step in running[leg["id"]]:
                for key in re.findall(r"matrix\.(\w+)", json.dumps(step)):
                    if key not in leg:
                        problems.append("%s: %r uses matrix.%s, which the leg %s does not have" % (job_id, title(step), key, leg["id"]))
        for step in job["steps"]:
            if not is_test_step(step):
                continue
            where = [leg for leg in legs if any(step is s for s in running.get(leg["id"], []))]
            if not where:
                problems.append("%s: the test step %r runs in no leg" % (job_id, title(step)))
            elif len({leg["part"] for leg in where}) != 1:
                problems.append("%s: the test step %r runs in the legs of more than one part" % (job_id, title(step)))
            elif re.search(r"\bctest\b", step["run"]) and len(where) != 1:
                problems.append("%s: the ctest step %r runs in %d legs (each part of ctest is one leg)" % (job_id, title(step), len(where)))
        selected = {leg["part"] for leg in legs if any(is_test_step(step) for step in running.get(leg["id"], []))}
        if selected != parts:
            problems.append("%s: no test step is selected by the part(s) %s" % (job_id, sorted(parts - selected)))
    return problems


def problems_of_the_names(doc):
    """What is wrong with the names of the checks and of the log artifacts: each exists once, and the five required names are each reported once (for a pull request)."""
    problems, checks, artifacts = [], [], []
    for job_id, job in doc["jobs"].items():
        for leg in legs_of(job):
            try:
                checks.append(expand(job.get("name", job_id), leg))
            except KeyError as error:
                problems.append("%s: %s" % (job_id, error))
            for step in job.get("steps", []):
                if str(step.get("uses", "")).startswith("actions/upload-artifact@"):
                    try:
                        artifacts.append(expand(step["with"]["name"], leg))
                    except KeyError as error:
                        problems.append("%s: %s" % (job_id, error))
    for name in sorted(set(n for n in checks if checks.count(n) > 1)):
        problems.append("two checks are named %r" % name)
    for name in REQUIRED_NAMES:
        if checks.count(name) != 1:
            problems.append("the required check %r is reported %d times" % (name, checks.count(name)))
    for name in sorted(set(n for n in artifacts if artifacts.count(n) > 1)):
        problems.append("two legs upload the artifact %r (the second upload fails)" % name)
    if not artifacts:
        problems.append("no artifact of logs")
    return problems


def problems_of_the_aggregates(doc):
    """The small jobs that carry the required names: each needs the first job and every job behind it, looks at the result of each, and runs whatever they did."""
    problems = []
    for job, (behind, _) in AGGREGATES.items():
        needs = doc["jobs"][job].get("needs", [])
        needs = [needs] if isinstance(needs, str) else list(needs)
        if sorted(needs) != sorted(["changes"] + behind):
            problems.append("%s: needs %s, but it stands for %s and the first job" % (job, needs, behind))
        text = json.dumps(doc["jobs"][job]["steps"])
        for leg in ["changes"] + behind:
            if "needs.%s.result" % leg not in text:
                problems.append("%s: the result of %s is not looked at" % (job, leg))
        if "always()" not in str(doc["jobs"][job].get("if", "")):
            problems.append("%s: runs only when its needs passed (a skipped job counts as passing)" % job)
    # the jobs of a system are all behind its name
    for system in ("linux", "macos"):
        jobs = [j for j in doc["jobs"] if j.startswith(system + "_")]
        if sorted(jobs) != sorted(AGGREGATES[system][0]):
            problems.append("%s: the jobs of its system are %s" % (system, jobs))
    for job in doc["jobs"]:
        if job.startswith("windows_") and job != "windows_tests" and job not in AGGREGATES:
            problems.append("%s: a Windows job that no required name stands for" % job)
    return problems


def aggregate_passes(doc, job, changes, light, results):
    """Whether the small job `job` passes (its script run in bash as GitHub runs it) when the first job has the result `changes` and says `light`, and the jobs behind it have `results`
    ({job: result}). KeyError for a variable of its step that the context does not give."""
    step = doc["jobs"][job]["steps"][0]
    env = {}
    for name, value in step["env"].items():
        m = re.fullmatch(r"\$\{\{ needs\.([a-z0-9_]+)\.result \}\}", str(value))
        if m:
            env[name] = changes if m.group(1) == "changes" else results[m.group(1)]
        elif value == "${{ needs.changes.outputs.light }}":
            env[name] = light
        else:
            raise KeyError("%s: %s" % (name, value))
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "step.sh")
        with open(path, "w", encoding="utf-8") as f:
            f.write(step["run"])
        done = subprocess.run(["bash", "--noprofile", "--norc", "-eo", "pipefail", path], env=dict(os.environ, **env), capture_output=True, text=True)
    return done.returncode == 0


def problems_of_how_the_aggregates_decide(doc, thorough=True):
    """Every combination of the first job's result (success, failure, cancelled, skipped, none), its answer (true, false, none) and the results of the jobs behind the name: the small job passes
    for exactly these: the first job succeeded and (every job behind it succeeded, or the name may be skipped, the answer is `true` and every job behind it was skipped)."""
    problems = []
    for job, (behind, may_skip) in AGGREGATES.items():
        for changes in (RESULTS + ("",)) if thorough else ("success", "failure"):
            for light in ("true", "false", ""):
                for combination in itertools.product((RESULTS + ("",)) if thorough else ("success", "failure", "skipped"), repeat=len(behind)):
                    results = dict(zip(behind, combination))
                    expected = changes == "success" and (all(r == "success" for r in combination) or (may_skip and light == "true" and all(r == "skipped" for r in combination)))
                    try:
                        got = aggregate_passes(doc, job, changes, light, results)
                    except KeyError as error:
                        return problems + ["%s: %s" % (job, error)]
                    if got != expected:
                        problems.append("%s %s when the first job is %r (%r) and the jobs behind it are %s" % (job, "passes" if got else "fails", changes, light, results))
    return problems


def job_runs(doc, job_id, light, changes="success", event="pull_request"):
    """Whether the job runs, by its job-level `if`, when the first job has the result `changes` and says `light` (for a job whose only need is the first job, or one with `always()`)"""
    job = doc["jobs"][job_id]
    needs = job.get("needs", [])
    needs = [needs] if isinstance(needs, str) else needs
    status = "success" if changes == "success" or "changes" not in needs else "failure"        # (the status of a job whose need did not succeed)

    def lookup(path):
        return {"needs.changes.outputs.light": light, "needs.changes.result": changes, "github.event_name": event, "github.ref": "refs/pull/1/merge"}[path]
    return truthy(evaluate(str(job.get("if", "success()")), lookup, status))


def problems_of_the_light_checks(doc):
    """What the first job's answer does, for what it can say: light (the macOS and Windows legs are skipped and their small jobs say so), not light (everything runs), nothing (the job failed: no
    leg of macOS or Windows runs, and the small jobs run to fail; the legs that do not depend on it run). The small jobs and the other legs run for every answer."""
    problems = []
    jobs = list(doc["jobs"])
    for light, changes in (("true", "success"), ("false", "success"), ("", "failure"), ("", "cancelled"), ("", "skipped")):
        said = "%r with the result %s" % (light, changes)
        for job_id in ("macos_tests", "windows_tests"):
            expected = changes == "success" and light != "true"
            if job_runs(doc, job_id, light, changes) != expected:
                problems.append("%s %s when the first job says %s" % (job_id, "is skipped" if expected else "runs", said))
        for job_id in [j for j in jobs if j not in ("macos_tests", "windows_tests")]:
            if not job_runs(doc, job_id, light, changes):
                problems.append("%s is skipped when the first job says %s" % (job_id, said))
    for leg in legs_of(doc["jobs"]["windows_tests"]):
        ran = steps_that_run(doc["jobs"]["windows_tests"], leg, "false")
        tests = [step for step in ran if is_test_step(step)]
        if len(tests) != 3:
            problems.append("windows %s: it must run its three kinds of test on %s: %s" % (leg["name"], leg["os"], [title(s) for s in tests]))
        if str(doc["jobs"]["windows_tests"]["runs-on"]) != "${{ matrix.os }}":
            problems.append("windows %s: it runs on %s" % (leg["name"], doc["jobs"]["windows_tests"]["runs-on"]))
    return problems


def problems_of_ctest(doc):
    """A ctest that finds no test passes: every call has --no-tests=error (a renamed test, a filter that matches nothing)."""
    problems = []
    for job_id, job in doc["jobs"].items():
        for step in job.get("steps", []):
            for line in str(step.get("run", "")).splitlines():
                if re.search(r"\bctest\b", line) and "--no-tests=error" not in line:
                    problems.append("%s: %r: a ctest without --no-tests=error: %s" % (job_id, title(step), line.strip()))
    return problems


def problems_of_swallowed_failures(doc):
    """A failure that cannot fail anything: `continue-on-error` (on a job, a leg or a step: on the single step of a small job it would make a required name pass for every result), a test command
    followed by `|| true` or `|| :`, a script that turns the exit on `set +e`, and a script that keeps a status (`status=0`) and does not end by exiting with it or has a test command that does not set it."""
    problems = []
    for job_id, job in doc["jobs"].items():
        if "continue-on-error" in job:
            problems.append("%s: continue-on-error on the job" % job_id)
        for step in job.get("steps", []):
            if "continue-on-error" in step:
                problems.append("%s: %r has continue-on-error" % (job_id, title(step)))
            script = str(step.get("run", ""))
            lines = [l.strip() for l in script.splitlines() if l.strip() and not l.strip().startswith("#")]
            if re.search(r"\bset\s+\+e\b", script):
                problems.append("%s: %r turns the exit on error off" % (job_id, title(step)))
            tests = [l for l in lines if TEST_COMMAND.search(l)]
            for line in tests:
                if re.search(r"\|\|\s*(true|:)(\s|;|$)|;\s*(true|:)\s*$", line):
                    problems.append("%s: %r swallows the failure of a test command: %s" % (job_id, title(step), line))
            if "status=0" in lines:
                if lines[-1] != "exit $status":
                    problems.append("%s: %r keeps a status and does not end with `exit $status`" % (job_id, title(step)))
                for line in tests:
                    if "status=1" not in line:
                        problems.append("%s: %r runs a test command that does not set the status: %s" % (job_id, title(step), line))
    return problems


CHECKS_OF_THE_WORKFLOW = (problems_of_the_legs, problems_of_the_names, problems_of_the_aggregates, problems_of_ctest, problems_of_the_light_checks, problems_of_swallowed_failures)


@unittest.skipUnless(yaml, "PyYAML is needed")
class TheLegsRunTheirTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = yaml.safe_load(TEXT)

    def test_the_expressions_are_read_as_github_reads_them(self):
        names = {"a": "x", "b": "", "c": "Y"}
        lookup = names.__getitem__
        for expression, value in (("a == 'x'", True), ("a == 'X'", True), ("a != 'x'", False), ("!b", True), ("!a", False), ("b || a", "x"), ("a && c", "Y"), ("b && c", ""), ("a == 'x' && c == 'y'", True),
                                  ("a == 'z' && 'p' || 'q'", "q"), ("a == 'x' && 'p' || 'q'", "p"), ("!(a == 'x')", False), ("${{ a != 'x' || c == 'y' }}", True), ("a == 'it''s'", False)):
            self.assertEqual(evaluate(expression, lookup), value, expression)
        self.assertTrue(evaluate("always()", lookup, "failure") and evaluate("failure()", lookup, "failure") and evaluate("!cancelled()", lookup, "failure"))
        self.assertFalse(evaluate("success()", lookup, "failure") or evaluate("failure()", lookup) or evaluate("!cancelled()", lookup, "cancelled"))
        with self.assertRaises(KeyError):
            evaluate("nothing == 'x'", lookup)
        with self.assertRaises(ValueError):
            evaluate("a == 'x' b", lookup)

    def test_the_legs_are_every_part_run_by_steps_that_exist_and_every_leg_runs_a_test(self):
        self.assertEqual(problems_of_the_legs(self.doc), [])
        # and the legs really are what the file says (a simulation that ran nothing would find nothing wrong)
        linux = self.doc["jobs"]["linux_tests"]
        ran = {leg["id"]: [title(s) for s in steps_that_run(linux, leg) if is_test_step(s)] for leg in legs_of(linux)}
        self.assertEqual(sorted(ran), ["ctest-long", "ctest-lookups", "ctest-rest", "python-1", "python-2", "python-3", "scripts"])
        self.assertEqual(len(ran["scripts"]), 3, ran["scripts"])
        self.assertEqual(len(ran["python-1"]), 1)
        macos = self.doc["jobs"]["macos_tests"]
        self.assertEqual({leg["id"]: len([s for s in steps_that_run(macos, leg) if is_test_step(s)]) for leg in legs_of(macos)}, {"ctest": 1, "scripts-python": 4})

    def test_every_check_and_every_artifact_of_the_logs_has_a_name_of_its_own_and_the_five_required_names_are_reported_once(self):
        self.assertEqual(problems_of_the_names(self.doc), [])

    def test_the_small_jobs_need_every_job_of_their_system_and_run_whatever_the_legs_did(self):
        self.assertEqual(problems_of_the_aggregates(self.doc), [])

    @unittest.skipUnless(shutil.which("bash"), "bash is needed")
    def test_the_small_jobs_pass_for_exactly_the_results_that_the_rule_lets_through(self):
        self.assertEqual(problems_of_how_the_aggregates_decide(self.doc), [])

    def test_no_failure_is_swallowed_in_a_job_a_leg_a_step_or_a_gate(self):
        self.assertEqual(problems_of_swallowed_failures(self.doc), [])
        self.assertNotIn("continue-on-error", TEXT)                                       # (not even in a comment: the word is searched for in the text)
        # the python tests of both systems are such steps: the status is kept, set by the runner and the version check, and the step exits with it
        for job in ("linux_tests", "macos_tests"):
            steps = [s for s in self.doc["jobs"][job]["steps"] if "run_python_tests.py" in str(s.get("run", ""))]
            self.assertEqual(len(steps), 1, job)
            lines = [l.strip() for l in steps[0]["run"].splitlines()]
            self.assertEqual(lines[0], "status=0")
            self.assertEqual(lines[-1], "exit $status")
            self.assertTrue(any(l.startswith("python3 tests/scripts/run_python_tests.py") and l.endswith("|| status=1") for l in lines), job)
            self.assertTrue(any(l == "python3 tools/check_version_consistency.py || status=1" for l in lines), job)

    def test_no_ctest_passes_with_no_test_run(self):
        self.assertEqual(problems_of_ctest(self.doc), [])
        calls = sum(len(re.findall(r"\bctest\b", str(step.get("run", "")))) for job in self.doc["jobs"].values() for step in job.get("steps", []))
        self.assertEqual(calls, 5, "three Linux parts, macOS, Windows: where is the other one?")

    def test_the_answer_of_the_first_job_skips_macos_and_the_builds_of_windows_and_nothing_else(self):
        self.assertEqual(problems_of_the_light_checks(self.doc), [])
        windows = self.doc["jobs"]["windows_tests"]
        self.assertEqual([leg["name"] for leg in legs_of(windows)], ["MSVC 2022", "MSVC 2026"])
        self.assertEqual([leg["os"] for leg in legs_of(windows)], ["windows-2022", "windows-latest"])        # (both compilers for every event: a name never passes without its compiler)


@unittest.skipUnless(yaml, "PyYAML is needed")
class TheChecksCanFail(unittest.TestCase):
    """The checks above are not vacuous: a copy of ci.yml with one breakage in it is judged (the breakages are the ones a reader of the file could make by a slip of the pen)."""

    @classmethod
    def setUpClass(cls):
        cls.doc = yaml.safe_load(TEXT)

    def judged(self, old, new, check):
        self.assertEqual(TEXT.count(old), 1, "the text to break is not there exactly once: " + old)
        return check(yaml.safe_load(TEXT.replace(old, new)))

    def test_a_typo_in_the_part_of_a_step_leaves_a_leg_with_nothing_to_run(self):
        for old, new, word in (
                ("steps.build.conclusion == 'success' && matrix.part == 'ctest-long' }}", "steps.build.conclusion == 'success' && matrix.part == 'ctest-longg' }}", "ctest-longg"),
                ("steps.build.conclusion == 'success' && matrix.part == 'scripts' }}\n        run: |\n          status=0\n          build/map_sweep --selftest", "steps.build.conclusion == 'success' && matrix.part == 'script' }}\n        run: |\n          status=0\n          build/map_sweep --selftest", "script"),
                ("!cancelled() && matrix.part == 'python' }}", "!cancelled() && matrix.part == 'pyhton' }}", "pyhton"),
                ("matrix.part == 'ctest-lookups' }}", "matrix.part == 'ctest-lookup' }}", "ctest-lookup"),
                ("matrix.part == 'scripts-python' }}\n        run: |\n          status=0\n          python3 tools/check", "matrix.part == 'scripts_python' }}\n        run: |\n          status=0\n          python3 tools/check", "scripts_python")):
            with self.subTest(word):
                problems = self.judged(old, new, problems_of_the_legs)
                self.assertTrue(any("which no leg has" in p and word in p for p in problems), problems)
                self.assertTrue(any("runs no test" in p or "runs in no leg" in p or "no test step is selected" in p for p in problems), problems)

    def test_a_step_that_waits_for_a_build_that_is_not_there_never_runs(self):
        for job, configure in (("linux_tests", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"), ("macos_tests", '-DCMAKE_PREFIX_PATH="$HOME/sdl2"')):
            with self.subTest(job):
                build = "\n\n      - name: Build the game, the server and the test programs\n        id: build\n"
                problems = self.judged(configure + build, configure + build.replace("id: build", "id: building"), problems_of_the_legs)
                self.assertTrue(any(p.startswith(job) and "asks for the step 'build', which is not before it" in p for p in problems), problems)
        problems = self.judged("steps.build_e2e.conclusion == 'success' && matrix.part == 'scripts' }}", "steps.build_e2e.conclusion == 'succeeded' && matrix.part == 'scripts' }}", problems_of_the_legs)
        self.assertTrue(any("linux_tests: the test step 'Opaque-box E2E runner' runs in no leg" in p for p in problems), problems)
        problems = self.judged("        id: build_e2e\n        if: ${{ matrix.part == 'scripts' }}\n", "        id: build_e2e\n        if: ${{ matrix.part == 'script' }}\n", problems_of_the_legs)
        self.assertTrue(any("runs in no leg" in p for p in problems), problems)

    def test_a_test_step_for_two_parts_or_none_is_found(self):
        problems = self.judged("!cancelled() && matrix.part == 'python' }}", "!cancelled() && matrix.part != 'ctest-rest' }}", problems_of_the_legs)
        self.assertTrue(any("more than one part" in p for p in problems), problems)

    def test_two_legs_with_one_name_or_one_artifact_are_found(self):
        problems = self.judged("name: linux-test-logs-${{ matrix.id }}", "name: linux-test-logs-${{ matrix.part }}", problems_of_the_names)
        self.assertTrue(any("two legs upload the artifact 'linux-test-logs-python'" in p for p in problems), problems)
        problems = self.judged("name: macos-test-logs-${{ matrix.id }}", "name: macos-test-logs", problems_of_the_names)
        self.assertTrue(any("two legs upload the artifact 'macos-test-logs'" in p for p in problems), problems)
        problems = self.judged("name: windows-${{ matrix.os }}-test-logs", "name: windows-test-logs", problems_of_the_names)
        self.assertTrue(any("two legs upload the artifact 'windows-test-logs'" in p for p in problems), problems)
        problems = self.judged('- id: ctest-rest\n            name: "ctest: all the other tests"', '- id: ctest-rest\n            name: "ctest: the next longest tests"', problems_of_the_legs)
        self.assertTrue(any("own name" in p for p in problems), problems)
        problems = self.judged("- id: python-3\n", "- id: python-2\n", problems_of_the_legs)
        self.assertTrue(any("own id" in p for p in problems), problems)

    def test_a_required_name_that_is_missing_twice_or_renamed_is_found(self):
        problems = self.judged("    name: Linux (GCC)\n", "    name: Linux (GCC 13)\n", problems_of_the_names)
        self.assertTrue(any("'Linux (GCC)' is reported 0 times" in p for p in problems), problems)
        problems = self.judged("    name: Web (Emscripten, Docker image)\n", "    name: Linux (GCC)\n", problems_of_the_names)
        self.assertTrue(any("two checks are named 'Linux (GCC)'" in p for p in problems), problems)
        problems = self.judged("    name: Windows (MSVC 2022)\n", "    name: Windows (MSVC 2022 x64)\n", problems_of_the_names)
        self.assertTrue(any("'Windows (MSVC 2022)' is reported 0 times" in p for p in problems), problems)
        problems = self.judged("    name: Windows tests (${{ matrix.name }})\n", "    name: Windows (${{ matrix.name }})\n", problems_of_the_names)
        self.assertTrue(any("'Windows (MSVC 2022)' is reported" in p or "two checks are named 'Windows (MSVC 2022)'" in p for p in problems), problems)

    def test_a_small_job_that_misses_a_job_of_its_system_or_does_not_always_run_is_found(self):
        problems = self.judged("    needs: [changes, linux_tests]\n", "    needs: changes\n", problems_of_the_aggregates)
        self.assertTrue(any(p.startswith("linux: needs") for p in problems), problems)
        problems = self.judged("    needs: [changes, web_image, web_stack]\n", "    needs: [changes, web_image]\n", problems_of_the_aggregates)
        self.assertTrue(any(p.startswith("web: needs") for p in problems), problems)
        problems = self.judged("    needs: [changes, macos_tests]\n    if: ${{ always() }}", "    needs: [changes, macos_tests]\n    if: ${{ success() }}", problems_of_the_aggregates)
        self.assertTrue(any("runs only when its needs passed" in p for p in problems), problems)
        problems = self.judged("          RESULT: ${{ needs.macos_tests.result }}", "          RESULT: ${{ needs.macos.result }}", problems_of_the_aggregates)
        self.assertTrue(any("the result of macos_tests is not looked at" in p for p in problems), problems)
        problems = self.judged("          IMAGE: ${{ needs.web_image.result }}\n", "", problems_of_the_aggregates)
        self.assertTrue(any("the result of web_image is not looked at" in p for p in problems), problems)

    @unittest.skipUnless(shutil.which("bash"), "bash is needed")
    def test_a_small_job_that_passes_what_it_should_not_is_found(self):
        mac = 'echo "the job that decides what runs: $CHANGES (light: ${LIGHT:-nothing}); the macOS test jobs: $RESULT"\n'
        for old, new, word in (
                (mac + '          [ "$CHANGES" = success ] || { echo "the job that decides what runs did not succeed: this check fails"; exit 1; }\n', mac, "macos passes when the first job is 'failure'"),
                (mac + '          [ "$CHANGES" = success ] || { echo "the job that decides what runs did not succeed: this check fails"; exit 1; }\n          [ "$RESULT" = success ] && exit 0\n          [ "$RESULT" = skipped ] && [ "$LIGHT" = true ]',
                 mac + '          [ "$CHANGES" = success ] || { echo "the job that decides what runs did not succeed: this check fails"; exit 1; }\n          [ "$RESULT" = success ] && exit 0\n          [ "$RESULT" = skipped ] && [ "$LIGHT" != false ]',
                 "macos passes when the first job is 'success' ('')"),
                ('echo "the job that decides what runs: $CHANGES; the Linux test jobs: $RESULT"\n          [ "$CHANGES" = success ] || { echo "the job that decides what runs did not succeed: this check fails"; exit 1; }\n',
                 'echo "the job that decides what runs: $CHANGES; the Linux test jobs: $RESULT"\n', "linux passes when the first job is 'failure'"),
                ('[ "$IMAGE" = success ] && [ "$STACK" = success ] ||', '[ "$IMAGE" = success ] ||', "web passes when the first job is 'success'"),
                ('[ "$IMAGE" = success ] && [ "$STACK" = success ] ||', '[ "$STACK" = success ] ||', "web passes when the first job is 'success'"),
                ('[ "$RESULT" = success ] && exit 0\n          [ "$RESULT" = skipped ] && [ "$LIGHT" = true ] && { echo "skipped for a change of pages, documents and page tests only"; exit 0; }\n          echo "a Windows test job failed, was cancelled or did not run: this check fails"\n          exit 1\n\n  windows_2026:',
                 '[ "$RESULT" = success ] && exit 0\n          [ "$RESULT" = skipped ] && { echo "skipped for a change of pages, documents and page tests only"; exit 0; }\n          echo "a Windows test job failed, was cancelled or did not run: this check fails"\n          exit 1\n\n  windows_2026:',
                 "windows_2022 passes when the first job is 'success' ('false')")):
            with self.subTest(word):
                problems = self.judged(old, new, lambda doc: problems_of_how_the_aggregates_decide(doc, thorough=False))
                self.assertTrue(any(p.startswith(word) for p in problems), (word, problems[:3]))

    def test_a_swallowed_failure_is_found(self):
        for old, new, word in (
                ("    name: Windows tests (${{ matrix.name }})\n", "    name: Windows tests (${{ matrix.name }})\n    continue-on-error: true\n", "windows_tests: continue-on-error on the job"),
                ("      - name: Did both web jobs pass?\n", "      - name: Did both web jobs pass?\n        continue-on-error: true\n", "web: 'Did both web jobs pass?' has continue-on-error"),
                ('python3 tests/scripts/run_python_tests.py --shard "$SLICE" || status=1', 'python3 tests/scripts/run_python_tests.py --shard "$SLICE" || true', "linux_tests: 'Repository checks"),
                ("          exit $status\n\n      - name: Keep the test logs of a failed run\n        if: failure()\n        uses: actions/upload-artifact@v7\n        with:\n          name: macos-test-logs", "          true\n\n      - name: Keep the test logs of a failed run\n        if: failure()\n        uses: actions/upload-artifact@v7\n        with:\n          name: macos-test-logs", "macos_tests: ")):
            with self.subTest(word):
                problems = self.judged(old, new, problems_of_swallowed_failures)
                self.assertTrue(any(p.startswith(word) for p in problems), (word, problems))

    def test_a_ctest_that_may_find_nothing_is_found(self):
        flag = " --no-tests=error"
        for where, old in (("linux_tests", '--parallel 4 --no-tests=error -R "$CTEST_ALONE"'), ("linux_tests", '--parallel 4 --no-tests=error -E "${CTEST_ALONE}|${CTEST_LONG}"'),
                           ("macos_tests", "--parallel 4 --no-tests=error\n\n      - name: Tool self-tests (map_sweep, bot_arena, replay_tool, lvl_tool) and the community maps\n        if: ${{ !cancelled() && steps.build.conclusion == 'success' && matrix.part == 'scripts-python' }}"),
                           ("windows_tests", "--parallel 4 --no-tests=error\n\n      - name: Tool self-tests (map_sweep, bot_arena, replay_tool, lvl_tool)\n")):
            with self.subTest(where):
                problems = self.judged(old, old.replace(flag, "", 1), problems_of_ctest)
                self.assertTrue(any(p.startswith(where) and "a ctest without --no-tests=error" in p for p in problems), problems)
        problems = problems_of_ctest(self.doc)
        self.assertEqual(problems, [])

    def test_a_light_answer_that_skips_the_wrong_jobs_is_found(self):
        mac_if = "    # Skipped for a change of web pages, documents and page tests only (the job `changes`); it does not run when that job failed (the small job `macos` then fails).\n    if: ${{ !cancelled() && needs.changes.result == 'success' && needs.changes.outputs.light != 'true' }}"
        win_if = "    # of its name, which is why the required names are carried by the small jobs below, and not by these legs. It does not run when `changes` failed (the small jobs then fail).\n    if: ${{ !cancelled() && needs.changes.result == 'success' && needs.changes.outputs.light != 'true' }}"
        for job, job_if in (("macos_tests", mac_if), ("windows_tests", win_if)):
            problems = self.judged(job_if, job_if.replace(" && needs.changes.outputs.light != 'true'", ""), problems_of_the_light_checks)
            self.assertTrue(any("%s runs when the first job says 'true'" % job in p for p in problems), problems)
            problems = self.judged(job_if, job_if.replace(" && needs.changes.result == 'success'", ""), problems_of_the_light_checks)
            self.assertTrue(any("%s runs when the first job says '' with the result failure" % job in p for p in problems), problems)
            problems = self.judged(job_if, job_if.replace("!cancelled() && needs.changes.result == 'success' && ", "success() && "), problems_of_the_light_checks)
            self.assertEqual(problems, [], "(success() is the status that a job whose need succeeded has: the same answers)")
        problems = self.judged("    needs: [changes, windows_tests]\n    if: ${{ always() }}\n    runs-on: ubuntu-latest\n    timeout-minutes: 5\n    steps:\n      - name: Did every Windows job pass (or was it right to skip them)?\n        env:\n          CHANGES: ${{ needs.changes.result }}\n          LIGHT: ${{ needs.changes.outputs.light }}\n          RESULT: ${{ needs.windows_tests.result }}\n        run: |\n          echo \"the job that decides what runs: $CHANGES (light: ${LIGHT:-nothing}); the Windows test jobs: $RESULT\"\n          [ \"$CHANGES\" = success ] || { echo \"the job that decides what runs did not succeed: this check fails\"; exit 1; }\n          [ \"$RESULT\" = success ] && exit 0\n          [ \"$RESULT\" = skipped ] && [ \"$LIGHT\" = true ] && { echo \"skipped for a change of pages, documents and page tests only\"; exit 0; }\n          echo \"a Windows test job failed, was cancelled or did not run: this check fails\"\n          exit 1\n\n  windows_2026:",
                                "    needs: [changes, windows_tests]\n    if: ${{ success() }}\n    runs-on: ubuntu-latest\n    timeout-minutes: 5\n    steps:\n      - name: Did every Windows job pass (or was it right to skip them)?\n        env:\n          CHANGES: ${{ needs.changes.result }}\n          LIGHT: ${{ needs.changes.outputs.light }}\n          RESULT: ${{ needs.windows_tests.result }}\n        run: |\n          echo \"the job that decides what runs: $CHANGES (light: ${LIGHT:-nothing}); the Windows test jobs: $RESULT\"\n          [ \"$CHANGES\" = success ] || { echo \"the job that decides what runs did not succeed: this check fails\"; exit 1; }\n          [ \"$RESULT\" = success ] && exit 0\n          [ \"$RESULT\" = skipped ] && [ \"$LIGHT\" = true ] && { echo \"skipped for a change of pages, documents and page tests only\"; exit 0; }\n          echo \"a Windows test job failed, was cancelled or did not run: this check fails\"\n          exit 1\n\n  windows_2026:",
                                problems_of_the_light_checks)
        self.assertTrue(any(p.startswith("windows_2022 is skipped when the first job says") for p in problems), problems)
        problems = self.judged("      - uses: actions/checkout@v7\n\n      - name: Restore the compiler cache\n        uses: actions/cache@v6\n        with:\n          path: .sccache\n", "      - uses: actions/checkout@v7\n\n      - name: Restore the compiler cache\n        if: ${{ needs.changes.outputs.light != 'true' }}\n        uses: actions/cache@v6\n        with:\n          path: .sccache\n", problems_of_the_light_checks) if False else []


class TheCiMachinesHavePyYAML(unittest.TestCase):
    def test_the_linux_jobs_of_the_ci_check_the_structure_of_the_workflow(self):
        """The checks above are skipped without PyYAML, which would leave the legs unchecked without a word: on the Linux machines of the CI it must be there (the job installs python3-yaml)."""
        if os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_OS") == "Linux":
            self.assertIsNotNone(yaml, "PyYAML is missing on a Linux machine of the CI: the structure of ci.yml is not checked")


if __name__ == "__main__":
    unittest.main()
