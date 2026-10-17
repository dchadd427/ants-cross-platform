"""HTTP servers for the tests that start at once and never ask the network for a name.

http.server.HTTPServer.server_bind() calls socket.getfqdn(host), a reverse lookup of the address it just bound. On a machine whose resolver is slow or unreachable (a macOS runner of
the CI: seconds for each start, 45 s for five tests of one file) that is the whole cost of a fake server, for a name that nobody reads. These two classes bind the same way and set the
two attributes by hand, so a test that serves on 127.0.0.1 touches no resolver. This is the one home of the fix: a test that needs an HTTP server on a loopback address uses one of
them (tests/scripts/test_quick_http.py keeps them honest).
"""
import http.server
import socketserver


class QuickHTTPServer(http.server.HTTPServer):
    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name = self.server_address[0]          # (an address as given, never looked up)
        self.server_port = self.server_address[1]


class QuickThreadingHTTPServer(socketserver.ThreadingMixIn, QuickHTTPServer):
    daemon_threads = True
