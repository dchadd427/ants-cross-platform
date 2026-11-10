#!/usr/bin/env python3
"""The one-shot helper ants-results-init of docker-compose.stack.yml and docker-compose.server.yml (run by ./run_tests.sh and the CI; no docker needed).

The game server runs as the user ants with a read-only root file system and no capabilities, so it cannot change the owner of its results volume. A volume that existed before the image gave
/results to ants keeps its old owner (root) and /results/<room>.json cannot be written. The helper runs before every start of the server, from the server's own image, as root with the one
capability CHOWN, and gives the top folder /results to the user ants (`chown ants: /results`, never -R, under `timeout 20`). These tests read both files as text (the staging file has no helper
and must not get one), check the raw command (a lone `$` would be filled in by Compose as an empty text: the shell variable of the script is written `$$`) and RUN the script on scratch folders
(with the user nobody in the place of ants): a root-owned folder is handed over, an owner that is right is left alone, nothing below changes, the exit status is 0 in every case.
Broken copies: tests/data/results_init_mutants.json (run with tools/mutate.py).
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FILES = ("docker-compose.stack.yml", "docker-compose.server.yml")
HELPER = "ants-results-init"


def read(name):
    with open(os.path.join(REPO, name), encoding="utf-8") as f:
        return f.read()


def service(text, name):
    """The lines of one service, comments left out."""
    lines = text.splitlines()
    start = next(i for i, l in enumerate(lines) if l == "  %s:" % name)
    end = next((i for i in range(start + 1, len(lines)) if re.match(r"^  [a-z]|^\S", lines[i])), len(lines))
    return "\n".join(l for l in lines[start:end] if not l.lstrip().startswith("#"))


def raw_command(text):
    """The `command: [...]` line of the helper as written in the file."""
    return re.search(r'(?m)^    command: (\[.*\])\s*$', service(text, HELPER)).group(1)


def script(text):
    """The shell script of the helper's command, as the shell gets it ($$ is $)."""
    return json.loads(raw_command(text))[0].replace("$$", "$")


class TheService(unittest.TestCase):
    def test_both_files_have_the_helper_and_the_staging_file_does_not(self):
        for name in FILES:
            self.assertIn("\n  %s:\n" % HELPER, read(name), name)
        self.assertNotIn(HELPER, read("docker-compose.staging.yml"))

    def test_it_is_a_one_shot_that_runs_as_root_with_one_capability_and_nothing_else(self):
        for name in FILES:
            s = service(read(name), HELPER)
            self.assertRegex(s, r'(?m)^    restart: "no"$', name)
            self.assertRegex(s, r'(?m)^    user: "0"$', name)
            self.assertRegex(s, r"(?m)^    read_only: true$", name)
            self.assertRegex(s, r"(?m)^    cap_drop: \[ALL\]$", name)
            self.assertRegex(s, r"(?m)^    cap_add: \[CHOWN\]$", name)
            self.assertRegex(s, r'(?m)^    security_opt: \["no-new-privileges:true"\]$', name)
            self.assertRegex(s, r"(?m)^    pids_limit: 16$", name)
            self.assertRegex(s, r"(?m)^    network_mode: none$", name)
            self.assertNotIn("ports:", s, name)
            self.assertNotIn("networks:", s, name)
            self.assertNotIn("privileged", s, name)
            self.assertEqual(re.findall(r"(?m)^      - (.*)$", s), ["ants-server-results:/results"], name)
            self.assertEqual(len(re.findall(r'(?m)^    user: ', s)), 1, name)
            code = "\n".join(l for l in read(name).splitlines() if not l.lstrip().startswith("#"))
            self.assertEqual(code.count("entrypoint:"), 1, name)          # (the server keeps the image's own entrypoint)
            self.assertIn('    entrypoint: ["/bin/sh", "-c"]\n', s + "\n", name)

    def test_only_this_service_runs_as_root_and_the_server_is_not_widened(self):
        for name in FILES:
            text = read(name)
            code = "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))
            self.assertEqual(len(re.findall(r"(?m)^    user:", code)), 1, name)
            self.assertNotIn("cap_add", service(text, "ants-server"), name)
            self.assertNotIn("user:", service(text, "ants-server"), name)
            self.assertEqual(code.count("cap_add"), 1, name)
            for line in ("read_only: true", "cap_drop: [ALL]", 'security_opt: ["no-new-privileges:true"]', "pids_limit: 64"):
                self.assertIn(line, service(text, "ants-server"), name)

    def test_the_server_waits_for_it_to_complete(self):
        for name in FILES:
            s = service(read(name), "ants-server") + "\n"
            self.assertRegex(s, r"(?m)^    depends_on:\n      %s:\n        condition: service_completed_successfully\n(?!        )" % HELPER, name)         # (and nothing more under it: no `required: false`)

    def test_it_uses_the_build_of_the_server_so_that_the_user_exists_and_a_new_volume_is_filled_from_the_image(self):
        for name in FILES:
            text = read(name)
            self.assertIn("    build: &server-build\n", text, name)
            self.assertRegex(service(text, HELPER), r"(?m)^    build: \*server-build$", name)
            self.assertNotIn("image: debian", service(text, HELPER), name)

    def test_the_server_image_makes_the_user_ants_and_gives_it_the_results_folder(self):
        self.assertRegex(read("Dockerfile.server"), r"useradd --system [^\n]* ants && \\\n\s+mkdir -p /maps /results && chown ants /results")

    def test_the_command_has_no_lone_dollar_for_compose_to_fill_in(self):
        for name in FILES:
            raw = raw_command(read(name))
            self.assertEqual(re.findall(r"(?<!\$)\$(?!\$)", raw), [], name)         # (a single $ is replaced by Compose with an empty text: the script would then do nothing and still exit 0)
            self.assertEqual(raw.count("$$"), 1, name)
            self.assertEqual(raw.count("$$(stat -c %U /results)"), 1, name)

    def test_the_script_hands_the_top_folder_to_ants_never_recurses_and_always_exits_zero(self):
        for name in FILES:
            sh = script(read(name))
            self.assertNotRegex(sh, r"chown\s+(-\w*R|--recursive)", name)
            self.assertNotIn("-R", sh, name)
            self.assertNotIn("--reference", sh, name)
            self.assertNotIn("chmod", sh, name)
            self.assertNotRegex(sh, r"\brm\b", name)
            self.assertEqual(sh.count("chown"), 1, name)
            self.assertIn("timeout 20 chown ants: /results &&", sh, name)
            self.assertIn('[ "$(stat -c %U /results)" = ants ]', sh, name)
            self.assertTrue(sh.rstrip().endswith("; fi; exit 0"), name)
            self.assertEqual(sh.count("exit "), 1, name)
        self.assertEqual(script(read(FILES[0])), script(read(FILES[1])))


