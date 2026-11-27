#!/usr/bin/env python3
"""The web server's logs are kept for 14 days and then deleted (docs/BUILD_AND_RUN.md "The web server's logs"; the Legal page says so).

docker/web-log.sh is run for real, with a stand-in for the web server that does what nginx does with its log (it opens the file once, writes to it, and opens it again by its name when it is sent SIGUSR1): the web
server is the script's own process and writes to the file with nothing between them, so that a slow or stopped cutting never holds a request up; the live file is renamed to a file of its own every tick, the web
server is told after a real cut only, and the lines after it land in the new file with no line lost or twice; a failing step of the cutting stops nothing; a file whose last line is more than 14 days old is deleted
and a younger one is not; a live file that a stopped container left behind with old lines in it is cut off at the start, so that it does not take the new lines in and is deleted with its age. Where the program nginx
is installed the same is done with the real web server and real requests, and docker/nginx.conf is held to write its logs where the script keeps them.
"""
import os
import re
import shutil
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.request

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "docker", "web-log.sh")
DAY = 24 * 3600

# What nginx does with its log, as far as the rotation goes: the file is opened once (append) and kept open, a line is written for each line that is read from the input, and SIGUSR1 opens the file again by its
# name. It says "pid N" when it is up, "ok" for every line it wrote and "reopened" for every time it opened the file again; "burst N" writes N lines at once and "text" is written to its error output.
STAND_IN = r"""
import os, signal, sys
path = sys.argv[1]
fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o600)
def reopen(*_):
    global fd
    new = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o600)
    os.close(fd)
    fd = new
    print("reopened", flush=True)
signal.signal(signal.SIGUSR1, reopen)
print("pid", os.getpid(), flush=True)
print("what the web server says on its own", file=sys.stderr, flush=True)
for line in sys.stdin:
    line = line.rstrip("\n")
    if line.startswith("burst "):
        os.write(fd, b"".join(b"%07d a request of the burst of lines that fills a pipe many times over\n" % i for i in range(int(line.split()[1]))))
    else:
        os.write(fd, line.encode() + b"\n")
    print("ok", flush=True)
"""


def read(*path):
    with open(os.path.join(REPO, *path), encoding="utf-8") as f:
        return f.read()


def files_of(folder):
    if not os.path.isdir(folder):
        return []
    return sorted(n for n in os.listdir(folder) if n.startswith("web.") and n.endswith(".log"))


def cut_files(folder):
    """The cut files, the oldest first (by the time of their last line: the names of two cuts in one second are not in the order of the cuts)."""
    return sorted((n for n in files_of(folder) if n != "web.log"), key=lambda n: (os.stat(os.path.join(folder, n)).st_mtime_ns, n))


def lines_of(folder):
    """Every line of the log, the cut files oldest first and the live file last."""
    out = []
    for name in cut_files(folder) + [n for n in files_of(folder) if n == "web.log"]:
        with open(os.path.join(folder, name), encoding="utf-8", errors="replace") as f:
            out += f.read().splitlines()
    return out


def loop_of(pid):
    """The processes of the script's loop that cuts and deletes: the children of the script's process (which is the web server by now, whose workers are children too) that still run the script."""
    done = subprocess.run(["ps", "-A", "-o", "pid=", "-o", "ppid=", "-o", "args="], capture_output=True, text=True)
    rows = [line.split(None, 2) for line in done.stdout.splitlines() if line.strip()]
    return [int(a) for a, b, args in rows if int(b) == pid and "web-log.sh" in args]


class Server:
    """The script, run with the stand-in for the web server, and what the stand-in says."""

    def __init__(self, test, tick=600, env=None, command=None):
        self.err_path = os.path.join(test.tmp, "container.err.%d" % len(test.procs))
        err = open(self.err_path, "wb")
        environment = dict(os.environ, ANTS_LOG_DIR=test.folder, ANTS_LOG_TICK=str(tick))
        environment.update(env or {})
        self.said = []
        self.proc = subprocess.Popen(["sh", SCRIPT] + (command or [sys.executable, "-c", STAND_IN, os.path.join(test.folder, "web.log")]), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err,
                                     env=environment, start_new_session=True)
        err.close()
        test.procs.append(self.proc)
        self.reader = threading.Thread(target=self.read_output, daemon=True)
        self.reader.start()

    def read_output(self):
        for line in self.proc.stdout:
            self.said.append(line.decode().rstrip("\n"))

    def count(self, word):
        return len([s for s in self.said if s.split(" ")[0] == word])

    def request(self, line, wait=20):
        """One line to the log of the web server, and its word that the line is written."""
        before = self.count("ok")
        self.proc.stdin.write((line + "\n").encode())
        self.proc.stdin.flush()
        end = time.time() + wait
        while time.time() < end and self.count("ok") == before:
            time.sleep(0.002)
        assert self.count("ok") > before, "the web server did not write %r (it said %r)" % (line, self.said)


