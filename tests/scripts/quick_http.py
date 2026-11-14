"""HTTP servers for the tests that start at once and never ask the network for a name.

http.server.HTTPServer.server_bind() calls socket.getfqdn(host), a reverse lookup of the address it just bound. On a machine whose resolver is slow or unreachable (a macOS runner of
the CI: seconds for each start, 45 s for five tests of one file) that is the whole cost of a fake server, for a name that nobody reads. These two classes bind the same way and set the
two attributes by hand, so a test that serves on 127.0.0.1 touches no resolver. This is the one home of the fix: a test that needs an HTTP server on a loopback address uses one of
them (tests/scripts/test_quick_http.py keeps them honest).
"""
import atexit
import http.server
import os
import shutil
import socket
import socketserver
import tempfile
import threading
import time


class QuickHTTPServer(http.server.HTTPServer):
    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name = self.server_address[0]          # (an address as given, never looked up)
        self.server_port = self.server_address[1]


class QuickThreadingHTTPServer(socketserver.ThreadingMixIn, QuickHTTPServer):
    daemon_threads = True


# Ports for the servers that the tests start. A number that was found free and handed back (bind to port 0, read the number, close) is free for anybody until the server binds it: a test that
# runs beside this one (the classes of run_python_tests.py run side by side, and so do the parts of test_ants_server.sh) can be given the same number, and the second server does not start.
# So a process takes its numbers from a RANGE OF ITS OWN: PORT_SPAN numbers of PORT_FIRST - PORT_FIRST + PORT_SLOTS * PORT_SPAN - 1 (26000 - 31999, above the range of test_ants_server.sh and below
# the numbers that the system gives to outgoing connections: 32768 and up on Linux, 49152 and up on macOS and Windows). The range is claimed with an atomic mkdir of a folder named for the
# slot in the temporary folder (it holds the process id and is removed when the process ends; one of a process that no longer runs, or older than PORT_STALE seconds, is taken over), free_port
# counts up inside the range and skips a number that something on this machine holds (a bind test), and the range wraps at its end.
PORT_FIRST = 26000
PORT_SLOTS = 40
PORT_SPAN = 150
PORT_STALE = 4 * 3600
_port_lock = threading.Lock()
_port_range = None          # (first number, next offset, claimed folder)


def _process_gone(pid):
    if os.name == "nt":
        return False        # (os.kill would end a process there: only the age decides)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    except OSError:
        return False
    return False


def _release_port_range():
    if _port_range is not None and _port_range[2]:
        shutil.rmtree(_port_range[2], ignore_errors=True)


def _claim_port_range():
    root = os.path.join(tempfile.gettempdir(), "ants_test_ports")
    os.makedirs(root, exist_ok=True)
    for slot in range(PORT_SLOTS):
        folder = os.path.join(root, "slot%d" % slot)
        take = None
        try:
            os.mkdir(folder)
            take = folder
        except OSError:
            try:
                with open(os.path.join(folder, "pid"), encoding="ascii") as f:
                    pid = int(f.read().strip())
                old = _process_gone(pid) or time.time() - os.path.getmtime(folder) > PORT_STALE
            except (OSError, ValueError):
                old = False     # (a folder that has no process id yet is being claimed)
            if old:
                try:
                    os.mkdir(folder + ".take")      # (one process at a time takes a slot over, after it has read the process id again)
                except OSError:
                    continue
                try:
                    with open(os.path.join(folder, "pid"), encoding="ascii") as f:
                        if int(f.read().strip()) == pid:
                            take = folder
                except (OSError, ValueError):
                    pass
                finally:
                    os.rmdir(folder + ".take")
        if take:
            with open(os.path.join(take, "pid"), "w", encoding="ascii") as f:
                f.write(str(os.getpid()))
            atexit.register(_release_port_range)
            return PORT_FIRST + slot * PORT_SPAN, take
    return PORT_FIRST + (os.getpid() % PORT_SLOTS) * PORT_SPAN, ""      # (more processes than slots: the range is shared, the bind test still skips a number that is held)


def free_port():
    """The next number of this process's range that nothing on this machine holds."""
    global _port_range
    with _port_lock:
        if _port_range is None:
            first, folder = _claim_port_range()
            _port_range = (first, 0, folder)
        first, nxt, folder = _port_range
        for i in range(PORT_SPAN):
            offset = (nxt + i) % PORT_SPAN
            with socket.socket() as s:
                try:
                    s.bind(("", first + offset))
                except OSError:
                    continue
            _port_range = (first, (offset + 1) % PORT_SPAN, folder)
            return first + offset
    raise OSError("no free port in %d - %d" % (first, first + PORT_SPAN - 1))
