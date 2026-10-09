#!/usr/bin/env python3
"""The CI workflow (.github/workflows/ci.yml) keeps its promises (run by ./run_tests.sh --fast and by the CI).

main is protected: a change merges only through a pull request whose five checks pass, and the checks are named by their JOB NAMES. A renamed or split job would make every
pull request unmergeable, and a deploy job that echoes its secret or runs after a failed job would be worse. Read as text (no YAML library is needed; with PyYAML the structure
is checked too):

  - the five required job names, MSVC 2022 for pull requests, pushes to main and manual runs and MSVC 2026 always, the triggers (no push to other branches)
  - the deploy job: after every other job, only for a push to main or staging, secrets only in `env:` and never in an argument or a message, the file filter and the webhook
    script of tools/ are what it calls; it waits for an idle game server first (tools/deploy_wait.py, its variables, a job limit above the longest wait, a concurrency group of
    its own that lets a newer deploy replace a waiting one)
  - the same suites as ./run_tests.sh: every test program of its table is registered with ctest (which Linux, macOS and Windows run), its other suites (tool self-tests, script
    suites, E2E runner, repository checks) are steps of the Linux and macOS jobs, and ctest has no test that the table lacks
  - the players' maps: the Linux and macOS step that sweeps Community-Maps and runs `tools/community_maps.py verify` (and suite 2.18.1 of the runner) is run here against made-up
    programs: a sweep or a check that fails fails the step or the suite, every finding of a failed sweep is shown, and the other tools of the step still run
  - every action is a first-party `actions/` one at a major version or a commit-pinned one; the sccache download is pinned and checked by its SHA-256
"""
import glob
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
        self.assertEqual(names["windows"], "Windows (${{ matrix.name }})")
        self.assertEqual(names["web"], "Web (Emscripten, Docker image)")
        matrix_lines = [l for l in job_block("windows") if "fromJSON(" in l]
        self.assertEqual(len(matrix_lines), 1)
        self.assertIn('{"name":"MSVC 2022","os":"windows-2022"}', matrix_lines[0])
        self.assertIn('{"name":"MSVC 2026","os":"windows-latest"}', matrix_lines[0])

    def test_msvc_2022_runs_for_pull_requests_main_and_manual_runs_and_2026_always(self):
        expression = [l for l in job_block("windows") if "fromJSON(" in l][0]
        condition, alternatives = expression.split("&&", 1)[0], expression
        lists = re.findall(r"'(\[\{.*?\}\])'", expression)
        self.assertEqual(len(lists), 2)
        only_2026, both = lists
        self.assertIn('"MSVC 2026"', only_2026)
        self.assertNotIn("MSVC 2022", only_2026)
        self.assertIn("MSVC 2022", both)
        self.assertIn("MSVC 2026", both)
        # the short list is chosen for a push to a branch that is not main, and for nothing else
        self.assertIn("github.event_name == 'push'", expression)
        self.assertIn("github.ref != 'refs/heads/main'", expression)
        self.assertRegex(expression, r"fromJSON\(github\.event_name == 'push' && github\.ref != 'refs/heads/main' && '\[")

    def test_every_job_has_a_time_limit_and_the_matrix_does_not_stop_at_the_first_failure(self):
        for job in job_ids():
            self.assertTrue(any(re.match(r"^    timeout-minutes: \d+$", l) for l in job_block(job)), job)
        self.assertTrue(any(l.strip() == "fail-fast: false" for l in job_block("windows")))

    def test_triggers(self):
        head = "\n".join(LINES[:LINES.index("jobs:")])
        self.assertRegex(head, r"(?m)^on:\n  pull_request:\n  push:\n    branches: \[main, staging\]\n  workflow_dispatch:\n")
        self.assertNotRegex(head, r"(?m)^  push:\s*$\n  pull_request")                   # no bare `push:` (every branch)
        self.assertIn("cancel-in-progress: true", head)


