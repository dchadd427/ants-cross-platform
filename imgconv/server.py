"""The image converter's door (docs/HUB_IMAGES.md): one small HTTP server that does one thing for one caller, the chat hub, on the stack's private network.

  POST /convert   body = the bytes of an uploaded image (at most 4 MiB, a `Content-Length` and nothing else)
                  200  one line of JSON (`x`, `w`, `h`, `tw`, `th`, `anim`, `still`, `full`, `thumb`: the type and sizes of the two files and their lengths), a newline, the image, the thumbnail
                  4xx  {"error": code}: `type`, `pixels`, `bad`, `frames`, `big` (what imgconv/convert.py says), `length`, `body`
  GET  /health    200, for the container's health check

The server holds no secret, reads no file, writes no file and talks to nobody: it is given bytes and answers with bytes. It serves one request at a time (a second connection waits in the listen
queue, which the hub never fills: it sends at most one at a time), and every conversion runs in a child process of its own with a CPU limit, a memory limit and a wall clock limit, so that a crash
or a hang inside a decoder is one refused image and never a dead service. Nothing of the image or of the caller is logged: a line says a code and a size.
"""
import http.server
import json
import multiprocessing
import os
import sys
import time

import convert as convertmod

MAX_BODY = 4 * 1024 * 1024
WALL_SECONDS = 10.0                     # the child is killed after this long, whatever it is doing
CPU_SECONDS = 8                         # ... and by the kernel after this much CPU
MEMORY_BYTES = 1024 * 1024 * 1024       # address space of the child (the container's own memory limit is what really counts)
READ_SECONDS = 15.0                     # a request that is not all there by then is dropped


def child(conn, data):
    """The conversion, in a process of its own: the limits first, then the decoder, then the answer through the pipe (a crash sends nothing, which the parent reads as `bad`)."""
    try:
        import resource
        for which, value in ((resource.RLIMIT_CPU, CPU_SECONDS), (resource.RLIMIT_AS, MEMORY_BYTES)):
            try:
                resource.setrlimit(which, (value, value))
            except (ValueError, OSError):
                pass                                    # (a platform without that limit: the container's own are what hold)
    except ImportError:
        pass
    try:
        r = convertmod.convert(data)
        conn.send(("ok", tuple(r)))
    except convertmod.ConvertError as e:
        conn.send(("error", e.code))
    except BaseException:
        conn.send(("error", "bad"))
    finally:
        conn.close()


def run_child(data, wall=WALL_SECONDS):
    """(`ok`, Result) or (`error`, code): the conversion in a forked child, killed when it takes too long."""
    ctx = multiprocessing.get_context("fork")
    parent, kid = ctx.Pipe(duplex=False)
    process = ctx.Process(target=child, args=(kid, data), daemon=True)
    process.start()
    kid.close()
    try:
        if parent.poll(wall):
            try:
                kind, value = parent.recv()
            except (EOFError, OSError):
                return "error", "bad"
            return kind, convertmod.Result(*value) if kind == "ok" else value
        return "error", "bad"                           # (too slow: it is killed below, and the answer is the same as for a file that cannot be read)
    finally:
        parent.close()
        if process.is_alive():
            process.kill()
        process.join(2)


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "AntsImg"
    sys_version = ""
    protocol_version = "HTTP/1.1"
    timeout = READ_SECONDS

    def log_message(self, *args):
        pass                                            # (nothing of a request is logged: the server's own lines say a code and a size, below)

    def send(self, status, body, content_type="application/json"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def error(self, status, code):
        self.send(status, json.dumps({"error": code}).encode("ascii"))

    def do_GET(self):
        if self.path == "/health":
            return self.send(200, b'{"ok":true}')
        self.error(404, "not_found")

    def do_POST(self):
        if self.path != "/convert":
            return self.error(404, "not_found")
        length = self.headers.get("Content-Length", "")
        if "Transfer-Encoding" in self.headers or not length.isdigit() or not 1 <= int(length) <= MAX_BODY:
            return self.error(413 if length.isdigit() and int(length) > MAX_BODY else 400, "length")
        try:
            data = self.rfile.read(int(length))
        except OSError:
            return
        if len(data) != int(length):
            return self.error(400, "body")
        started = time.monotonic()
        kind, value = run_child(data)
        ms = int((time.monotonic() - started) * 1000)
        if kind != "ok":
            print("convert code=%s bytes=%d ms=%d" % (value, len(data), ms), file=sys.stderr, flush=True)
            return self.error(422, value)
        head = {"x": value.x, "w": value.w, "h": value.h, "tw": value.tw, "th": value.th, "anim": bool(value.anim), "still": bool(value.still), "full": len(value.full), "thumb": len(value.thumb)}
        print("convert code=ok x=%s bytes=%d ms=%d" % (value.x, len(data), ms), file=sys.stderr, flush=True)
        self.send(200, json.dumps(head, separators=(",", ":")).encode("ascii") + b"\n" + value.full + value.thumb, "application/octet-stream")


def make(host, port):
    server = http.server.HTTPServer((host, port), Handler)
    server.request_queue_size = 8
    return server


def main():
    port = int(os.environ.get("ANTS_IMG_PORT", "4050"))
    server = make(os.environ.get("ANTS_IMG_HOST", "0.0.0.0"), port)
    print("listening port=%d" % port, file=sys.stderr, flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
