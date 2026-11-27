#!/usr/bin/env python3
"""The web server's logs are kept for 14 days and then deleted (docs/BUILD_AND_RUN.md "The web server's logs"; the Legal page says so).

docker/web-log.sh is run for real, with a command in place of the web server: what the command writes to its output and its error output reaches the files of the log folder and none of it stays in the
container's own output (which Docker keeps without an end); the live file is cut into a file of its own every tick; a file whose last line is more than 14 days old is deleted and a younger one is not; a live file
that a stopped container left behind with old lines in it is cut off at the start, so that it does not take the new lines in and is deleted with its age; the status of the command is the script's.
"""
import os
import re
import shutil
import signal
import subprocess
import tempfile
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "docker", "web-log.sh")
DAY = 24 * 3600


def read(*path):
    with open(os.path.join(REPO, *path), encoding="utf-8") as f:
        return f.read()


def files_of(folder):
    if not os.path.isdir(folder):
        return []
    return sorted(n for n in os.listdir(folder) if n.startswith("web.") and n.endswith(".log"))


def lines_of(folder):
    """Every line of the log, the cut files in the order of their names and the live file last."""
    out = []
    names = files_of(folder)
    for name in [n for n in names if n != "web.log"] + [n for n in names if n == "web.log"]:
        with open(os.path.join(folder, name), encoding="utf-8") as f:
            out += f.read().splitlines()
    return out


