#!/usr/bin/env python3
"""The one-shot helper ants-results-init of docker-compose.stack.yml and docker-compose.server.yml (run by ./run_tests.sh and the CI; no docker needed).

The game server runs as the user ants with a read-only root file system and no capabilities, so it cannot change the owner of its results volume. A volume that existed before the image gave
/results to ants keeps its old owner (root) and /results/<room>.json cannot be written. The helper runs before every start of the server, as root with the one capability CHOWN, and gives the top
folder /results the owner of the folder that the server made inside it. These tests read both files as text (the staging file has no helper and must not get one) and RUN the helper's command
on scratch folders: the owner follows replays, restart, ants.db in that order; a new (empty) volume is left alone; no recursion; the exit status is 0 in every case.
Broken copies: tests/data/results_init_mutants.json (run with tools/mutate.py).
"""
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


def script(text):
    """The shell script of the helper's command, as the shell gets it ($$ is $)."""
    import json
    m = re.search(r'(?m)^    command: (\[.*\])\s*$', service(text, HELPER))
    return json.loads(m.group(1))[0].replace("$$", "$")


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
            s = service(read(name), "ants-server")
            self.assertRegex(s, r"(?m)^    depends_on:\n      %s:\n        condition: service_completed_successfully$" % HELPER, name)

    def test_it_uses_the_build_of_the_server_so_that_a_new_volume_is_filled_from_the_image(self):
        for name in FILES:
            text = read(name)
            self.assertIn("    build: &server-build\n", text, name)
            self.assertRegex(service(text, HELPER), r"(?m)^    build: \*server-build$", name)
            self.assertNotIn("image: debian", service(text, HELPER), name)

    def test_the_script_never_recurses_and_always_exits_zero(self):
        for name in FILES:
            sh = script(read(name))
            self.assertNotRegex(sh, r"chown\s+(-\w*R|--recursive)", name)
            self.assertNotIn("-R", sh, name)
            self.assertNotIn("chmod", sh, name)
            self.assertNotIn("rm ", sh, name)
            self.assertEqual(sh.count("chown"), 1, name)
            self.assertTrue(sh.rstrip().endswith("exit 0"), name)
            self.assertEqual(sh.count("exit "), 2, name)
            self.assertEqual(len(re.findall(r"exit (?!0)", sh)), 0, name)
            self.assertIn("for d in replays restart ants.db;", sh, name)
            self.assertIn("chown --reference=/results/$d /results", sh, name)
        self.assertEqual(script(read(FILES[0])), script(read(FILES[1])))


@unittest.skipUnless(shutil.which("sh") and shutil.which("chown"), "needs sh and chown")
class TheScriptRuns(unittest.TestCase):
    def run_script(self, results):
        sh = script(read(FILES[0])).replace("/results", results)
        return subprocess.run(["sh", "-c", sh], capture_output=True, text=True, timeout=30)

    def test_a_new_empty_volume_is_left_alone_and_the_exit_is_zero(self):
        with tempfile.TemporaryDirectory() as d:
            before = os.stat(d)
            r = self.run_script(d)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("nothing to do", r.stdout)
            self.assertEqual(len(r.stdout.strip().splitlines()), 1)
            self.assertEqual((os.stat(d).st_uid, os.stat(d).st_gid), (before.st_uid, before.st_gid))
            self.assertEqual(os.listdir(d), [])

    def test_a_missing_folder_does_not_fail(self):
        r = self.run_script("/nonexistent-results-folder")
        self.assertEqual(r.returncode, 0, r.stderr)

    @unittest.skipUnless(hasattr(os, "geteuid") and os.geteuid() == 0, "changing an owner needs root")
    def test_the_top_folder_follows_the_first_folder_that_exists_and_nothing_below_changes(self):
        for present, order in ((["replays"], "replays"), (["restart"], "restart"), (["ants.db"], "ants.db"), (["restart", "ants.db"], "restart")):
            with tempfile.TemporaryDirectory() as d:
                for i, name in enumerate(present):
                    p = os.path.join(d, name)
                    os.mkdir(p) if name != "ants.db" else open(p, "w").close()
                    os.chown(p, 1234 + i, 4321 + i)
                inner = os.path.join(d, "restart" if "restart" in present else "x")
                if "restart" in present:
                    open(os.path.join(inner, "file"), "w").close()
                    os.chown(os.path.join(inner, "file"), 0, 0)
                r = self.run_script(d)
                self.assertEqual(r.returncode, 0, r.stderr)
                ref = os.stat(os.path.join(d, order))
                self.assertEqual((os.stat(d).st_uid, os.stat(d).st_gid), (ref.st_uid, ref.st_gid), present)
                if "restart" in present:
                    self.assertEqual(os.stat(os.path.join(inner, "file")).st_uid, 0)
                self.assertEqual(len(r.stdout.strip().splitlines()), 1)

    @unittest.skipUnless(hasattr(os, "geteuid") and os.geteuid() == 0, "changing an owner needs root")
    def test_a_folder_that_belongs_to_root_keeps_root(self):
        with tempfile.TemporaryDirectory() as d:
            os.mkdir(os.path.join(d, "replays"))
            os.chown(d, 1234, 4321)
            r = self.run_script(d)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(os.stat(d).st_uid, 0)


if __name__ == "__main__":
    unittest.main()
