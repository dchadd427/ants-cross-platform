#!/usr/bin/env python3
"""The images' upload door and the converter's answers, on real sockets, against the real roles and a fake Discord and a fake converter (hub/imgdoor.py, hub/images.py, hub/roles.py, hub/admindoor.py;
docs/HUB_IMAGES.md). The fake converter says whatever the test wants, honest or not, so the hub's own checks of its answer are what is tested (the real one is tests/scripts/test_img_converter.py). Every id,
name and byte here is made up.

  TheOrder       everything that can be decided is decided before a body byte is read: a refused request is answered at once, with the body not sent
  TheUpload      a good image becomes files, a row and an id, and the account's next `say` puts it in the chat; the converter is sent the bytes and nothing else; the types, the lengths, the budgets, the
                 waiting uploads, the places; the files of a deleted line go at once
  TheConverter   a converter that refuses, closes, hangs, lies about its sizes, its types or its lengths: one answer, nothing stored, the account's token back where it was not the account's doing
  TheRole        the role is Discord's and fails closed: a lapsed answer, a Discord that never answered, a role that is taken away, a flag that comes while the converter works; the operator's account needs none
  TheAdminDoor   the admin page's list of the people whose posting is taken away, and its two changes, with the page's token and the table of the powers
  NoId           no Discord id, no id of a role, no cookie in a frame, an answer, a log line, a row or a file name
"""
import http.server
import json
import os
import socket
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "..")))
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from test_hub_admin_door import DoorTest, NAMES as DOOR_NAMES, ADMIN as DOOR_ADMIN, MOD as DOOR_MOD, OWNER as DOOR_OWNER, TARGET as DOOR_TARGET, ADMIN2 as DOOR_ADMIN2, PLAIN as DOOR_PLAIN  # noqa: E402

OWNER = fd.snowflake(910)
ADMIN = fd.snowflake(911)
MOD = fd.snowflake(912)
HOLDER = fd.snowflake(913)               # holds the images' role
PLAIN = fd.snowflake(914)
OTHER = fd.snowflake(915)                # a second holder
NAMES = {OWNER: "olive.owner", ADMIN: "adam.admin", MOD: "molly.mod", HOLDER: "hazel.holder", PLAIN: "pat.plain", OTHER: "otto.other"}
OPTIONS = dict(roles_refill_seconds=0.01, roles_jitter=0.0, tick_seconds=0.05, sockets_per_address=50, frame_burst=1000, frame_refill_seconds=0.001, discord_seconds=1.0, discord_connect_seconds=1.0,
               admin_lookups_burst=50, admin_lookups_refill_seconds=0.01, image_convert_seconds=2.0, image_body_seconds=3.0, image_connect_seconds=1.0, image_queue_seconds=1.0,
               image_address_burst=1000, image_address_refill_seconds=0.001, burst=1000, refill_seconds=0.001, global_burst=10 ** 6, global_refill_seconds=0.0001, account_hour_lines=10 ** 6, account_day_lines=10 ** 6)
JPEG_FULL = b"\xff\xd8\xff\xe0" + b"made-up jpeg " * 5 + b"\xff\xd9"
JPEG_THUMB = b"\xff\xd8\xff\xe0" + b"made-up thumb " * 3 + b"\xff\xd9"
PNG_FULL = b"\x89PNG\r\n\x1a\n" + b"made-up png " * 5 + b"IEND\xaeB`\x82"
GIF_FULL = b"GIF89a" + b"made-up gif " * 5 + b";"
UPLOAD = b"\x89PNG\r\n\x1a\n the bytes that the page sends: the converter decides what they are"
SECRET_COOKIE = "cookie-canary-0123456789abcdef0123456789abcd"


def reply(x="jpg", full=None, thumb=None, **override):
    """The converter's 200: a line of JSON and then the two files."""
    full = full if full is not None else {"jpg": JPEG_FULL, "png": PNG_FULL, "gif": GIF_FULL}[x]
    thumb = thumb if thumb is not None else JPEG_THUMB
    head = {"x": x, "w": 800, "h": 600, "tw": 360, "th": 270, "anim": x == "gif", "still": False, "full": len(full), "thumb": len(thumb)}
    head.update(override)
    return json.dumps(head).encode() + b"\n" + full + thumb


