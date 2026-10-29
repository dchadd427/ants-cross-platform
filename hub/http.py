"""The little HTTP the hub speaks: read one request head, answer with one JSON body, close. The one request that does not end there is the WebSocket upgrade (hub/server.py).

A request head is parsed strictly (docs/HUB.md, "The handshake"): CRLF line ends only (a stray CR or LF fails the patterns of the request line and of the header lines), no folded
lines, no control characters, at most 60 headers, and the headers that the hub believes (Origin, Upgrade, Connection, the Sec-WebSocket pair, X-Client-Address, Content-Length,
Transfer-Encoding, Host) at most once. No request body is ever read.
"""
import json
import re

MAX_HEADERS = 60
REQUEST_LINE = re.compile(r"([A-Z]{1,10}) (/[!-~]{0,1999}) HTTP/1\.[01]\Z")
HEADER_LINE = re.compile(r"([!#$%&'*+.^_`|~0-9A-Za-z-]+):([\t\x20-\x7e\x80-\xff]*)\Z")        # (the blanks around the value are cut after the match: a pattern that did it itself backtracked, quadratically, on a long run of blanks)
ONCE = frozenset(("origin", "upgrade", "connection", "sec-websocket-key", "sec-websocket-version", "x-client-address", "content-length", "transfer-encoding", "host",
                  "cookie", "x-hub-test-login"))
REASONS = {101: "Switching Protocols", 200: "OK", 400: "Bad Request", 403: "Forbidden", 404: "Not Found", 405: "Method Not Allowed", 408: "Request Timeout",
           426: "Upgrade Required", 429: "Too Many Requests", 431: "Request Header Fields Too Large", 500: "Internal Server Error", 503: "Service Unavailable"}


class HttpError(Exception):
    def __init__(self, status, code, headers=None):
        super().__init__(code)
        self.status = status
        self.code = code
        self.headers = headers or {}


class Request:
    __slots__ = ("method", "path", "headers")

    def __init__(self, method, path, headers):
        self.method = method
        self.path = path
        self.headers = headers              # lower-case name -> value; a header that came twice is joined with ", " (only those not in ONCE may)

    def has_token(self, name, token):
        return token in [t.strip().lower() for t in self.headers.get(name, "").split(",")]


def parse_head(head):
    """A Request from the bytes up to and including the blank line, or an HttpError."""
    try:
        text = head.decode("latin-1")
    except UnicodeDecodeError:                                       # (cannot happen: latin-1 takes every byte)
        raise HttpError(400, "head")
    if not text.endswith("\r\n\r\n"):
        raise HttpError(400, "head")
    lines = text[:-4].split("\r\n")
    m = REQUEST_LINE.match(lines[0])
    if not m:
        raise HttpError(400, "request_line")
    if len(lines) - 1 > MAX_HEADERS:
        raise HttpError(431, "headers")
    headers = {}
    for line in lines[1:]:
        h = HEADER_LINE.match(line)
        if not h:
            raise HttpError(400, "header")
        name, value = h.group(1).lower(), h.group(2).strip(" \t")
        if name in headers:
            if name in ONCE:
                raise HttpError(400, "repeated_" + name.replace("-", "_"))
            headers[name] += ", " + value
        else:
            headers[name] = value
    return Request(m.group(1), m.group(2), headers)


def response(status, body=None, headers=None):
    """The bytes of a response that closes the connection; a body is a JSON value."""
    payload = b"" if body is None else json.dumps(body, separators=(",", ":")).encode("utf-8")
    lines = ["HTTP/1.1 %d %s" % (status, REASONS.get(status, "Status"))]
    if status != 101:
        lines += ["Content-Type: application/json", "Content-Length: %d" % len(payload), "Cache-Control: no-store", "X-Content-Type-Options: nosniff", "Connection: close"]
    lines += ["%s: %s" % kv for kv in (headers or {}).items()]
    return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + payload