ROOT = hasattr(os, "geteuid") and os.geteuid() == 0


@unittest.skipUnless(shutil.which("sh") and shutil.which("chown") and shutil.which("timeout") and shutil.which("stat"), "needs sh, chown, timeout and stat")
class TheScriptRuns(unittest.TestCase):
    USER = "nobody"            # (stands in for ants, which only the image has)

    def run_script(self, results, user=None):
        sh = script(read(FILES[0])).replace("/results", results).replace("= ants ]", "= %s ]" % (user or self.USER)).replace("chown ants:", "chown %s:" % (user or self.USER))
        return subprocess.run(["sh", "-c", sh], capture_output=True, text=True, timeout=60)

    def test_a_missing_folder_does_not_fail_and_says_so_in_one_line(self):
        r = self.run_script("/nonexistent-results-folder")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("could not change", r.stdout)
        self.assertEqual(len(r.stdout.strip().splitlines()), 1)

    @unittest.skipUnless(ROOT, "changing an owner needs root")
    def test_a_root_folder_is_handed_over_and_nothing_below_it_changes(self):
        import pwd
        uid = pwd.getpwnam(self.USER).pw_uid
        with tempfile.TemporaryDirectory() as d:
            os.mkdir(os.path.join(d, "restart"))
            f = os.path.join(d, "restart", "file")
            open(f, "w").close()
            os.chown(os.path.join(d, "restart"), 0, 0)
            os.chown(f, 0, 0)
            r = self.run_script(d)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(os.stat(d).st_uid, uid)
            self.assertEqual((os.stat(os.path.join(d, "restart")).st_uid, os.stat(f).st_uid), (0, 0))
            self.assertEqual(r.stdout.strip().splitlines(), ["ants-results-init: %s now belongs to ants" % d])
            again = self.run_script(d)                                                    # (the next start: nothing to do)
            self.assertEqual(again.returncode, 0, again.stderr)
            self.assertIn("already belongs to ants", again.stdout)
            self.assertEqual(os.stat(d).st_uid, uid)

    @unittest.skipUnless(ROOT, "changing an owner needs root")
    def test_a_new_empty_volume_that_belongs_to_ants_is_left_alone(self):
        import pwd
        pw = pwd.getpwnam(self.USER)
        with tempfile.TemporaryDirectory() as d:
            os.chown(d, pw.pw_uid, pw.pw_gid)
            r = self.run_script(d)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(len(r.stdout.strip().splitlines()), 1)
            self.assertIn("nothing to do", r.stdout)
            self.assertEqual(os.listdir(d), [])

    @unittest.skipUnless(ROOT, "changing an owner needs root")
    def test_a_chown_that_fails_says_so_and_still_exits_zero(self):
        with tempfile.TemporaryDirectory() as d:
            r = self.run_script(d, user="no-such-user-here")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("could not change", r.stdout)
            self.assertEqual(os.stat(d).st_uid, 0)


if __name__ == "__main__":
    unittest.main()