class FakeConverter:
    """The converter of the stack as a test wants it: `mode` is `ok`, `refuse:<code>`, `close` (no answer), `hang`, `hold` (waits for `release`), or `raw:<name>` (a answer that is not exactly the converter's) or `rawhold:<name>` (the same, and the connection stays open)."""

    def __init__(self):
        self.mode = "ok"
        self.calls = []                       # (headers, body) of every call
        self.release = threading.Event()
        self.entered = threading.Semaphore(0)
        self.x = "jpg"
        self.raw = {}
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):
                pass

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
                outer.calls.append((dict(self.headers), body))
                outer.entered.release()
                mode = outer.mode
                if mode == "close":
                    self.close_connection = True
                    return
                if mode == "hang":
                    time.sleep(5)
                    return
                if mode == "hold":
                    outer.release.wait(15)
                if mode.startswith("refuse:"):
                    return self.send(422 if mode[7:] != "big" else 413, json.dumps({"error": mode[7:]}).encode())
                if mode.startswith("raw:"):
                    return self.write_raw(outer.raw[mode[4:]])
                if mode.startswith("rawhold:"):                # (some of an answer, and then the connection stays open: a hub that waits for what was promised waits for the whole time that it has)
                    try:
                        self.wfile.write(outer.raw[mode[8:]])
                        self.wfile.flush()
                    except OSError:
                        pass
                    outer.release.wait(15)
                    self.close_connection = True
                    return
                self.send(200, reply(outer.x))

            def send(self, status, body):
                self.write_raw(("HTTP/1.1 %d X\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % (status, len(body))).encode() + body)

            def write_raw(self, data):
                try:
                    self.wfile.write(data)
                    self.wfile.flush()
                except OSError:
                    pass                                  # (a hub that read what it wanted and hung up: an answer that is too big, a lie)
                self.close_connection = True

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.httpd.daemon_threads = True
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    @property
    def url(self):
        return "http://127.0.0.1:%d" % self.port

    def close(self):
        self.release.set()
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(5)


def with_head(kind="jpg", **override):
    """The converter's 200 with some keys of its first line changed (the files are what they were: a lie is in the line)."""
    full = {"jpg": JPEG_FULL, "png": PNG_FULL, "gif": GIF_FULL}[kind]
    head = {"x": kind, "w": 800, "h": 600, "tw": 360, "th": 270, "anim": kind == "gif", "still": False, "full": len(full), "thumb": len(JPEG_THUMB)}
    head.update(override)
    return json.dumps(head).encode() + b"\n" + full + JPEG_THUMB


def recv(c, seconds=1.0):
    """The next frame of a chat socket as JSON, or None when nothing comes (or the socket ended) within `seconds`."""
    try:
        return c.recv_json(seconds)
    except (socket.timeout, EOFError):
        return None


def send_json(c, obj):
    c.send(hc.frame(1, json.dumps(obj).encode("utf-8")))


class ImgTest(unittest.TestCase):
    options = {}
    env = {}

    def setUp(self):
        self.fake = fd.FakeDiscord()
        self.addCleanup(self.fake.close)
        self.fake.members = {OWNER: [], ADMIN: [fd.ROLE_ADMIN], MOD: [fd.ROLE_MOD], HOLDER: [fd.ROLE_IMAGES], OTHER: [fd.ROLE_IMAGES], PLAIN: []}
        self.converter = FakeConverter()
        self.addCleanup(self.converter.close)
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        env = dict({"ANTS_HUB_IMAGE_CONVERTER_URL": self.converter.url, "ANTS_HUB_IMAGE_DIR": self.folder.name}, **self.env)
        self.rig = hub_rig.Rig(member_fake=self.fake, owner=OWNER, recheck=30, env=env, **dict(OPTIONS, **self.options))
        self.rig.__enter__()
        self.addCleanup(self.rig.__exit__, None, None, None)
        self.sockets = []
        self.addCleanup(self.close_sockets)
        self.count = 0
        for account in NAMES:
            self.rig.call(self.rig.store.set_profile, account, NAMES[account], "", 1000)

    def close_sockets(self):
        for c in self.sockets:
            c.close()

    # ---- the page's calls --------------------------------------------------------------------------------------------------------------------

    def socket(self, account):
        """A chat socket of the account, with its role known to the hub (its `you` says `img` as the hub knows it)."""
        c = hc.Client(self.rig.port, login="%s:%s" % (account, NAMES[account]), timeout=30.0)
        self.sockets.append(c)
        self.assertTrue(c.ok, c.status)
        c.welcome = c.recv_json()
        return c

    def know(self, account, state=None, seconds=20):
        """Open a socket of the account and read until its `you` carries the state of the images that the test wants (`on` or `role` by default: whatever is not `wait`); the socket."""
        c = self.socket(account)
        you = c.welcome["you"]
        end = time.monotonic() + seconds
        while (you.get("img") == "wait" if state is None else you.get("img") != state) and time.monotonic() < end:
            m = recv(c, 1.0)
            if m and m.get("t") == "you":
                you = m["you"]
        c.you = you
        return c

    def headers(self, account, extra=None, drop=()):
        self.count += 1
        pairs = {"X-Client-Address": "198.51.100.%d" % (self.count % 200 + 1), "X-Hub-Test-Login": "%s:%s" % (account, NAMES[account]), "Origin": hc.SITE, "Sec-Fetch-Site": "same-origin",
                 "Content-Type": "application/octet-stream", "X-Ants-Image": "1"}
        pairs.update(extra or {})
        for name in drop:
            pairs.pop(name, None)
        return pairs

    def upload(self, account, body=UPLOAD, extra=None, drop=(), **kw):
        return hc.request(self.rig.port, "POST", "/hub/img/up", self.headers(account, extra, drop), body=body, timeout=30.0, **kw)

    def raw_post(self, account, promised, extra=None, drop=(), send=b"", wait=3.0, method="POST", path="/hub/img/up"):
        """A request that PROMISES `promised` bytes and sends `send` of them: the answer (status, JSON) as the hub gives it with the rest of the body not sent, or (None, None) when it waits for the body."""
        pairs = self.headers(account, extra, drop)
        head = "%s %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n" % (method, path) + "".join("%s: %s\r\n" % kv for kv in pairs.items())
        if promised is not None:
            head += "Content-Length: %d\r\n" % promised
        with socket.create_connection(("127.0.0.1", self.rig.port), timeout=wait) as s:
            s.sendall((head + "\r\n").encode("latin-1") + send)
            data = b""
            try:
                while b"\r\n\r\n" not in data or len(data.partition(b"\r\n\r\n")[2]) < int(self.length_of(data) or 0):
                    chunk = s.recv(4096)
                    if not chunk:
                        break
                    data += chunk
            except socket.timeout:
                return None, None
        status = int(data.split(b" ", 2)[1])
        body = data.partition(b"\r\n\r\n")[2]
        return status, json.loads(body) if body else None

    @staticmethod
    def length_of(data):
        for line in data.partition(b"\r\n\r\n")[0].split(b"\r\n")[1:]:
            if line.lower().startswith(b"content-length:"):
                return line.split(b":")[1].strip()
        return 0

    def files(self):
        return sorted(os.listdir(self.folder.name))

    def say(self, c, text, img=None):
        send_json(c, {"t": "say", "text": text, **({"img": img} if img is not None else {})})

    def next_of(self, c, kind, seconds=10):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            m = recv(c, 1.0)
            if m and m.get("t") == kind:
                return m
        return None

    def limit(self, **changes):
        """Change numbers of the hub's limits where the images read them (the hub, its images, its uploads' budgets, the converter's client): a test that needs a small burst or a long wait."""
        def apply():
            hub = self.rig.hub
            new = type(hub.limits)(**{**hub.limits.__dict__, **changes})
            hub.limits = hub.images.limits = hub.images.uploads.limits = hub.converter.limits = new
        self.rig.call(apply)

    def role_known(self, account, images=None):
        """Make the hub's answer about the account old (or young): `ok_at` of its entry is moved, which is what time does to a belief."""
        def move():
            entry = self.rig.hub.chat.roles.entries[account]
            entry.ok_at -= 10 ** 6 if images is False else 0
        self.rig.call(move)

    def log(self):
        return self.rig.log_text.getvalue()


class TheOrder(ImgTest):
    """The request is judged before its body is read: with a promised body that never comes, a refusal still comes at once."""

    def setUp(self):
        super().setUp()
        self.know(HOLDER)
        self.know(PLAIN)

    def refused(self, status, code, account=HOLDER, promised=1000, **kw):
        got = self.raw_post(account, promised, **kw)
        self.assertEqual(got, (status, {"error": code}), kw)
        self.assertEqual(self.converter.calls, [])
        self.assertEqual(self.files(), [])

    def test_the_method_and_the_query_and_the_site_and_the_type_are_refused_at_once(self):
        self.refused(405, "method", method="GET")
        self.refused(404, "not_found", path="/hub/img/up?x=1")
        self.refused(403, "origin", drop=("Origin",))
        self.refused(403, "origin", extra={"Origin": "https://evil.example"})
        self.refused(403, "origin", extra={"Origin": hc.SITE + "/"})
        self.refused(403, "origin", drop=("Sec-Fetch-Site",))
        self.refused(403, "origin", extra={"Sec-Fetch-Site": "cross-site"})
        self.refused(403, "origin", extra={"Sec-Fetch-Site": "same-site"})
        self.refused(415, "type", drop=("X-Ants-Image",))
        self.refused(415, "type", extra={"X-Ants-Image": "2"})
        self.refused(415, "type", extra={"Content-Type": "image/png"})
        self.refused(415, "type", drop=("Content-Type",))
        self.refused(415, "type", extra={"Content-Type": "multipart/form-data; boundary=x"})

    def test_a_visitor_with_no_login_and_a_person_who_may_not_are_refused_before_their_body(self):
        self.refused(401, "login", drop=("X-Hub-Test-Login",))
        self.refused(401, "login", extra={"X-Hub-Test-Login": "not a login"})
        self.refused(403, "images", account=PLAIN)

    def test_a_banned_a_muted_a_young_and_a_flagged_account_are_refused_before_their_body(self):
        def set_flags():
            store, ref = self.rig.store, self.rig.hub.chat.ref
            store.ban(OTHER, "terminal")
            store.mute(MOD, 10, "terminal")
            store.set_noimages(HOLDER, True, "terminal")
            self.rig.hub.chat.sync_images()
        self.know(OTHER)
        self.know(MOD)
        self.rig.call(set_flags)
        for account in (OTHER, MOD, HOLDER):
            self.refused(403, "images", account=account)
        self.refused(403, "images", extra={"X-Hub-Test-Login": "%s:%s:young" % (ADMIN, NAMES[ADMIN])}, account=ADMIN)

    def test_the_length_is_judged_before_the_body_is_read(self):
        self.refused(411, "length", promised=None)
        self.refused(411, "length", promised=0)
        self.refused(413, "big", promised=4 * 1024 * 1024 + 1)
        self.refused(413, "big", promised=10 ** 9)
        self.refused(411, "length", promised=None, extra={"Transfer-Encoding": "chunked"})
        status, _ = self.raw_post(HOLDER, 4 * 1024 * 1024, send=b"x" * 10, wait=1.0)
        self.assertIsNone(status, "the biggest that is allowed is waited for, not refused")

    def test_the_order_is_the_site_then_the_type_then_a_login_then_the_role_then_the_length(self):
        self.refused(403, "origin", account=PLAIN, extra={"Origin": "https://evil.example", "Content-Type": "text/plain"}, drop=("X-Hub-Test-Login",))
        self.refused(415, "type", account=PLAIN, extra={"Content-Type": "text/plain"}, drop=("X-Hub-Test-Login",), promised=None)
        self.refused(401, "login", drop=("X-Hub-Test-Login",), promised=None)
        self.refused(403, "images", account=PLAIN, promised=None)

    def test_a_body_that_does_not_come_is_dropped_and_holds_no_place(self):
        status, body = self.raw_post(HOLDER, 5000, send=b"\x89PNG" + b"x" * 100, wait=10.0)
        self.assertEqual((status, body), (400, {"error": "body"}))
        self.assertEqual(self.converter.calls, [])
        self.assertEqual(self.upload(HOLDER).status, 200, "the places are free again")

    def test_a_body_of_another_kind_than_an_image_is_refused_by_its_first_bytes_and_never_sent_on(self):
        for body in (b"<svg xmlns='http://www.w3.org/2000/svg'><script>1</script></svg>", b"<!doctype html><script>1</script>", b"PK\x03\x04zip", b"BM\x00\x00", b"%PDF-1.4", b"x" * 100):
            r = self.upload(HOLDER, body)
            self.assertEqual((r.status, r.json()), (415, {"error": "type"}), body[:12])
        self.assertEqual(self.converter.calls, [])
        for first in (b"\x89PNG", b"\xff\xd8\xff", b"GIF89a", b"RIFF"):
            self.assertEqual(self.upload(HOLDER, first + b"x" * 40).status, 200, first)       # (the converter looks at the rest)
            self.converter.calls.clear()
            self.rig.call(lambda: self.rig.hub.chat.store.db.execute("DELETE FROM images"))
            self.rig.call(self.rig.hub.images.uploads.accounts.clear)


class TheUpload(ImgTest):
    def test_a_holder_uploads_and_says_and_everybody_sees_it_and_the_files_are_the_converters_and_named_at_random(self):
        holder, reader = self.know(HOLDER), hc.Client(self.rig.port)
        self.sockets.append(reader)
        reader.recv_json()
        self.assertEqual(holder.you["img"], "on")
        r = self.upload(HOLDER)
        self.assertEqual(r.status, 200)
        answer = r.json()
        self.assertEqual(set(answer), {"id", "w", "h", "x", "anim", "still"})
        self.assertRegex(answer["id"], r"^[0-9a-f]{32}$")
        self.assertEqual((answer["w"], answer["h"], answer["x"], answer["anim"], answer["still"]), (800, 600, "jpg", False, False))
        self.assertEqual(r.get("cache-control"), "no-store")
        self.assertEqual(r.get("x-content-type-options"), "nosniff")
        self.assertEqual(self.files(), [answer["id"] + ".jpg", answer["id"] + ".t.jpg"])
        with open(os.path.join(self.folder.name, answer["id"] + ".jpg"), "rb") as f:
            self.assertEqual(f.read(), JPEG_FULL, "the converter's bytes, not the upload's")
        with open(os.path.join(self.folder.name, answer["id"] + ".t.jpg"), "rb") as f:
            self.assertEqual(f.read(), JPEG_THUMB)
        self.say(holder, "the best spot", answer["id"])
        line = self.next_of(reader, "line")
        self.assertEqual(line["text"], "the best spot")
        self.assertEqual(line["img"], {"n": answer["id"], "x": "jpg", "w": 800, "h": 600, "tw": 360, "th": 270})
        self.assertEqual(self.rig.call(self.rig.store.image_of_line, line["id"]).name, answer["id"])

    def test_the_converter_is_sent_the_bytes_and_nothing_of_the_visitor(self):
        self.know(HOLDER)
        self.assertEqual(self.upload(HOLDER, UPLOAD, extra={"Cookie": "__Host-ants_hub=" + SECRET_COOKIE}).status, 200)
        (headers, body), = self.converter.calls
        self.assertEqual(body, UPLOAD)
        text = json.dumps(headers).lower()
        for needle in (SECRET_COOKIE, HOLDER, "x-hub-test-login", "origin", "x-client-address", "cookie", "198.51.100", NAMES[HOLDER]):
            self.assertNotIn(needle.lower(), text, needle)
        self.assertEqual(headers["Content-Type"], "application/octet-stream")

    def test_a_png_a_gif_and_a_webp_give_their_own_type_and_an_animated_one_says_so(self):
        self.know(HOLDER)
        for x, anim in (("png", False), ("gif", True), ("jpg", False)):
            self.converter.x = x
            r = self.upload(HOLDER)
            self.assertEqual((r.status, r.json()["x"], r.json()["anim"]), (200, x, anim))
            self.assertTrue(os.path.exists(os.path.join(self.folder.name, r.json()["id"] + "." + x)))
            self.rig.call(self.rig.hub.images.uploads.accounts.clear)

    def test_at_most_three_uploads_wait_and_a_line_takes_one_and_the_fourth_is_refused(self):
        holder = self.know(HOLDER)
        ids = []
        for _ in range(3):
            ids.append(self.upload(HOLDER).json()["id"])
            self.rig.call(self.rig.hub.images.uploads.accounts.clear)
        r = self.upload(HOLDER)
        self.assertEqual((r.status, r.json()), (429, {"error": "rate"}))
        self.assertEqual(len(self.files()), 6)
        self.say(holder, "one", ids[0])
        self.assertIsNotNone(self.next_of(holder, "line"))
        self.rig.call(self.rig.hub.images.uploads.accounts.clear)
        self.assertEqual(self.upload(HOLDER).status, 200)

    def test_an_account_has_a_burst_and_then_waits_and_its_token_is_spent_by_a_refused_image_too(self):
        self.know(HOLDER)
        self.limit(image_burst=2, image_refill_seconds=60.0, image_pending_max=10)
        self.assertEqual(self.upload(HOLDER, b"BAD" + b"x" * 30).status, 415)
        self.assertEqual(self.upload(HOLDER).status, 200, "a refusal by the first bytes spends nothing: the hub did not look at the image")
        self.converter.mode = "refuse:bad"
        self.assertEqual(self.upload(HOLDER).status, 422)
        r = self.upload(HOLDER)
        self.assertEqual((r.status, r.json()), (429, {"error": "rate"}), "two images were looked at: the burst is spent")
        self.assertEqual(len(self.converter.calls), 2)

    def test_the_hub_has_two_places_for_bodies_and_a_short_queue_for_the_converter_then_busy(self):
        self.know(HOLDER)
        self.know(OTHER)
        self.know(ADMIN)
        self.limit(image_queue_seconds=20.0, image_convert_seconds=20.0)
        self.converter.mode = "hold"
        results = []

        def go(account):
            results.append((account, self.upload(account)))
        threads = [threading.Thread(target=go, args=(a,)) for a in (HOLDER, OTHER, ADMIN)]
        for t in threads[:2]:
            t.start()
            self.assertTrue(self.converter.entered.acquire(timeout=10) or t is threads[1])
        self.rig.call(lambda: setattr(self.rig.hub.image_door, "waiting", self.rig.hub.limits.image_queue_max + 1))        # (the converter and its whole queue are taken)
        r = self.upload(ADMIN)
        self.assertEqual((r.status, r.json()), (429, {"error": "busy"}))
        self.rig.call(lambda: setattr(self.rig.hub.image_door, "waiting", 1))
        self.converter.release.set()
        for t in threads[:2]:
            t.join(20)
        self.assertEqual(sorted(r.status for _, r in results), [200, 200])
        self.assertEqual(self.upload(ADMIN).status, 200, "free again")

    def test_a_third_body_at_once_is_busy_and_gives_its_token_back(self):
        self.know(HOLDER)
        self.know(OTHER)
        self.know(ADMIN)
        sockets = []
        for account in (HOLDER, OTHER):
            s = socket.create_connection(("127.0.0.1", self.rig.port), timeout=10)
            sockets.append(s)
            head = "POST /hub/img/up HTTP/1.1\r\nHost: x\r\n" + "".join("%s: %s\r\n" % kv for kv in self.headers(account).items()) + "Content-Length: 4096\r\n\r\n"
            s.sendall(head.encode() + b"\x89PNG")
        time.sleep(0.5)
        status, body = self.raw_post(ADMIN, 100, send=b"\x89PNG" + b"x" * 96)
        self.assertEqual((status, body), (429, {"error": "busy"}))
        for s in sockets:
            s.close()
        time.sleep(0.5)
        self.assertEqual(self.upload(ADMIN).status, 200, "the places and the token are back")

    def test_the_quota_of_the_disk_is_full_and_the_next_one_waits_for_room(self):
        self.know(HOLDER)
        self.limit(image_files_max=1)
        self.assertEqual(self.upload(HOLDER).status, 200)
        r = self.upload(HOLDER)
        self.assertEqual((r.status, r.json()), (503, {"error": "full"}))
        self.assertEqual(len(self.files()), 2)
        self.assertIn("image kind=full", self.log())

    def test_a_deleted_line_takes_its_files_with_it_before_the_next_request(self):
        mod, holder = self.know(MOD), self.know(HOLDER)
        name = self.upload(HOLDER).json()["id"]
        self.say(holder, "oops", name)
        line = self.next_of(holder, "line")
        self.say(mod, "/del %d" % line["id"])
        self.assertIsNotNone(self.next_of(holder, "del"))
        self.assertEqual(self.files(), [])
        self.assertEqual(self.rig.call(self.rig.store.image_names), set())
        rows = self.rig.call(self.rig.store.actions, 0, 10)
        self.assertEqual([r.action for r in rows], ["del_img"])

    def test_the_author_deletes_its_own_image_by_its_own_frame(self):
        holder = self.know(HOLDER)
        name = self.upload(HOLDER).json()["id"]
        self.say(holder, "mine", name)
        line = self.next_of(holder, "line")
        send_json(holder, {"t": "delimg", "id": line["id"]})
        self.assertEqual(self.next_of(holder, "del"), {"t": "del", "id": line["id"]})
        self.assertEqual(self.files(), [])

    def test_a_line_that_wants_an_upload_that_is_not_there_is_imgid_and_text_goes_on_working(self):
        holder = self.know(HOLDER)
        self.say(holder, "nothing", "0" * 32)
        self.assertEqual(self.next_of(holder, "err")["code"], "imgid")
        self.say(holder, "plain words")
        self.assertEqual(self.next_of(holder, "line")["text"], "plain words")

    def test_the_log_says_a_kind_of_every_upload_and_nothing_else(self):
        self.know(HOLDER)
        self.upload(HOLDER)
        self.converter.mode = "refuse:pixels"
        self.upload(HOLDER)
        self.assertIn(" image kind=ok", self.log())
        self.assertIn(" image kind=pixels", self.log())
        for line in self.log().splitlines():
            if " image " in line:
                self.assertRegex(line, r"^\S+ image kind=[a-z]+$")


class TheConverter(ImgTest):
    def setUp(self):
        super().setUp()
        self.know(HOLDER)

    def failed(self, status, code):
        r = self.upload(HOLDER)
        self.assertEqual((r.status, r.json()), (status, {"error": code}))
        self.assertEqual(self.files(), [])
        self.assertEqual(self.rig.call(self.rig.store.image_names), set())
        self.rig.call(self.rig.hub.images.uploads.accounts.clear)

    def test_what_the_converter_refuses_is_told_in_one_word_and_nothing_is_kept(self):
        for code, status in (("type", 415), ("pixels", 422), ("bad", 422), ("frames", 422), ("big", 413)):
            self.converter.mode = "refuse:" + code
            self.failed(status, code)

    def test_a_converter_that_is_not_there_closes_hangs_or_says_a_code_that_is_not_its_own_is_down(self):
        self.converter.mode = "close"
        self.failed(503, "down")
        self.converter.mode = "hang"
        started = time.monotonic()
        self.failed(503, "down")
        self.assertLess(time.monotonic() - started, 4.5, "the hub gives up after its own limit")
        self.converter.mode = "raw:code"
        self.converter.raw["code"] = b"HTTP/1.1 422 X\r\nContent-Length: 22\r\nConnection: close\r\n\r\n" + b'{"error": "some text"}'
        self.failed(503, "down")
        self.converter.raw["code"] = b"HTTP/1.1 500 X\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"
        self.failed(503, "down")
        self.assertEqual(self.rig.call(self.rig.hub.image_door.converter_state), "down")
        self.converter.mode = "ok"
        self.assertEqual(self.upload(HOLDER).status, 200)
        self.assertEqual(self.rig.call(self.rig.hub.image_door.converter_state), "ok")

    def test_a_down_converter_does_not_use_the_accounts_budget_up(self):
        self.converter.mode = "close"
        for _ in range(8):
            self.assertEqual(self.upload(HOLDER).status, 503)
        self.converter.mode = "ok"
        self.assertEqual(self.upload(HOLDER).status, 200, "the token was given back each time")

    def test_an_answer_that_is_not_exactly_the_converters_is_down_and_nothing_is_stored(self):
        good = reply("jpg")
        head = json.loads(good.partition(b"\n")[0])
        lies = {
            "not json": b"not json\n" + JPEG_FULL + JPEG_THUMB,
            "no newline": b"x" * 100,
            "extra key": with_head(extra=1),
            "missing key": json.dumps({k: v for k, v in head.items() if k != "still"}).encode() + b"\n" + JPEG_FULL + JPEG_THUMB,
            "type not offered": with_head(x="exe"),
            "a type that is a list": with_head(x=["png"]),
            "a type that is an object": with_head(x={"a": 1}),
            "a type that is a number": with_head(x=5),
            "type that does not match the bytes": reply("jpg", full=PNG_FULL),
            "gif that is not animated": with_head("gif", anim=False),
            "a jpg that is animated": with_head(anim=True),
            "animated and still": with_head("gif", still=True),
            "lengths that do not add up": with_head(full=len(JPEG_FULL) + 1),
            "a second thumbnail behind the first, so that both ends look right": json.dumps(head).encode() + b"\n" + JPEG_FULL + JPEG_THUMB + JPEG_THUMB,
            "a length that is not a number": with_head(full="5"),
            "a bool for a size": with_head(w=True),
            "zero size": with_head(w=0),
            "a size past the cap": with_head(w=2049),
            "a thumbnail past the cap": with_head(tw=401),
            "a negative size": with_head(h=-5),
            "a float": with_head(h=600.5),
            "a thumbnail that is not a jpeg": reply("jpg", thumb=b"\x89PNG\r\n\x1a\n" + b"x" * 20 + b"IEND\xaeB`\x82"),
            "a file that does not end as its type ends": reply("jpg", full=b"\xff\xd8\xff" + b"x" * 40),
            "a png with no end": reply("png", full=b"\x89PNG\r\n\x1a\n" + b"x" * 40),
            "a header too long": b"{" + b" " * 600 + b"}\n" + JPEG_FULL + JPEG_THUMB,
            "a non-ASCII header": ("\u00e9" * 5).encode() + b"\n" + JPEG_FULL + JPEG_THUMB,
            "NaN": b'{"x":"jpg","w":NaN,"h":6,"tw":3,"th":2,"anim":false,"still":false,"full":4,"thumb":4}\n' + b"x" * 8,
        }
        for name, answer in lies.items():
            self.converter.mode = "raw:lie"
            self.converter.raw["lie"] = ("HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(answer)).encode() + answer
            self.failed(503, "down")
        for name, raw in {
            "a redirect": b"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:1/\r\nContent-Length: 0\r\n\r\n",
            "chunked": b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
            "no length": b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" + good,
            "two lengths": b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n" + b"x" * 6,
            "compressed": b"HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: %d\r\n\r\n" % len(good) + good,
            "not http": b"SSH-2.0-OpenSSH\r\n\r\n",
            "a length that is too big": b"HTTP/1.1 200 OK\r\nContent-Length: 99999999\r\n\r\n" + good[:100],
            "a short body": b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n" % (len(good) + 50) + good,
        }.items():
            self.converter.mode = "raw:lie"
            self.converter.raw["lie"] = raw
            self.failed(503, "down")

    def test_the_answer_is_read_up_to_its_cap_and_not_a_byte_more(self):
        too_much = b"x" * (self.rig.hub.limits.image_reply_max + 10)
        self.converter.mode = "raw:big"
        self.converter.raw["big"] = ("HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(too_much)).encode() + too_much
        self.failed(503, "down")
        self.converter.mode = "rawhold:promise"
        self.converter.raw["promise"] = b"HTTP/1.1 200 OK\r\nContent-Length: 99999999\r\nConnection: close\r\n\r\n" + b"x" * 100
        started = time.monotonic()
        self.failed(503, "down")
        self.assertLess(time.monotonic() - started, 4, "a length over the cap is refused at once, not waited for until the converter's time is out")

    def test_the_hub_checks_its_own_ends_even_for_an_honest_answer_at_the_edges(self):
        full = b"\xff\xd8\xff" + b"x" * (self.rig.hub.limits.image_reply_max - 10 - 200) + b"\xff\xd9"
        answer = reply("jpg", full=full, w=2048, h=2048, tw=400, th=400)
        self.converter.mode = "raw:edge"
        self.converter.raw["edge"] = ("HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(answer)).encode() + answer
        r = self.upload(HOLDER)
        self.assertEqual(r.status, 200)
        self.assertEqual((r.json()["w"], r.json()["h"]), (2048, 2048))


class TheRole(ImgTest):
    def test_a_person_whose_answer_is_old_may_not_upload_and_may_not_say_an_image_and_may_say_words(self):
        holder = self.know(HOLDER)
        self.assertEqual(self.upload(HOLDER).status, 200)
        mine = self.rig.call(lambda: list(self.rig.store.image_names()))[0]
        self.rig.call(lambda: setattr(self.rig.hub.chat.roles.entries[HOLDER], "ok_at", self.rig.hub.clock() - 10 ** 5))
        r = self.upload(HOLDER)
        self.assertEqual((r.status, r.json()), (403, {"error": "images"}))
        self.say(holder, "an image", mine)
        self.assertEqual(self.next_of(holder, "err")["code"], "img")
        self.say(holder, "just words")
        self.assertEqual(self.next_of(holder, "line")["text"], "just words")

    def test_a_discord_that_never_answered_is_wait_and_nobody_but_the_operator_posts(self):
        self.fake.member_mode = "close"
        for account in (HOLDER, ADMIN, MOD):
            c = self.socket(account)
            self.assertEqual(c.welcome["you"].get("img"), "wait", NAMES[account])
            r = self.upload(account)
            self.assertEqual((r.status, r.json()), (403, {"error": "images"}), NAMES[account])
        owner = self.socket(OWNER)
        self.assertEqual(owner.welcome["you"]["img"], "on", "the operator's account needs no answer of Discord")
        self.assertEqual(self.upload(OWNER).status, 200)
        self.assertEqual(self.files() and len(self.files()), 2)

    def test_the_images_role_a_moderator_and_an_administrator_may_and_a_person_with_no_role_may_not(self):
        for account, state in ((HOLDER, "on"), (MOD, "on"), (ADMIN, "on"), (PLAIN, "role")):
            self.assertEqual(self.know(account).you["img"], state, NAMES[account])
        self.assertEqual([self.upload(a).status for a in (HOLDER, MOD, ADMIN, PLAIN)], [200, 200, 200, 403])

    def test_a_person_with_some_other_role_of_the_server_is_a_person_with_no_images_role(self):
        self.fake.members[PLAIN] = [fd.ROLE_OTHER]
        self.assertEqual(self.know(PLAIN).you["img"], "role")
        self.assertEqual(self.upload(PLAIN).status, 403)

    def test_a_role_that_is_taken_away_is_found_at_the_next_answer_and_the_page_is_told(self):
        holder = self.know(HOLDER)
        self.assertEqual(holder.you["img"], "on")
        self.fake.members[HOLDER] = []
        def recheck():
            entry = self.rig.hub.chat.roles.entries[HOLDER]
            entry.due = 0
            entry.new = True
            self.rig.hub.chat.roles.wake.set()
        self.rig.call(recheck)
        you = self.next_of(holder, "you")
        end = time.monotonic() + 15
        while you and you["you"].get("img") == "on" and time.monotonic() < end:
            you = self.next_of(holder, "you")
        self.assertEqual(you["you"]["img"], "role")
        self.assertEqual(self.upload(HOLDER).status, 403)

    def test_the_images_already_posted_by_a_person_whose_role_lapsed_stay(self):
        holder = self.know(HOLDER)
        name = self.upload(HOLDER).json()["id"]
        self.say(holder, "posted while allowed", name)
        line = self.next_of(holder, "line")
        self.rig.call(lambda: setattr(self.rig.hub.chat.roles.entries[HOLDER], "ok_at", self.rig.hub.clock() - 10 ** 5))
        self.assertEqual(self.files(), [name + ".jpg", name + ".t.jpg"])
        late = hc.Client(self.rig.port)
        self.sockets.append(late)
        welcome = late.recv_json()
        self.assertEqual(welcome["history"][-1]["img"]["n"], name, "a newcomer still sees it")
        self.assertEqual(line["img"]["n"], name)

    def test_a_flag_that_comes_while_the_converter_works_keeps_the_image_from_being_stored(self):
        self.know(HOLDER)
        self.converter.mode = "hold"
        out = []
        t = threading.Thread(target=lambda: out.append(self.upload(HOLDER)))
        t.start()
        self.assertTrue(self.converter.entered.acquire(timeout=10))

        def flag():
            self.rig.store.set_noimages(HOLDER, True, "terminal")
            self.rig.hub.chat.sync_images()
        self.rig.call(flag)
        self.converter.release.set()
        t.join(20)
        self.assertEqual((out[0].status, out[0].json()), (403, {"error": "images"}))
        self.assertEqual(self.files(), [])

    def test_the_flag_beats_the_role_the_operator_is_above_it_and_a_lifted_flag_gives_the_role_back(self):
        self.know(HOLDER)
        self.know(OWNER)
        def flag(on):
            for account in (HOLDER, OWNER):
                self.rig.store.set_noimages(account, on, "terminal")
            self.rig.hub.chat.sync_images()
        self.rig.call(flag, True)
        self.assertEqual((self.upload(HOLDER).status, self.upload(OWNER).status), (403, 200))
        self.rig.call(flag, False)
        self.assertEqual(self.upload(HOLDER).status, 200)

    def test_no_converter_no_images_and_the_door_says_off(self):
        self.rig = hub_rig.Rig(member_fake=self.fake, owner=OWNER, recheck=30, **dict(OPTIONS))      # (a second hub with no converter address: the first one is stopped by its own cleanup)
        self.rig.__enter__()
        self.addCleanup(self.rig.__exit__, None, None, None)
        c = self.socket(HOLDER)
        self.assertNotIn("img", c.welcome["you"])
        r = self.upload(HOLDER)
        self.assertEqual((r.status, r.json()), (503, {"error": "off"}))


class NoId(ImgTest):
    def test_no_id_no_role_id_and_no_cookie_anywhere_a_page_or_a_log_could_show_it(self):
        mod, holder, reader = self.know(MOD), self.know(HOLDER), self.socket(PLAIN)
        frames = []
        name = self.upload(HOLDER, extra={"Cookie": "__Host-ants_hub=" + SECRET_COOKIE}).json()["id"]
        self.say(holder, "caption", name)
        self.next_of(holder, "line")
        self.say(mod, "/noimages hazel.holder")
        self.next_of(mod, "ok")
        for client in (mod, holder, reader):
            frames.append(json.dumps(client.welcome))
        for client in (mod, holder, reader):
            for _ in range(10):
                m = recv(client, 0.3)
                if m is None:
                    break
                frames.append(json.dumps(m))
        everything = "\n".join(frames + [self.log()])
        rows = [repr(self.rig.call(lambda t=t: self.rig.store.db.execute("SELECT * FROM %s" % t).fetchall())) for t in ("images", "noimages", "modlog")]
        everything += "\n".join(rows) + "\n".join(self.files())
        for needle in list(NAMES) + [fd.ROLE_IMAGES, fd.ROLE_ADMIN, fd.ROLE_MOD, SECRET_COOKIE, self.fake.bot_token, self.fake.guild]:
            self.assertNotIn(needle, everything.replace(NAMES[HOLDER] if needle == HOLDER else "\0", ""), needle)
        for needle in ("198.51.100",):
            self.assertNotIn(needle, everything)

    def test_every_refusal_of_the_door_is_json_with_one_code_and_no_other_text(self):
        self.know(PLAIN)
        for r in (self.upload(PLAIN), self.upload(PLAIN, extra={"Origin": "https://evil.example"}), self.upload(PLAIN, drop=("X-Hub-Test-Login",))):
            self.assertEqual(r.get("content-type"), "application/json")
            self.assertEqual(set(r.json()), {"error"})
            self.assertRegex(r.json()["error"], r"^[a-z_]+$")
            for needle in (PLAIN, NAMES[PLAIN], fd.ROLE_IMAGES):
                self.assertNotIn(needle, r.text())


class TheAdminDoor(DoorTest):
    """The admin page's list and its two changes (docs/HUB_IMAGES.md, "Moderation"): the same door, token and table of the powers as for a ban."""

    options = {}

    def setUp(self):
        self.image_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.image_dir.cleanup)
        super().setUp()

    def ref_of(self, account):
        return self.ref(account)

    def noimages(self, account=DOOR_ADMIN):
        return self.get("imagesoff", account)

    def test_the_list_is_for_an_administrator_and_holds_names_and_references_and_never_an_id(self):
        self.rig.call(self.rig.store.set_noimages, DOOR_TARGET, True, self.ref(DOOR_ADMIN))
        r = self.noimages()
        self.assertEqual(r.status, 200)
        body = r.json()
        self.assertEqual((body["untrusted"], body["untrusted_fields"]), (True, ["name", "by_name"]))
        (row,) = body["imagesoff"]
        self.assertEqual((row["ref"], row["name"], row["by"], row["by_name"]), (self.ref(DOOR_TARGET), DOOR_NAMES[DOOR_TARGET], self.ref(DOOR_ADMIN), DOOR_NAMES[DOOR_ADMIN]))
        self.assertEqual(set(row), {"ref", "name", "at", "by", "by_name"})
        for account in DOOR_NAMES:
            self.assertNotIn(account, r.text())
        self.assertEqual(self.noimages(DOOR_MOD).status, 403)
        self.assertEqual(self.noimages(DOOR_PLAIN).status, 403)
        self.assertEqual(self.get("imagesoff", None).status, 401)

    def test_an_administrator_takes_away_and_gives_back_with_the_pages_token_and_the_log_says_so(self):
        r = self.post("noimages", {"account": DOOR_NAMES[DOOR_TARGET]})
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))
        self.assertEqual(set(self.rig.call(self.rig.store.noimages_marks)), {self.ref(DOOR_TARGET)})
        again = self.post("noimages", {"account": DOOR_NAMES[DOOR_TARGET]})
        self.assertEqual((again.status, again.json()), (409, {"error": "bad"}))
        r = self.post("images", {"account": DOOR_NAMES[DOOR_TARGET]})
        self.assertEqual(r.status, 200)
        self.assertEqual(self.rig.call(self.rig.store.noimages_marks), {})
        none = self.post("images", {"account": DOOR_NAMES[DOOR_TARGET]})
        self.assertEqual((none.status, none.json()), (409, {"error": "bad"}))
        rows = self.modlog()
        self.assertEqual([(x.action, x.actor, x.target, x.detail) for x in rows], [("images", self.ref(DOOR_ADMIN), self.ref(DOOR_TARGET), ""), ("noimages", self.ref(DOOR_ADMIN), self.ref(DOOR_TARGET), "")])

    def test_the_door_refuses_what_it_refuses_for_a_ban_a_moderator_a_stranger_a_bad_body_no_token(self):
        for name in ("noimages", "images"):
            self.assertEqual(self.post(name, {"account": DOOR_NAMES[DOOR_TARGET]}, account=DOOR_MOD).status, 403, name)
            self.assertEqual(self.post(name, {"account": DOOR_NAMES[DOOR_TARGET]}, csrf=None).status, 403, name)
            self.assertEqual(self.post(name, {"account": "nobody.at.all"}).status, 404, name)
            self.assertEqual(self.post(name, {"account": DOOR_NAMES[DOOR_TARGET], "x": 1}).status, 400, name)
            self.assertEqual(self.post(name, {"account": 5}).status, 400, name)
        self.assertEqual(self.rig.call(self.rig.store.noimages_marks), {})

    def test_the_table_of_the_powers_an_administrator_on_an_administrator_is_power_and_the_operator_is_above(self):
        r = self.post("noimages", {"account": DOOR_NAMES[DOOR_ADMIN2]})
        self.assertEqual((r.status, r.json()), (403, {"error": "power"}))
        r = self.post("noimages", {"account": DOOR_NAMES[DOOR_OWNER]})
        self.assertEqual(r.status, 403)
        r = self.post("noimages", {"account": DOOR_NAMES[DOOR_ADMIN]})
        self.assertEqual(r.status, 403, "not oneself")
        r = self.post("noimages", {"account": DOOR_NAMES[DOOR_ADMIN2]}, account=DOOR_OWNER)
        self.assertEqual(r.status, 200, "the operator may")
        self.assertEqual(set(self.rig.call(self.rig.store.noimages_marks)), {self.ref(DOOR_ADMIN2)})

    def test_a_flag_that_outlived_its_accounts_other_rows_is_lifted_by_its_reference(self):
        self.post("noimages", {"account": DOOR_NAMES[DOOR_TARGET]})
        ref = self.ref(DOOR_TARGET)
        self.rig.call(self.rig.store.forget, DOOR_TARGET, 0)
        self.assertEqual([r["name"] for r in self.noimages().json()["imagesoff"]], [""], "the name is gone with the profile: the reference is what is shown")
        r = self.post("images", {"account": ref})
        self.assertEqual((r.status, r.json()), (200, {"ok": True}))
        self.assertEqual(self.rig.call(self.rig.store.noimages_marks), {})

    def test_the_status_has_the_numbers_of_the_images_and_no_account(self):
        body = self.get("status").json()["images"]
        self.assertEqual(sorted(body), ["bytes", "bytes_max", "converter", "files", "files_max", "off", "on"])
        self.assertEqual((body["files"], body["off"], body["on"], body["converter"]), (0, 0, False, "off"))

    def test_the_names_of_the_door_are_the_ones_that_nginx_lets_through(self):
        from hub import admindoor
        self.assertIn("imagesoff", admindoor.READS)
        self.assertTrue({"noimages", "images"} <= admindoor.WRITES)
        self.assertFalse(admindoor.READS & admindoor.WRITES, "a name is a read or a change, never both")


if __name__ == "__main__":
    unittest.main()
