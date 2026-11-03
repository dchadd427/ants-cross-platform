"""A WebSocket client for the hub's tests, written separately from hub/ws.py (a test must not share the code it checks): blocking sockets, masked frames, nothing clever.

frame() builds any client frame, including the broken ones (a missing mask, reserved bits, a length written the long way, a frame that is split); Client speaks to a hub on a
loopback port and reads what comes back.
"""
import base64
import hashlib
import json
import os
import socket
import struct
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
SITE = "https://chat.example.org"


def frame(opcode, payload=b"", fin=True, rsv=0, masked=True, mask=None, length_form=None):
    """The bytes of one client frame. length_form: None (the shortest), 16 or 64 (the length written in that many bits whatever its size)."""
    n = len(payload)
    b0 = (0x80 if fin else 0) | (rsv << 4) | opcode
    mask_bit = 0x80 if masked else 0
    if length_form == 16 or (length_form is None and 126 <= n < 65536):
        head = struct.pack("!BBH", b0, mask_bit | 126, n)
    elif length_form == 64 or (length_form is None and n >= 65536):
        head = struct.pack("!BBQ", b0, mask_bit | 127, n)
    else:
        head = struct.pack("!BB", b0, mask_bit | n)
    if not masked:
        return head + payload
    mask = os.urandom(4) if mask is None else mask
    return head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


def say(text):
    return frame(1, json.dumps({"t": "say", "text": text}).encode("utf-8"))


def handshake_request(origin=SITE, headers=None, path="/hub/ws", key=None, method="GET", drop=()):
    key = key or base64.b64encode(os.urandom(16)).decode("ascii")
    lines = ["%s %s HTTP/1.1" % (method, path), "Host: chat.example.org", "Upgrade: websocket", "Connection: Upgrade", "Sec-WebSocket-Key: " + key, "Sec-WebSocket-Version: 13"]
    if origin is not None:
        lines.append("Origin: " + origin)
    lines = [l for l in lines if l.split(":")[0].lower() not in [d.lower() for d in drop]]
    for name, value in (headers or {}).items():
        lines.append("%s: %s" % (name, value))
    return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1"), key


def read_head(sock, timeout=5.0):
    """The status line, the headers (lower-case names) and any bytes that came after the blank line."""
    sock.settimeout(timeout)
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    head, _, rest = data.partition(b"\r\n\r\n")
    lines = head.decode("latin-1").split("\r\n")
    headers = {}
    for line in lines[1:]:
        name, _, value = line.partition(":")
        headers[name.strip().lower()] = value.strip()
    return lines[0], headers, rest


def http_get(port, path, headers=None, timeout=5.0):
    """(status, body bytes) of a plain GET."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
        extra = "".join("%s: %s\r\n" % kv for kv in (headers or {}).items())
        s.sendall(("GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n%s\r\n" % (path, extra)).encode("latin-1"))
        status, h, rest = read_head(s, timeout)
        body = rest
        while True:
            try:
                chunk = s.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            body += chunk
    return int(status.split()[1]), body


class Response:
    """An HTTP answer: `status` (int), `headers` (a list of (lower-case name, value): a name may come twice), `body` (bytes)."""

    def __init__(self, status, headers, body):
        self.status, self.headers, self.body = status, headers, body

    def get(self, name):
        return next((v for k, v in self.headers if k == name.lower()), None)

    def all(self, name):
        return [v for k, v in self.headers if k == name.lower()]

    def json(self):
        return json.loads(self.body.decode("utf-8"))

    def text(self):
        return self.body.decode("utf-8", "replace")


def request(port, method, path, headers=None, body=b"", timeout=10.0, send_length=True, host="chat.example.org"):
    """One HTTP request to a loopback port and the whole answer (the connection is closed by the server). `headers` is a dict or a list of (name, value) pairs; none is added but Host, Connection
    and, for a body or a POST, Content-Length (send_length=False leaves it out)."""
    pairs = list(headers.items()) if isinstance(headers, dict) else list(headers or [])
    lines = ["%s %s HTTP/1.1" % (method, path), "Host: " + host, "Connection: close"]
    if send_length and (body or method == "POST"):
        lines.append("Content-Length: %d" % len(body))
    lines += ["%s: %s" % kv for kv in pairs]
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
        s.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + body)
        data = b""
        while True:
            try:
                chunk = s.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
    head, _, payload = data.partition(b"\r\n\r\n")
    rows = head.decode("latin-1").split("\r\n")
    out = []
    for row in rows[1:]:
        name, _, value = row.partition(":")
        out.append((name.strip().lower(), value.strip()))
    return Response(int(rows[0].split()[1]), out, payload)


class Jar:
    """The cookies of one browser, as far as the login's tests need: Set-Cookie of an answer stores or (Max-Age=0) removes one, header() is what the browser would send back."""

    def __init__(self):
        self.cookies = {}

    def update(self, response):
        for value in response.all("set-cookie"):
            first = value.split(";")[0]
            name, _, val = first.partition("=")
            if "max-age=0" in value.lower().replace(" ", "") or not val:
                self.cookies.pop(name, None)
            else:
                self.cookies[name] = val

    def header(self):
        return "; ".join("%s=%s" % kv for kv in self.cookies.items())


