"""tests/scripts/quick_http.py: the loopback HTTP servers of the tests start without a name lookup, and no test file builds a stdlib one.

  - starting either class never calls socket.getfqdn, gethostbyaddr, gethostbyname or getaddrinfo with a name (a lookup of an address literal by getaddrinfo is local and is let through)
  - they still serve a request, and the threading one serves two at once
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


if __name__ == "__main__":
    unittest.main()