class TheLogFolder(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants-web-log-")
        self.folder = os.path.join(self.tmp, "log")
        self.procs = []

    def tearDown(self):
        for p in self.procs:
            try:
                os.killpg(p.pid, signal.SIGKILL)       # (the two loops of the script go on after the command ends, as they do in a container until it is stopped)
            except OSError:
                pass
            p.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def start(self, command, tick=600):
        out, err = os.path.join(self.tmp, "container.out"), os.path.join(self.tmp, "container.err")
        env = dict(os.environ, ANTS_LOG_DIR=self.folder, ANTS_LOG_TICK=str(tick))
        with open(out, "w") as o, open(err, "w") as e:
            proc = subprocess.Popen(["sh", SCRIPT] + command, stdout=o, stderr=e, env=env, start_new_session=True)
        self.procs.append(proc)
        self.container_out, self.container_err = out, err
        return proc

    def wait_for(self, what, seconds=20):
        end = time.time() + seconds
        while time.time() < end:
            if what():
                return True
            time.sleep(0.05)
        return what()

    def age(self, name, days):
        path = os.path.join(self.folder, name)
        then = time.time() - days * DAY
        os.utime(path, (then, then))

    def put(self, name, text, days):
        os.makedirs(self.folder, exist_ok=True)
        with open(os.path.join(self.folder, name), "w", encoding="utf-8") as f:
            f.write(text)
        self.age(name, days)

    def test_the_output_and_the_error_output_of_the_command_are_kept_and_the_containers_own_output_stays_empty(self):
        proc = self.start(["sh", "-c", "echo '203.0.113.9 [t] \"GET /\" 200'; echo 'client: 203.0.113.9, request: \"GET /x\"' >&2"])
        self.assertEqual(proc.wait(timeout=20), 0)
        self.assertTrue(self.wait_for(lambda: len(lines_of(self.folder)) == 2), lines_of(self.folder))
        self.assertEqual(sorted(lines_of(self.folder)), ['203.0.113.9 [t] "GET /" 200', 'client: 203.0.113.9, request: "GET /x"'])
        for path in (self.container_out, self.container_err):
            with open(path, encoding="utf-8") as f:
                self.assertEqual(f.read(), "", "what the web server writes never reaches Docker's log: %s" % path)

    def test_a_line_that_ends_the_output_without_a_newline_is_kept_too(self):
        self.start(["sh", "-c", "printf 'last'"])
        self.assertTrue(self.wait_for(lambda: lines_of(self.folder) == ["last"]), lines_of(self.folder))

    def test_the_status_of_the_command_is_the_status_of_the_script(self):
        self.assertEqual(self.start(["sh", "-c", "exit 3"]).wait(timeout=20), 3)
        self.assertEqual(self.start(["sh", "-c", "exit 0"]).wait(timeout=20), 0)

    def test_the_live_file_is_cut_into_a_file_of_its_own_every_tick_and_no_line_is_lost(self):
        self.start(["sh", "-c", "for i in 1 2 3 4 5 6 7 8; do echo line$i; sleep 0.6; done"], tick=1)
        expected = ["line%d" % i for i in range(1, 9)]
        self.assertTrue(self.wait_for(lambda: os.path.isdir(self.folder) and lines_of(self.folder) == expected), lines_of(self.folder))
        self.assertTrue(self.wait_for(lambda: len([n for n in files_of(self.folder) if n != "web.log"]) >= 2), files_of(self.folder))
        for name in files_of(self.folder):
            self.assertRegex(name, r"^web\.(\d{8}T\d{6}Z(\.\d+)?\.)?log$", "a cut file is named by the second of the cut")

    def test_a_file_is_deleted_when_its_last_line_is_more_than_14_days_old_and_not_before_and_nothing_else_is_touched(self):
        self.put("web.20240101T000000Z.log", "old\n", 14.01)
        self.put("web.20240102T000000Z.log", "older\n", 30)
        self.put("web.20240103T000000Z.1.log", "also old\n", 99)
        self.put("web.20240104T000000Z.log", "young\n", 13.99)
        self.put("web.20240105T000000Z.log", "yesterday\n", 1)
        self.put("notes.txt", "mine\n", 99)
        self.put("web.other", "mine\n", 99)
        self.start(["true"]).wait(timeout=20)
        self.assertEqual(files_of(self.folder), ["web.20240104T000000Z.log", "web.20240105T000000Z.log"])
        self.assertEqual(sorted(n for n in os.listdir(self.folder) if not n.startswith("web.2")), [".pipe", "notes.txt", "web.other"])

    def test_the_deletion_runs_every_tick_for_a_container_that_stays_up(self):
        self.start(["sleep", "30"], tick=1)
        self.assertTrue(self.wait_for(lambda: os.path.isdir(self.folder)))
        self.put("web.20240104T000000Z.log", "old when the container is already running\n", 15)
        self.put("web.20240105T000000Z.log", "young\n", 2)
        self.assertTrue(self.wait_for(lambda: files_of(self.folder) == ["web.20240105T000000Z.log"], 20), files_of(self.folder))

    def test_a_live_file_that_a_stopped_container_left_is_cut_off_at_the_start_and_does_not_take_the_new_lines_in(self):
        self.put("web.log", "a line of a week ago\n", 7)
        self.start(["sh", "-c", "echo new"]).wait(timeout=20)
        self.assertTrue(self.wait_for(lambda: "new" in lines_of(self.folder)), lines_of(self.folder))
        names = files_of(self.folder)
        self.assertEqual(len(names), 2, names)
        with open(os.path.join(self.folder, "web.log"), encoding="utf-8") as f:
            self.assertEqual(f.read(), "new\n")
        old = [n for n in names if n != "web.log"][0]
        with open(os.path.join(self.folder, old), encoding="utf-8") as f:
            self.assertEqual(f.read(), "a line of a week ago\n")

    def test_a_live_file_of_a_container_that_was_stopped_for_more_than_14_days_is_deleted(self):
        self.put("web.log", "a line of three weeks ago\n", 21)
        self.start(["true"]).wait(timeout=20)
        self.assertEqual(files_of(self.folder), [])

    def test_the_folder_is_private(self):
        self.start(["true"]).wait(timeout=20)
        self.assertEqual(os.stat(self.folder).st_mode & 0o077, 0, "the folder is made by the script with the mode of its umask")


class TheDaysAndTheWordsAreOne(unittest.TestCase):
    def test_the_script_keeps_14_days_and_the_image_runs_the_web_server_through_it(self):
        text = read("docker", "web-log.sh")
        self.assertEqual(re.findall(r"(?m)^KEEP_DAYS=(\d+)$", text), ["14"])
        dockerfile = read("Dockerfile")
        runner = dockerfile[dockerfile.index(" AS runner"):]
        self.assertIn("COPY docker/web-log.sh /etc/nginx/ants-web-log.sh\n", runner)
        self.assertEqual(re.findall(r"(?m)^CMD .*$", runner), ['CMD ["sh", "/etc/nginx/ants-web-log.sh", "/docker-entrypoint.sh", "nginx", "-g", "daemon off;"]'])


if __name__ == "__main__":
    unittest.main()