class Deploy(unittest.TestCase):
    def test_it_waits_for_every_other_job_and_only_runs_for_a_push_to_main_or_staging(self):
        block = job_block("deploy")
        needs = next(re.match(r"^    needs: \[(.*)\]$", l).group(1) for l in block if l.startswith("    needs:"))
        self.assertEqual(sorted(n.strip() for n in needs.split(",")), sorted(j for j in job_ids() if j != "deploy"))
        condition = next(l for l in block if l.startswith("    if:"))
        self.assertIn("github.event_name == 'push'", condition)
        self.assertIn("refs/heads/main", condition)
        self.assertIn("refs/heads/staging", condition)
        self.assertNotIn("always()", condition)                                           # no status function: it only runs when the jobs it needs succeeded
        self.assertNotIn("failure()", condition)
        self.assertEqual(job_ids()[-1], "deploy")

    def test_secrets_are_only_in_env_and_only_the_two_webhooks(self):
        lines = [(i, l) for i, l in enumerate(LINES) if "secrets." in l and not l.lstrip().startswith("#")]
        self.assertEqual(len(lines), 2, lines)
        names = set()
        for i, line in lines:
            m = re.match(r"^      ([A-Z_]+): \$\{\{ .*secrets\.(PORTAINER_[A-Z_]*WEBHOOK_URL) .*\}\}$", line)
            self.assertIsNotNone(m, "a secret outside the `env:` of the deploy job: " + line)
            names.add(m.group(2))
            above = [l for l in LINES[:i] if re.match(r"^    env:\s*$", l)]
            self.assertTrue(above, "the secret is not under an env: key")
        self.assertEqual(names, {"PORTAINER_WEBHOOK_URL", "PORTAINER_STAGING_WEBHOOK_URL"})
        self.assertTrue(all(i >= LINES.index("  deploy:") for i, _ in lines), "the secrets are used by the deploy job only")

    def test_each_webhook_variable_holds_its_secret_on_its_own_branch_only(self):
        production = next(l for l in LINES if l.lstrip().startswith("PRODUCTION_WEBHOOK:"))
        staging = next(l for l in LINES if l.lstrip().startswith("STAGING_WEBHOOK:"))
        self.assertIn("github.ref == 'refs/heads/main' && secrets.PORTAINER_WEBHOOK_URL || ''", production)
        self.assertIn("github.ref == 'refs/heads/staging' && secrets.PORTAINER_STAGING_WEBHOOK_URL || ''", staging)       # (never `main ? A : B`: an empty A would fall to B)

    def test_the_webhook_variables_are_never_printed_or_given_to_a_command_as_an_argument(self):
        block = "\n".join(job_block("deploy"))
        for line in job_block("deploy"):
            if "WEBHOOK" in line and "secrets." not in line:
                allowed = ('[ -z "${PRODUCTION_WEBHOOK}${STAGING_WEBHOOK}" ]' in line or 'WEBHOOK_URL="${PRODUCTION_WEBHOOK}${STAGING_WEBHOOK}" bash tools/deploy_webhook.sh "$site"' in line
                           or re.match(r"^\s+(PRODUCTION|STAGING)_WEBHOOK: ", line) or line.lstrip().startswith("#") or "tools/deploy_webhook.sh" in line)
                self.assertTrue(allowed, "the webhook variable is used in a way that could print it: " + line)
        self.assertNotRegex(block, r"(?m)^\s*(echo|printf)\b.*WEBHOOK")
        self.assertNotIn("curl", block)                                                    # the call is tools/deploy_webhook.sh's, which keeps the address off every command line
        self.assertNotIn("set -x", block)
        self.assertNotIn("xtrace", block)
        self.assertIn("deploy secret not set: skipped", block)

    def test_it_uses_the_filter_and_the_webhook_script_and_checks_the_tip(self):
        block = "\n".join(job_block("deploy"))
        self.assertIn("python3 tools/deploy_filter.py --base", block)
        self.assertIn("--github-output", block)
        self.assertIn("docker-compose.staging.yml", block)
        self.assertIn("bash tools/deploy_webhook.sh", block)
        self.assertIn("fetch-depth: 0", block)
        self.assertIn("git ls-remote origin", block)
        self.assertIn("actions: read", block)
        self.assertIn("gh run list --workflow ci.yml", block)
        for script in ("tools/deploy_filter.py", "tools/deploy_webhook.sh"):
            self.assertTrue(os.path.isfile(os.path.join(REPO, script)), script)
        # the webhook call needs all three answers: a secret, a change that counts, the tip of the branch
        call = block[block.index("name: Call the deploy webhook"):]
        self.assertIn("steps.secret.outputs.present == 'true'", call)
        self.assertIn("steps.filter.outputs.deploy == 'true'", call)
        self.assertIn("steps.tip.outputs.tip == 'true'", call)

    def test_it_waits_for_an_idle_game_server_before_the_tip_is_checked_and_the_webhook_is_called(self):
        block = "\n".join(job_block("deploy"))
        order = [block.index("name: " + name) for name in ("Is the deploy secret set?", "Did the push change what the site serves or runs?", "Wait for an idle game server",
                                                           "Is this push still the tip of the branch?", "Call the deploy webhook")]
        self.assertEqual(order, sorted(order))                                             # the tip is checked after the wait, which may have taken hours
        wait = block[block.index("name: Wait for an idle game server"):block.index("name: Is this push still the tip of the branch?")]
        self.assertIn("if: steps.secret.outputs.present == 'true' && steps.filter.outputs.deploy == 'true'", wait)
        self.assertIn("PRODUCTION_BUSY_URL: ${{ vars.DEPLOY_BUSY_URL || 'https://beta.playants.org/busy' }}", wait)
        self.assertIn("STAGING_BUSY_URL: ${{ vars.STAGING_BUSY_URL }}", wait)
        self.assertIn("MAX_WAIT_MINUTES: ${{ vars.DEPLOY_MAX_WAIT_MINUTES || '180' }}", wait)
        self.assertIn('python3 tools/deploy_wait.py --label "$site" --url "$url" --max-wait-minutes "$MAX_WAIT_MINUTES"', wait)
        self.assertNotIn("secrets.", wait)                                                 # the addresses are the sites' own: variables, not secrets
        self.assertNotIn("WEBHOOK", wait)
        self.assertTrue(os.path.isfile(os.path.join(REPO, "tools", "deploy_wait.py")))

    def test_the_job_limit_is_above_the_longest_wait_so_that_the_deploy_is_never_cut_off_by_the_limit(self):
        cap = float(re.search(r"^MAX_WAIT_CAP_MINUTES = ([\d.]+)", read(os.path.join(REPO, "tools", "deploy_wait.py")), re.M).group(1))
        limit = int(next(re.match(r"^    timeout-minutes: (\d+)$", l).group(1) for l in job_block("deploy") if l.startswith("    timeout-minutes:")))
        self.assertGreaterEqual(limit, cap + 20)                                           # the wait, the checkout, the filter and the call
        self.assertLessEqual(limit, 360)                                                   # (what a hosted runner allows a job)

    def test_only_the_deploy_job_has_a_concurrency_group_and_it_cancels_a_job_that_waits(self):
        for job in job_ids():
            has = any(l == "    concurrency:" for l in job_block(job))
            self.assertEqual(has, job == "deploy", job)
        block = "\n".join(job_block("deploy"))
        self.assertIn("    concurrency:\n      group: deploy-${{ github.ref }}\n      cancel-in-progress: true\n", block)
        workflow_group = re.search(r"(?m)^concurrency:\n  group: (.+)\n", TEXT).group(1)
        self.assertNotEqual(workflow_group.split("-")[0], "deploy")                        # (the same group would cancel the run that the job is in)
        self.assertTrue(workflow_group.startswith("ci-"))


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

    def test_the_other_suites_of_the_runner_are_steps_of_the_linux_and_macos_jobs(self):
        for job in ("linux", "macos"):
            block = "\n".join(job_block(job))
            for needle in ("build/map_sweep --selftest", "build/map_sweep Community-Maps --ticks 600", "python3 tools/community_maps.py verify Community-Maps", "build/bot_arena --selftest", "build/replay_tool --selftest", "e2e_runner --all", "bash tests/scripts/test_start_game.sh",
                           "tests/scripts/test_ants_server.sh --list-parts", "tests/scripts/test_ants_server.sh --part", "python3 tools/check_version_consistency.py",
                           "python3 tests/scripts/run_python_tests.py", "ctest --test-dir build"):
                self.assertIn(needle, block, "%s: no step runs `%s`" % (job, needle))
        windows = "\n".join(job_block("windows"))
        for needle in ("ctest --test-dir build", "map_sweep.exe --selftest", "bot_arena.exe --selftest", "replay_tool.exe --selftest", "e2e_runner.exe --all"):
            self.assertIn(needle, windows)

    def test_the_commands_of_the_runners_script_and_python_suites_are_what_the_ci_runs(self):
        self.assertIn("python3 tests/scripts/run_python_tests.py", self.runner)
        self.assertIn("python3 tools/check_version_consistency.py", self.runner)
        self.assertIn("test_start_game.sh", self.runner)
        self.assertIn("map_sweep\" --selftest", self.runner)
        self.assertIn("map_sweep\" Community-Maps", self.runner)
        self.assertIn("python3 tools/community_maps.py verify Community-Maps", self.runner)
        self.assertIn("bot_arena\" --selftest", self.runner)
        self.assertIn("replay_tool\" --selftest", self.runner)
        self.assertIn("e2e_runner", self.runner)

    def test_the_web_job_builds_both_images_and_checks_the_stacks_and_the_label(self):
        block = "\n".join(job_block("web"))
        for needle in ("docker build -t ants-beta .", "docker build -f Dockerfile.server -t ants-server .", "nginx -t", "docker compose -f docker-compose.stack.yml config --quiet",
                       "docker compose -f docker-compose.staging.yml config --quiet", "--build-arg ANTS_SITE_LABEL=staging", "(staging)</title>", "! grep -qi staging"):
            self.assertIn(needle, block)


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
        for job in ("linux", "macos"):
            yield job, step_of(job, "tools/community_maps.py verify")[1]

    def test_both_steps_are_named_for_the_community_maps_and_the_windows_one_is_not(self):
        for job in ("linux", "macos"):
            self.assertIn("community maps", step_of(job, "tools/community_maps.py verify")[0], job)
        self.assertNotIn("ommunity", step_of("windows", "map_sweep.exe --selftest")[0])
        self.assertEqual(step_of("linux", "tools/community_maps.py verify")[0], step_of("macos", "tools/community_maps.py verify")[0])
        self.assertEqual(step_of("linux", "tools/community_maps.py verify")[1], step_of("macos", "tools/community_maps.py verify")[1])

    def test_a_step_where_everything_passes_runs_every_tool_and_passes(self):
        self.with_log(sweep_log(0))
        for job, script in self.steps():
            status, out, ran = self.run_script(script)
            self.assertEqual(status, 0, (job, out))
            self.assertEqual([r.split()[0] for r in ran], ["map_sweep", "map_sweep", "community_maps", "bot_arena", "replay_tool"], job)
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
            self.assertEqual([r.split()[0] for r in ran], ["map_sweep", "map_sweep", "community_maps", "bot_arena", "replay_tool"], "the other tools still run: " + job)

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
            self.assertEqual([r.split()[0] for r in ran], ["map_sweep", "map_sweep", "community_maps", "bot_arena", "replay_tool"], job)

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
        block = "\n".join(job_block("windows"))
        self.assertIn("-G Ninja", block)
        self.assertIn("-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl", block)
        self.assertIn("-DCMAKE_CXX_COMPILER_LAUNCHER=sccache", block)
        self.assertIn("-DCMAKE_C_COMPILER_LAUNCHER=sccache", block)
        self.assertIn("ilammy/msvc-dev-cmd@", block)
        code = "\n".join(l for l in job_block("windows") if not l.lstrip().startswith("#"))
        self.assertNotRegex(code, r"Visual Studio 1\d")                                    # (no Visual Studio generator: its projects cannot use a compiler launcher)
        self.assertNotIn("-A x64", code)
        self.assertNotIn("CXXFLAGS", block)                                                # (-MP made cl start its own processes: Ninja already runs the files side by side)
        self.assertNotIn("--config Release", block)
        self.assertNotIn("Release/", block)                                                # Ninja is single-configuration: the programs are not in a Release folder

    def test_the_cache_is_keyed_like_the_linux_ones_and_the_server_is_stopped_before_it_is_saved(self):
        block = "\n".join(job_block("windows"))
        self.assertIn("key: sccache-${{ matrix.os }}-${{ github.sha }}", block)
        self.assertIn("restore-keys: sccache-${{ matrix.os }}-", block)
        self.assertIn("path: .sccache", block)
        self.assertIn("SCCACHE_DIR: ${{ github.workspace }}/.sccache", block)
        self.assertIn("sccache --show-stats", block)
        self.assertIn("sccache --stop-server", block)
        self.assertLess(block.index("sccache --stop-server"), block.index("Test programs (ctest)"))
        linux = "\n".join(job_block("linux"))
        self.assertIn("key: ccache-${{ runner.os }}-${{ github.sha }}", linux)

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