class Client:
    def __init__(self, port, origin=SITE, headers=None, key=None, path="/hub/ws", drop=(), timeout=5.0, login=None, trailer=b""):
        """Connect and do the handshake; `status` is the HTTP status line, `reply_headers` the answer's headers. login="1001:maple.leaf" sends the test-login header; `trailer` is sent in the same write as the handshake."""
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        self.timeout = timeout
        self.buffer = b""
        headers = dict(headers or {})
        if login:
            headers["X-Hub-Test-Login"] = login
        request, self.key = handshake_request(origin, headers, path, key, drop=drop)
        self.sock.sendall(request + trailer)
        self.status, self.reply_headers, self.buffer = read_head(self.sock, timeout)
        self.ok = self.status.split()[1] == "101"
        if self.ok:
            expect = base64.b64encode(hashlib.sha1((self.key + GUID).encode("ascii")).digest()).decode("ascii")
            assert self.reply_headers.get("sec-websocket-accept") == expect, "wrong Sec-WebSocket-Accept"

    def send(self, data):
        self.sock.settimeout(self.timeout)                  # (a read with no wait leaves the socket not blocking, and a write on it could fail for a full buffer)
        self.sock.sendall(data)

    def say(self, text):
        self.send(say(text))

    def recv_exact(self, n, timeout):
        self.sock.settimeout(timeout)
        while len(self.buffer) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError("the socket was closed")
            self.buffer += chunk
        data, self.buffer = self.buffer[:n], self.buffer[n:]
        return data

    def recv_frame(self, timeout=None):
        """(opcode, payload) of the next server frame (they are never masked); EOFError when the socket is closed; socket.timeout when nothing comes."""
        timeout = self.timeout if timeout is None else timeout
        b0, b1 = self.recv_exact(2, timeout)
        assert not b1 & 0x80, "a server frame must not be masked"
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack("!H", self.recv_exact(2, timeout))[0]
        elif n == 127:
            n = struct.unpack("!Q", self.recv_exact(8, timeout))[0]
        return b0 & 0x0F, self.recv_exact(n, timeout)

    def recv_json(self, timeout=None):
        """The next text frame as JSON; pings from the server are answered and skipped."""
        while True:
            opcode, payload = self.recv_frame(timeout)
            if opcode == 1:
                return json.loads(payload.decode("utf-8"))
            if opcode == 9:
                self.send(frame(10, payload))
                continue
            if opcode == 8:
                raise EOFError("closed: %r" % payload)
            raise AssertionError("unexpected opcode %d" % opcode)

    def expect_close(self, timeout=None):
        """The code of the close frame that the server sends (after any text still on the way); None when the socket ends without one."""
        end = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < end:
            try:
                opcode, payload = self.recv_frame(max(0.05, end - time.monotonic()))
            except EOFError:
                return None
            if opcode == 8:
                return struct.unpack("!H", payload[:2])[0] if len(payload) >= 2 else 1005
        raise AssertionError("no close frame")

    def drain(self, timeout=0.3):
        """Every text message that arrives within `timeout` of silence."""
        out = []
        while True:
            try:
                out.append(self.recv_json(timeout))
            except (socket.timeout, BlockingIOError, EOFError):
                return out

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