class TheLogFolder(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants-web-log-")
        self.folder = os.path.join(self.tmp, "log")
        self.procs = []

    def tearDown(self):
        for p in self.procs:
            try:
                os.killpg(p.pid, signal.SIGKILL)       # (the loop of the script goes on after the command ends, as it does in a container until it is stopped; it may be stopped by a test, and a stopped process is killed too)
            except OSError:
                pass
            p.wait()
            for pipe in (p.stdin, p.stdout):
                try:
                    pipe.close()
                except (OSError, AttributeError):
                    pass
        shutil.rmtree(self.tmp, ignore_errors=True)

    def wait_for(self, what, seconds=20):
        end = time.time() + seconds
        while time.time() < end:
            if what():
                return True
            time.sleep(0.05)
        return what()

    def put(self, name, text, days):
        os.makedirs(self.folder, exist_ok=True)
        path = os.path.join(self.folder, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        then = time.time() - days * DAY
        os.utime(path, (then, then))

    def test_the_command_is_the_process_of_the_script_and_gets_the_signal_that_stops_a_container_and_its_output_is_the_containers(self):
        server = Server(self)
        self.assertTrue(self.wait_for(lambda: server.count("pid") == 1), server.said)
        self.assertEqual(int(server.said[0].split()[1]), server.proc.pid, "the script is replaced by the command (exec), so that the web server is the container's first process and gets its signals")
        self.assertTrue(self.wait_for(lambda: os.path.getsize(server.err_path) > 0))
        with open(server.err_path, encoding="utf-8") as f:
            self.assertEqual(f.read(), "what the web server says on its own\n", "nothing stands between the command and the container's output")
        server.proc.send_signal(signal.SIGTERM)
        self.assertEqual(server.proc.wait(timeout=5), -signal.SIGTERM)

    def test_the_status_of_the_command_is_the_status_of_the_script(self):
        self.assertEqual(Server(self, command=["sh", "-c", "exit 3"]).proc.wait(timeout=20), 3)
        self.assertEqual(Server(self, command=["sh", "-c", "exit 0"]).proc.wait(timeout=20), 0)

    def test_a_line_is_in_the_live_file_at_once_and_nothing_goes_through_the_script(self):
        server = Server(self)
        server.request("203.0.113.9 [t] \"GET /\" 200")
        self.assertEqual(lines_of(self.folder), ['203.0.113.9 [t] "GET /" 200'])

    def test_a_slow_or_absent_cutting_never_holds_up_the_web_server(self):
        """The old way was a pipe into a shell loop: a loop that is slow or gone fills the pipe (64 KB) and the web server waits at its next line. Here the loop is stopped altogether, and 20 times that many bytes are written."""
        server = Server(self, tick=0.3)
        self.assertTrue(self.wait_for(lambda: server.count("pid") == 1))
        loops = loop_of(server.proc.pid)
        self.assertTrue(loops, "the script's loop is a process of its own")
        for pid in loops:
            os.kill(pid, signal.SIGSTOP)
        server.request("burst 20000", wait=30)
        self.assertEqual(len(lines_of(self.folder)), 20000)
        self.assertGreater(os.path.getsize(os.path.join(self.folder, "web.log")), 20 * 64 * 1024)
        time.sleep(1.2)
        self.assertEqual(cut_files(self.folder), [], "a loop that is stopped cuts nothing, and the web server does not care")
        server.request("and one more")
        self.assertEqual(lines_of(self.folder)[-1], "and one more")

    def test_the_live_file_is_cut_into_a_file_of_its_own_every_tick_and_the_next_lines_are_in_the_new_file_with_none_lost(self):
        server = Server(self, tick=0.6)                      # (the lines that are written right after a cut cannot be cut again before they are looked at)
        expected = []
        for round_ in range(3):
            before = len(cut_files(self.folder))
            for i in range(3):
                line = "round%d-line%d" % (round_, i)
                expected.append(line)
                server.request(line)
            self.assertTrue(self.wait_for(lambda: len(cut_files(self.folder)) == before + 1 and server.count("reopened") == before + 1), (cut_files(self.folder), server.said))
            with open(os.path.join(self.folder, "web.log"), encoding="utf-8") as f:
                self.assertEqual(f.read(), "", "after the cut the web server has the new file, which is empty until its next line")
        server.request("after the last cut")
        expected.append("after the last cut")
        self.assertEqual(lines_of(self.folder), expected)
        with open(os.path.join(self.folder, "web.log"), encoding="utf-8") as f:
            self.assertEqual(f.read(), "after the last cut\n", "the lines after a cut land in the new file")
        for name in files_of(self.folder):
            self.assertRegex(name, r"^web\.(\d{8}T\d{6}Z(\.\d+)?\.)?log$", "a cut file is named by the second of the cut")

    def test_a_cut_while_the_web_server_is_writing_loses_no_line(self):
        server = Server(self, tick=0.2)
        for i in range(80):
            server.request("line%03d" % i)
            time.sleep(0.01)
        self.assertGreaterEqual(len(cut_files(self.folder)), 3)
        self.assertEqual(lines_of(self.folder), ["line%03d" % i for i in range(80)])

    def test_the_web_server_is_told_after_a_real_cut_only(self):
        server = Server(self, tick=0.3)
        time.sleep(0.9)
        self.assertEqual(server.count("reopened"), 0, "a quiet site's log is not cut, and nothing is told to the web server")
        server.request("a visit")
        self.assertTrue(self.wait_for(lambda: server.count("reopened") == 1), server.said)
        time.sleep(0.9)
        self.assertEqual(server.count("reopened"), 1)

    def test_a_cutting_that_fails_stops_nothing(self):
        broken = os.path.join(self.tmp, "broken")
        os.makedirs(broken)
        for tool in ("mv", "find", "date"):
            with open(os.path.join(broken, tool), "w") as f:
                f.write("#!/bin/sh\nexit 1\n")
            os.chmod(os.path.join(broken, tool), 0o755)
        server = Server(self, tick=0.3, env={"PATH": broken + os.pathsep + os.environ["PATH"]})
        server.request("before")
        time.sleep(1.5)
        self.assertIsNone(server.proc.poll(), "the web server runs on")
        server.request("after the failed cuts")
        self.assertEqual(lines_of(self.folder), ["before", "after the failed cuts"])
        self.assertEqual(server.count("reopened"), 0)

    def test_a_file_is_deleted_when_its_last_line_is_more_than_14_days_old_and_not_before_and_nothing_else_is_touched(self):
        self.put("web.20240101T000000Z.log", "old\n", 14.01)
        self.put("web.20240102T000000Z.log", "older\n", 30)
        self.put("web.20240103T000000Z.1.log", "also old\n", 99)
        self.put("web.20240104T000000Z.log", "young\n", 13.99)
        self.put("web.20240105T000000Z.log", "yesterday\n", 1)
        self.put("notes.txt", "mine\n", 99)
        self.put("web.other", "mine\n", 99)
        Server(self, command=["true"]).proc.wait(timeout=20)
        self.assertEqual(files_of(self.folder), ["web.20240104T000000Z.log", "web.20240105T000000Z.log"])
        self.assertEqual(sorted(n for n in os.listdir(self.folder) if not n.startswith("web.2")), ["notes.txt", "web.other"])

    def test_the_deletion_runs_every_tick_for_a_container_that_stays_up(self):
        server = Server(self, tick=0.3)
        self.assertTrue(self.wait_for(lambda: os.path.isdir(self.folder)))
        self.put("web.20240104T000000Z.log", "old when the container is already running\n", 15)
        self.put("web.20240105T000000Z.log", "young\n", 2)
        self.assertTrue(self.wait_for(lambda: cut_files(self.folder) == ["web.20240105T000000Z.log"], 20), files_of(self.folder))
        self.assertIsNone(server.proc.poll())

    def test_a_live_file_that_a_stopped_container_left_is_cut_off_at_the_start_and_does_not_take_the_new_lines_in(self):
        self.put("web.log", "a line of a week ago\n", 7)
        server = Server(self)
        server.request("new")
        names = files_of(self.folder)
        self.assertEqual(len(names), 2, names)
        with open(os.path.join(self.folder, "web.log"), encoding="utf-8") as f:
            self.assertEqual(f.read(), "new\n")
        old = [n for n in names if n != "web.log"][0]
        with open(os.path.join(self.folder, old), encoding="utf-8") as f:
            self.assertEqual(f.read(), "a line of a week ago\n")
        self.assertEqual(server.count("reopened"), 0, "nothing runs yet at the start that could be told")

    def test_a_live_file_of_a_container_that_was_stopped_for_more_than_14_days_is_deleted(self):
        self.put("web.log", "a line of three weeks ago\n", 21)
        Server(self, command=["true"]).proc.wait(timeout=20)
        self.assertEqual(files_of(self.folder), [])

    def test_the_folder_can_be_entered_by_the_web_servers_workers_and_listed_by_no_one_else(self):
        Server(self, command=["true"]).proc.wait(timeout=20)
        self.assertEqual(stat.S_IMODE(os.stat(self.folder).st_mode), 0o711, "the workers open the new file by its name after a cut, so they must be able to enter the folder (the files are the web server's own, and private)")


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the real web server was NOT run through docker/web-log.sh (tests/scripts/test_web_log.py)")
class TheRealWebServer(unittest.TestCase):
    """The program nginx, started by the script as the image starts it, with real requests: every request is answered and logged once, in order, across the cuts; and stopped cutting holds up none."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants-web-log-nginx-")
        self.folder = os.path.join(self.tmp, "log")
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        self.port = sock.getsockname()[1]
        sock.close()
        t = self.tmp
        for sub in ("b", "p", "f", "u", "s"):
            os.makedirs(os.path.join(t, "tmp", sub))
        os.makedirs(os.path.join(t, "empty"))
        conf = os.path.join(t, "nginx.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write(("user root;\n" if hasattr(os, "geteuid") and os.geteuid() == 0 else "") + "daemon off;\npid %(t)s/nginx.pid;\nerror_log %(t)s/main.log warn;\nworker_processes 3;\nevents { worker_connections 256; }\nhttp {\n  client_body_temp_path %(t)s/tmp/b;\n  proxy_temp_path %(t)s/tmp/p;\n"
                    "  fastcgi_temp_path %(t)s/tmp/f;\n  uwsgi_temp_path %(t)s/tmp/u;\n  scgi_temp_path %(t)s/tmp/s;\n  access_log off;\n  error_log %(log)s/web.log notice;\n"
                    "  log_format ants_noquery '$remote_addr \"$request_method $uri\" $status';\n  server {\n    listen 127.0.0.1:%(port)d;\n    access_log %(log)s/web.log ants_noquery;\n"
                    "    location /ok/ { return 200 \"ok\\n\"; }\n    location / { root %(t)s/empty; }\n  }\n}\n" % {"t": t, "log": self.folder, "port": self.port})
        self.proc = None
        self.conf = conf

    def tearDown(self):
        if self.proc:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except OSError:
                pass
            self.proc.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def start(self, tick):
        env = dict(os.environ, ANTS_LOG_DIR=self.folder, ANTS_LOG_TICK=str(tick))
        self.proc = subprocess.Popen(["sh", SCRIPT, shutil.which("nginx"), "-p", self.tmp + "/", "-c", self.conf], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env, start_new_session=True)
        for _ in range(200):
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.5).close()
                return
            except OSError:
                if self.proc.poll() is not None:
                    message = self.proc.stderr.read().decode(errors="replace")
                    if "unknown directive" in message:
                        self.skipTest("this nginx lacks a module that the test needs: " + message.strip()[:200])
                    self.fail("nginx would not start: " + message[:600])
                time.sleep(0.05)
        self.fail("nginx did not come up")

    def get(self, path):
        try:
            with urllib.request.urlopen("http://127.0.0.1:%d%s" % (self.port, path), timeout=10) as r:
                return r.status
        except urllib.error.HTTPError as e:
            return e.code

    def test_every_request_is_answered_and_logged_once_across_the_cuts_and_the_error_log_is_in_the_same_files(self):
        self.start(tick=0.5)
        wanted = []
        end = time.time() + 3.2
        i = 0
        while time.time() < end:
            path, status = ("/ok/%05d" % i, 200) if i % 10 else ("/missing/%05d" % i, 404)
            self.assertEqual(self.get(path), status)
            wanted.append(path)
            i += 1
            time.sleep(0.01)
        time.sleep(0.5)
        self.assertGreaterEqual(len(cut_files(self.folder)), 4, files_of(self.folder))
        seen = [re.search(r'"GET (\S+)"', line).group(1) for line in lines_of(self.folder) if re.search(r'"GET \S+" \d+$', line)]
        self.assertEqual(sorted(seen), sorted(wanted), "each request is one line of the log, whichever file it is in: none lost and none twice")
        self.assertEqual(len([line for line in lines_of(self.folder) if "[error]" in line and "/missing/" in line]), len([p for p in wanted if "/missing/" in p]), "the error log of a request is in the same files")
        self.assertTrue(os.path.exists(os.path.join(self.folder, "web.log")), "the web server has a new live file after a cut (four cuts, each of a live file that held lines: it wrote to the new file every time)")

    def test_a_cutting_that_is_stopped_holds_up_no_request(self):
        self.start(tick=0.5)
        for pid in loop_of(self.proc.pid):
            os.kill(pid, signal.SIGSTOP)
        started = time.time()
        for _ in range(2500):                                    # (about 2500 lines of 60 bytes and more than 2500 of the error log: far more than a pipe of 64 KB takes)
            self.assertEqual(self.get("/missing/stalls"), 404)
        self.assertLess(time.time() - started, 60)
        self.assertGreater(os.path.getsize(os.path.join(self.folder, "web.log")), 64 * 1024)
        self.assertEqual(cut_files(self.folder), [])

    def test_nginx_is_the_first_process_of_the_script_and_stops_at_the_signal_that_stops_a_container(self):
        self.start(tick=600)
        master = int(open(os.path.join(self.tmp, "nginx.pid")).read())
        self.assertEqual(master, self.proc.pid, "the script is replaced by nginx (exec): it is the container's first process, and it gets the stop signal")
        self.proc.send_signal(signal.SIGQUIT)
        self.assertEqual(self.proc.wait(timeout=10), 0)


class TheDaysAndTheWordsAreOne(unittest.TestCase):
    def test_the_script_keeps_14_days_and_the_image_runs_the_web_server_through_it(self):
        text = read("docker", "web-log.sh")
        self.assertEqual(re.findall(r"(?m)^KEEP_DAYS=(\d+)$", text), ["14"])
        dockerfile = read("Dockerfile")
        runner = dockerfile[dockerfile.index(" AS runner"):]
        self.assertIn("COPY docker/web-log.sh /etc/nginx/ants-web-log.sh\n", runner)
        self.assertEqual(re.findall(r"(?m)^CMD .*$", runner), ['CMD ["sh", "/etc/nginx/ants-web-log.sh", "/docker-entrypoint.sh", "nginx", "-g", "daemon off;"]'])

    def test_every_log_of_the_web_server_is_the_file_that_the_script_cuts_and_none_is_dockers_own_which_has_no_end(self):
        script = read("docker", "web-log.sh")
        folder = re.search(r"(?m)^DIR=\$\{ANTS_LOG_DIR:-([^}]+)\}$", script).group(1)
        name = re.search(r'(?m)^live="\$DIR/([^"]+)"$', script).group(1)
        conf = re.sub(r"(?m)#.*$", "", read("docker", "nginx.conf"))
        logs = re.findall(r"(?m)^\s*(access_log|error_log)\s+(\S+)", conf)
        self.assertGreater(len(logs), 10)
        for directive, target in logs:
            if directive == "access_log" and target == "off;":
                continue
            self.assertEqual(target.rstrip(";"), "%s/%s" % (folder, name), "%s: the script cuts and deletes that file, and no other" % directive)
        self.assertIn("error_log %s/%s notice;" % (folder, name), conf, "the error log of every request that no door sets its own for")


if __name__ == "__main__":
    unittest.main()