class Documents(unittest.TestCase):
    """What the documents say about the pipeline is what the files do (the documents name them instead of restating them where they can; these are the facts that they must state)."""

    @classmethod
    def setUpClass(cls):
        cls.workflow = read(os.path.join(REPO, "docs", "WORKFLOW.md"))
        cls.agents = read(os.path.join(REPO, "AGENTS.md"))
        cls.testing = read(os.path.join(REPO, "docs", "TESTING.md"))
        cls.staging = read(os.path.join(REPO, "docker-compose.staging.yml"))

    def test_the_required_checks_are_named_where_a_person_decides_about_merging(self):
        for name in ("Linux (GCC)", "macOS (Apple clang)", "Windows (MSVC 2022)", "Windows (MSVC 2026)", "Web (Emscripten, Docker image)"):
            self.assertIn(name, self.workflow)
        self.assertIn("(Linux GCC, macOS Apple clang, Windows MSVC 2022, Windows MSVC 2026, Web)", self.agents)   # (AGENTS.md names the same five in short form, in this order)
        for name in ("Linux (GCC)", "macOS (Apple clang)", "Windows (MSVC 2022)", "Windows (MSVC 2026)", "Web (Emscripten, Docker image)"):   # (the table of docs/TESTING.md has a row for each)
            self.assertIn("| " + name + " |", self.testing)

    def test_the_secrets_and_the_switch_over_are_explained(self):
        for needle in ("PORTAINER_WEBHOOK_URL", "PORTAINER_STAGING_WEBHOOK_URL", "deploy secret not set: skipped", "delete the repository webhook", "refs/heads/staging", "docker-compose.staging.yml"):
            self.assertIn(needle, self.workflow)
        self.assertIn("PORTAINER_WEBHOOK_URL", self.testing)

    def test_the_wait_and_every_variable_of_the_workflow_are_explained(self):
        variables = set(re.findall(r"\bvars\.([A-Z_]+)\b", TEXT))
        self.assertEqual(variables, {"DEPLOY_BUSY_URL", "STAGING_BUSY_URL", "DEPLOY_MAX_WAIT_MINUTES"})
        for variable in variables:
            self.assertIn(variable, self.workflow)
        for needle in ("/busy", "tools/deploy_wait.py", "DEPLOYING ANYWAY", "deploy-<branch>", "cancel-in-progress", "five minutes", "180"):
            self.assertIn(needle, self.workflow)
        self.assertIn("DEPLOY_MAX_WAIT_MINUTES", self.testing)
        self.assertIn("/busy", self.testing)

    def test_the_staging_ports_of_the_document_are_those_of_the_compose_file(self):
        defaults = re.findall(r"\$\{ANTS_STAGING_[A-Z_]+:-(\d+)\}", self.staging)
        self.assertEqual(sorted(defaults), ["19981", "4003", "4004", "4011"])
        for port in defaults:
            self.assertIn(port, self.workflow)
        for variable in re.findall(r"\$\{(ANTS_STAGING_[A-Z_]+):-", self.staging):
            self.assertIn(variable, self.workflow)

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
        self.assertEqual(list(self.doc["jobs"]), ["linux", "macos", "windows", "web", "deploy"])
        self.assertEqual(self.doc["jobs"]["deploy"]["needs"], ["linux", "macos", "windows", "web"])
        self.assertEqual(self.doc["jobs"]["deploy"]["permissions"], {"contents": "read", "actions": "read"})

    def test_the_triggers(self):
        on = self.doc[True] if True in self.doc else self.doc["on"]                       # (YAML 1.1 reads `on` as true)
        self.assertEqual(sorted(on), ["pull_request", "push", "workflow_dispatch"])
        self.assertEqual(on["push"], {"branches": ["main", "staging"]})

    def test_the_deploy_job_waits_cancels_and_has_a_limit_above_the_longest_wait(self):
        job = self.doc["jobs"]["deploy"]
        self.assertEqual(job["concurrency"], {"group": "deploy-${{ github.ref }}", "cancel-in-progress": True})
        self.assertGreaterEqual(job["timeout-minutes"], 320)
        for other in ("linux", "macos", "windows", "web"):
            self.assertNotIn("concurrency", self.doc["jobs"][other])
        names = [step.get("name") for step in job["steps"]]
        self.assertEqual(names.index("Wait for an idle game server") + 1, names.index("Is this push still the tip of the branch?"))
        self.assertGreater(names.index("Wait for an idle game server"), names.index("Did the push change what the site serves or runs?"))

    def test_every_step_of_the_deploy_job_that_needs_the_secret_depends_on_the_answer_of_the_first_step(self):
        steps = self.doc["jobs"]["deploy"]["steps"]
        self.assertEqual(steps[0]["id"], "secret")
        self.assertNotIn("if", steps[0])
        for step in steps[1:]:
            self.assertIn("steps.secret.outputs.present == 'true'", step.get("if", ""), step.get("name", step.get("uses")))


if __name__ == "__main__":
    unittest.main()
