"""tests/scripts/quick_http.py: the loopback HTTP servers of the tests start without a name lookup, and no test file builds a stdlib one.

  - starting either class never calls socket.getfqdn, gethostbyaddr, gethostbyname or getaddrinfo with a name (a lookup of an address literal by getaddrinfo is local and is let through)
  - they still serve a request, and the threading one serves two at once
  - quick_http.free_port: processes side by side are never given the same number (each has a range of its own below 32768), a number that something holds is skipped, and no tests/scripts/*.py
    has a free_port of its own that binds port 0 and gives the number back
  - no tests/scripts/test_*.py makes or derives from a http.server.HTTPServer or ThreadingHTTPServer itself (the stdlib one asks for the name of the machine when it binds); found in the syntax tree
"""
import ast
import glob
import http.client
import os
import socket
import sys
import threading
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import quick_http     # noqa: E402

import http.server    # noqa: E402


class Hello(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"hello"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def forbidden(name):
    def refuse(*args, **kwargs):
        raise AssertionError("%s%r: a name lookup" % (name, args))
    return refuse


class NoLookup(unittest.TestCase):
    def serve(self, cls):
        with mock.patch.object(socket, "getfqdn", forbidden("getfqdn")), mock.patch.object(socket, "gethostbyaddr", forbidden("gethostbyaddr")), \
                mock.patch.object(socket, "gethostbyname", forbidden("gethostbyname")):
            server = cls(("127.0.0.1", 0), Hello)
        self.addCleanup(server.server_close)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        return server

    def get(self, server):
        conn = http.client.HTTPConnection("127.0.0.1", server.server_address[1], timeout=5)
        conn.request("GET", "/")
        answer = conn.getresponse()
        return answer.status, answer.read()

    def test_the_plain_server_binds_without_asking_for_a_name_and_serves(self):
        server = self.serve(quick_http.QuickHTTPServer)
        self.assertEqual(server.server_port, server.server_address[1])
        self.assertEqual(self.get(server), (200, b"hello"))

    def test_the_threading_server_binds_without_asking_for_a_name_and_serves(self):
        server = self.serve(quick_http.QuickThreadingHTTPServer)
        self.assertTrue(server.daemon_threads)
        self.assertEqual(self.get(server), (200, b"hello"))

    def test_the_stdlib_server_does_ask(self):      # (this is why the classes exist: if the stdlib stopped asking, they could go)
        with mock.patch.object(socket, "getfqdn", side_effect=AssertionError("asked")):
            with self.assertRaises(AssertionError):
                http.server.HTTPServer(("127.0.0.1", 0), Hello)


def stdlib_server_uses(path):
    """Where a file builds, or derives from, a stdlib HTTP server (found in the syntax tree: a call over several lines, an alias of the import and a subclass are all seen)."""
    with open(path, encoding="utf-8") as f:
        tree = ast.parse(f.read(), path)
    names = {"HTTPServer", "ThreadingHTTPServer"}
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom) and node.module == "http.server":
            names |= {alias.asname for alias in node.names if alias.asname and alias.name in ("HTTPServer", "ThreadingHTTPServer")}
    def named(expr):
        return (expr.id if isinstance(expr, ast.Name) else expr.attr if isinstance(expr, ast.Attribute) else "") in names
    found = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Call) and named(node.func):
            found.append(node.lineno)
        if isinstance(node, ast.ClassDef) and any(named(base) for base in node.bases) and not any(isinstance(n, ast.FunctionDef) and n.name == "server_bind" for n in node.body):
            found.append(node.lineno)
    return found


class NoTestBuildsTheStdlibServer(unittest.TestCase):
    def test_every_test_file_uses_the_quick_classes(self):
        found = []
        for path in sorted(glob.glob(os.path.join(HERE, "test_*.py"))):
            if os.path.abspath(path) != os.path.abspath(__file__):         # (this file builds the stdlib one on purpose, to show that it asks)
                found += ["%s:%d" % (os.path.basename(path), n) for n in stdlib_server_uses(path)]
        self.assertEqual(found, [], "use tests/scripts/quick_http.py: the stdlib server looks up the name of the machine when it binds")

    def test_the_guard_sees_what_it_is_for(self):      # (the guard is not vacuous: calls over several lines, aliases and subclasses are found, the quick classes are not)
        import tempfile
        text = ("import http.server\nfrom http.server import HTTPServer as H\nclass S(http.server.ThreadingHTTPServer):\n    pass\n"
                "a = http.server.HTTPServer(\n    (\"127.0.0.1\", 0), None)\nb = H((\"127.0.0.1\", 0), None)\nc = quick_http.QuickHTTPServer((\"127.0.0.1\", 0), None)\n"
                "class Q(http.server.HTTPServer):\n    def server_bind(self):\n        pass\n")
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "sample.py")
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            self.assertEqual(stdlib_server_uses(path), [3, 5, 7])


PICKER = "import sys; sys.path.insert(0, sys.argv[1]); import quick_http; print(' '.join(str(quick_http.free_port()) for _ in range(40)), flush=True); sys.stdin.readline()"


class FreePort(unittest.TestCase):
    def test_processes_side_by_side_are_never_given_the_same_number(self):
        import subprocess
        procs = [subprocess.Popen([sys.executable, "-c", PICKER, HERE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True) for _ in range(6)]
        picks = []
        for proc in procs:      # (each keeps its range until it is told to end: they run side by side)
            picks.append([int(n) for n in proc.stdout.readline().split()])
        for proc in procs:
            proc.communicate("\n", timeout=60)
            self.assertEqual(proc.returncode, 0)
        everything = [n for each in picks for n in each]
        self.assertEqual(len(everything), 240)
        self.assertEqual(len(set(everything)), 240, "a number was given twice")
        self.assertTrue(all(quick_http.PORT_FIRST <= n < 32768 for n in everything), "a number is outside the range below the system's own")

    def test_one_process_counts_up_inside_its_range_and_skips_a_number_that_is_held(self):
        first = quick_http.free_port()
        held = socket.socket()
        self.addCleanup(held.close)
        held.bind(("", first + 1))
        after = [quick_http.free_port() for _ in range(3)]
        self.assertNotIn(first + 1, after)
        self.assertEqual(after[0], first + 2)
        self.assertEqual(len(set(after)), 3)

    def test_the_range_wraps_and_the_numbers_stay_free(self):
        seen = [quick_http.free_port() for _ in range(quick_http.PORT_SPAN + 5)]
        self.assertEqual(len(set(seen)), quick_http.PORT_SPAN)
        self.assertEqual(max(seen) - min(seen), quick_http.PORT_SPAN - 1)

    def test_no_file_has_a_free_port_of_its_own_that_gives_the_number_back(self):
        found = []
        for path in sorted(glob.glob(os.path.join(HERE, "*.py"))):
            if os.path.basename(path) == "quick_http.py":
                continue
            with open(path, encoding="utf-8") as f:
                tree = ast.parse(f.read())
            for node in ast.walk(tree):
                if isinstance(node, ast.FunctionDef) and node.name == "free_port" and any(isinstance(n, ast.Attribute) and n.attr == "getsockname" for n in ast.walk(node)):
                    found.append("%s:%d" % (os.path.basename(path), node.lineno))
        self.assertEqual(found, [], "use quick_http.free_port: a number that was given back is free for another process")


if __name__ == "__main__":
    unittest.main()
