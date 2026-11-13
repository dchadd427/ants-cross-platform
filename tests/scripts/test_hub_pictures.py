#!/usr/bin/env python3
"""The Discord picture of an account, kept on the hub's own server (hub/pictures.py, hub/discord.py `picture()`, the store's picture operations; docs/HUB.md "Pictures"; run by ./run_tests.sh --fast and
by the CI), against a FAKE Discord with a fake picture host (tests/scripts/fake_discord.py): real sockets, the real hub. Real Discord is never asked.

  - the check of a PNG on its own: what is kept (a bare PNG of five kinds of chunk) and every file that is refused;
  - the call to the picture host: the address is made from constants and two checked parts, so an odd id or code makes no request at all; no redirect is followed; no more than the cap is read;
  - a login with a picture ends with the picture served at a name, and the login does not wait for it; every way the picture host goes wrong leaves the letter and an older copy as it was
    (a 404 deletes the older copy), and the login's answer is the same;
  - the name is new at every login and two accounts never share one; the copy goes with the last login, Forget me, a kick, the switch, a block, a ban, and is not served past its time;
  - the door: the fixed headers on an answer and on a refusal, a name of any other shape, the limit;
  - no id, picture code, name or reference in the hub's log (in this process and in the real one).
"""
import asyncio
import json
import os
import re
import signal
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import fake_discord as fd  # noqa: E402
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from login_flow import Visitor  # noqa: E402
from hub import config as configmod, discord as discordmod, limits as limitmod, pictures as picturesmod, pseudo  # noqa: E402

CALLBACK = hc.SITE + "/hub/callback"
ACCOUNT = fd.snowflake(900)
OTHER = fd.snowflake(800)
THIRD = fd.snowflake(700)
ADDRESS = "198.51.100.77"
NAME = re.compile(r"[0-9a-f]{32}\Z")
chunk, make_png = fd.chunk, fd.make_png


def kinds_of(text):
    return re.findall(r" picture kind=(\w+)", text)


class ThePngCheck(unittest.TestCase):
    """picture.rewrite on files that are made here, byte by byte."""

    def chunks_of(self, data):
        at, found = 8, []
        while at < len(data):
            length = int.from_bytes(data[at:at + 4], "big")
            found.append(data[at + 4:at + 8])
            at += 12 + length
        self.assertEqual(at, len(data))
        return found

    def test_a_good_file_comes_out_as_a_bare_file_of_five_kinds_of_chunk_at_most(self):
        for color in (0, 2, 3, 4, 6):
            with self.subTest(color=color):
                data = make_png(color=color)
                self.assertEqual(picturesmod.rewrite(data), data)
                self.assertEqual(self.chunks_of(picturesmod.rewrite(data))[0::len(self.chunks_of(data)) - 1], [b"IHDR", b"IEND"])
        self.assertEqual(self.chunks_of(picturesmod.rewrite(make_png(color=3)))[:2], [b"IHDR", b"PLTE"])
        for depth, color in ((1, 0), (2, 0), (4, 0), (16, 0), (16, 2), (1, 3), (8, 3), (16, 4), (16, 6)):
            with self.subTest(depth=depth, color=color):
                picturesmod.rewrite(make_png(depth=depth, color=color))
        picturesmod.rewrite(make_png(8, 8))
        picturesmod.rewrite(make_png(128, 128))
        picturesmod.rewrite(make_png(interlace=1))

    def test_every_other_chunk_is_dropped_and_the_rest_is_what_was_there(self):
        extra = [(b"tEXt", b"Comment\x00canary-text"), (b"eXIf", b"canary-exif" * 20), (b"gAMA", b"\x00\x01\x86\xa0"), (b"iCCP", b"canary\x00\x00x"), (b"acTL", b"\x00\x00\x00\x02\x00\x00\x00\x00"),
                 (b"zzzz", b"private")]
        data = make_png(before=extra, after=[(b"fdAT", b"\x00\x00\x00\x00canary"), (b"tIME", b"\x07\xe9\x0b\x0d\x00\x00\x00")])
        out = picturesmod.rewrite(data)
        self.assertEqual(self.chunks_of(out), [b"IHDR", b"IDAT", b"IEND"])
        self.assertNotIn(b"canary", out)
        self.assertEqual(out, make_png())
        with_alpha = make_png(color=2, before=[(b"tRNS", b"\x00\x01\x00\x02\x00\x03")])
        self.assertEqual(self.chunks_of(picturesmod.rewrite(with_alpha)), [b"IHDR", b"tRNS", b"IDAT", b"IEND"])

    def test_a_file_that_is_not_a_whole_png_of_a_small_picture_is_refused(self):
        good = make_png()
        ihdr = lambda w, h, depth=8, color=2, interlace=0, comp=0, filt=0: chunk(b"IHDR", w.to_bytes(4, "big") + h.to_bytes(4, "big") + bytes([depth, color, comp, filt, interlace]))      # noqa: E731
        zlib = __import__("zlib")

        def D(w=64, h=64, depth=8, color=2, interlace=0, *more):
            """a picture data chunk that is a whole zlib stream of exactly the rows that the header says, where the header is one that can have rows (the others are refused at the header)"""
            if not (0 < w <= 5000 and 0 < h <= 5000 and color in (0, 2, 3, 4, 6) and depth in (1, 2, 4, 8, 16) and interlace == 0):
                return chunk(b"IDAT", b"\x78\x9c\x03\x00\x00\x00\x00\x01")
            return chunk(b"IDAT", zlib.compress((b"\x00" + b"\x11" * -(-w * {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color] * depth // 8)) * h))
        iend = chunk(b"IEND", b"")
        sig = good[:8]
        bad_crc = bytearray(good)
        bad_crc[good.index(b"IDAT") + 6] ^= 1
        bad_ihdr_crc = bytearray(good)
        bad_ihdr_crc[29] ^= 1
        cases = {
            "not bytes": "text", "empty": b"", "signature only": sig, "wrong signature": b"\x89PNX" + good[4:], "jpeg": b"\xff\xd8\xff\xe0" + b"x" * 100, "svg": b"<svg xmlns='http://www.w3.org/2000/svg'/>",
            "cut off": good[:len(good) - 5], "cut in a head": good[:20], "a crc that is wrong": bytes(bad_crc), "a header crc that is wrong": bytes(bad_ihdr_crc),
            "a byte after IEND": good + b"\x00", "a chunk after IEND": good + chunk(b"tEXt", b"x"), "no IEND": good[:-12], "IEND with a body": sig + ihdr(64, 64) + D(64, 64) + chunk(b"IEND", b"x"),
            "a first chunk that is not the header": sig + chunk(b"IDAT", ihdr(64, 64)[8:-4]) + D(64, 64) + iend, "no header first": sig + D() + iend, "a text chunk first": sig + chunk(b"tEXt", b"x") + ihdr(64, 64) + D(64, 64) + iend,
            "a header of 12 bytes": sig + chunk(b"IHDR", b"\x00" * 12) + D() + iend, "a second header": sig + ihdr(64, 64) + ihdr(64, 64) + D(64, 64) + iend,
            "4096 pixels wide": sig + ihdr(4096, 64) + D(4096, 64) + iend, "4096 pixels high": sig + ihdr(64, 4096) + D(64, 4096) + iend, "7 pixels": sig + ihdr(7, 64) + D(7, 64) + iend, "129 pixels": sig + ihdr(64, 129) + D(64, 129) + iend,
            "0 pixels": sig + ihdr(0, 0) + D(0, 0) + iend, "a side of two billion": sig + ihdr(2 ** 31, 64) + D(2 ** 31, 64) + iend, "a color type that is not one": sig + ihdr(64, 64, 8, 5) + D(64, 64, 8, 5) + iend,
            "a depth that the color does not take": sig + ihdr(64, 64, 4, 2) + D(64, 64, 4, 2) + iend, "a depth that is not one": sig + ihdr(64, 64, 7, 0) + D(64, 64, 7, 0) + iend,
            "interlace 2": sig + ihdr(64, 64, 8, 2, 2) + D(64, 64, 8, 2, 2) + iend, "a compression method": sig + ihdr(64, 64, 8, 2, 0, 1) + D(64, 64, 8, 2, 0, 1) + iend, "a filter method": sig + ihdr(64, 64, 8, 2, 0, 0, 1) + D(64, 64, 8, 2, 0, 0, 1) + iend,
            "no picture data": sig + ihdr(64, 64) + iend, "empty picture data": sig + ihdr(64, 64) + chunk(b"IDAT", b"") + iend,
            "picture data in two runs": sig + ihdr(64, 64) + chunk(b"IDAT", zlib.compress((b"\x00" + b"\x11" * 192) * 64)[:20]) + chunk(b"tEXt", b"x") + chunk(b"IDAT", zlib.compress((b"\x00" + b"\x11" * 192) * 64)[20:]) + iend,
            "a length past the end": sig + ihdr(64, 64) + (1000).to_bytes(4, "big") + b"IDAT" + b"x" * 20, "a length of two billion": sig + ihdr(64, 64) + (2 ** 31).to_bytes(4, "big") + b"IDAT" + b"x" * 20,
            "a length of four billion": sig + ihdr(64, 64) + b"\xff\xff\xff\xff" + b"IDAT" + b"x" * 20, "a length that overflows into the crc": sig + ihdr(64, 64) + (9).to_bytes(4, "big") + b"IDAT" + b"x" * 12,
            "a chunk name of digits": sig + ihdr(64, 64) + chunk(b"1234", b"x") + D(64, 64) + iend, "a critical chunk that is not known": sig + ihdr(64, 64) + chunk(b"ABCD", b"x") + D(64, 64) + iend,
            "a palette for grey": sig + ihdr(64, 64, 8, 0) + chunk(b"PLTE", b"\x00" * 3) + D(64, 64, 8, 0) + iend, "a palette of 4 bytes": sig + ihdr(64, 64, 8, 3) + chunk(b"PLTE", b"\x00" * 4) + D(64, 64, 8, 3) + iend,
            "an empty palette": sig + ihdr(64, 64, 8, 3) + chunk(b"PLTE", b"") + D(64, 64, 8, 3) + iend, "a palette of 300 colors": sig + ihdr(64, 64, 8, 3) + chunk(b"PLTE", b"\x00" * 900) + D(64, 64, 8, 3) + iend,
            "a palette type with no palette": sig + ihdr(64, 64, 8, 3) + D(64, 64, 8, 3) + iend, "two palettes": sig + ihdr(64, 64, 8, 3) + chunk(b"PLTE", b"\x00" * 6) * 2 + D(64, 64, 8, 3) + iend,
            "a palette after the data": sig + ihdr(64, 64, 8, 3) + chunk(b"PLTE", b"\x00" * 6) + D(64, 64, 8, 3) + chunk(b"PLTE", b"\x00" * 6) + iend,
            "transparency of the wrong length": sig + ihdr(64, 64, 8, 2) + chunk(b"tRNS", b"\x00") + D(64, 64, 8, 2) + iend, "transparency for a type that has an alpha channel": sig + ihdr(64, 64, 8, 6) + chunk(b"tRNS", b"\x00") + D(64, 64, 8, 6) + iend,
        }
        for name, data in cases.items():
            with self.subTest(name):
                with self.assertRaises(picturesmod.ShapeError):
                    picturesmod.rewrite(data)

    def test_the_data_must_be_a_whole_zlib_stream_that_inflates_to_exactly_the_rows_the_header_says(self):
        import struct
        import zlib

        def made(raw, width=64, height=64, color=2, depth=8, interlace=0, tail=b"", stream=None):
            data = zlib.compress(raw) + tail if stream is None else stream
            return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, interlace)) + chunk(b"IDAT", data) + chunk(b"IEND", b""))

        whole = zlib.compress(rows64 := (b"\x00" + b"\x11" * (64 * 3)) * 64)
        unfinished = zlib.compressobj()
        unfinished = unfinished.compress(rows64) + unfinished.flush(zlib.Z_SYNC_FLUSH)        # (all the rows, and no end of the stream)

        rows = b"\x00" + b"\x11" * (64 * 3)
        self.assertEqual(picturesmod.rewrite(made(rows * 64)), made(rows * 64))
        cases = {
            "a stream that inflates to 32 MiB under a 128 pixel header": made(b"\x00" * (32 * 2 ** 20), 128, 128, 6, 16),
            "bytes that are no zlib stream": made(b"", stream=b"<script>alert(1)</script>"),
            "a row too few": made(rows * 63), "a byte too many": made(rows * 64 + b"\x00"), "a byte too few": made((rows * 64)[:-1]),
            "a stream that is cut off": made(b"", stream=whole[:-4]), "a stream that has all the rows and no end": made(b"", stream=unfinished),
            "bytes after the end of the stream": made(rows * 64, tail=b"x"),
            "data that is not the rows of an interlaced file": made(rows * 64, interlace=1),
            "data for the other color": made(rows * 64, color=6),
        }
        for name, data in cases.items():
            with self.subTest(name):
                with self.assertRaises(picturesmod.ShapeError):
                    picturesmod.rewrite(data)

    def test_the_size_of_the_rows_is_what_the_format_says_for_every_color_depth_size_and_interlace(self):
        self.assertEqual(picturesmod.raw_size(64, 64, 2, 8, 0), 64 * (1 + 192))
        self.assertEqual(picturesmod.raw_size(13, 17, 0, 1, 0), 17 * (1 + 2))
        self.assertEqual(picturesmod.raw_size(128, 128, 6, 16, 0), 128 * (1 + 1024))
        self.assertEqual(picturesmod.raw_size(8, 8, 0, 8, 1), 1 * (1 + 1) + 1 * (1 + 1) + 1 * (1 + 2) + 2 * (1 + 2) + 2 * (1 + 4) + 4 * (1 + 4) + 4 * (1 + 8))

    def test_a_refusal_says_nothing_of_the_file(self):
        with self.assertRaises(picturesmod.ShapeError) as e:
            picturesmod.rewrite(b"\x89PNG\r\n\x1a\ncanary-bytes-1234")
        self.assertNotIn("canary", repr(e.exception) + str(e.exception))


class TheCallToThePictureHost(unittest.TestCase):
    """hub/discord.py on its own, against the fake picture host."""

    def setUp(self):
        self.fake = fd.FakeDiscord(CALLBACK, user=(ACCOUNT, "maple.leaf"))
        self.addCleanup(self.fake.close)

    def client(self, **limits):
        env = {"ANTS_HUB_PUBLIC_URL": hc.SITE, "ANTS_HUB_DISCORD_CLIENT_ID": self.fake.client_id, "ANTS_HUB_DISCORD_CLIENT_SECRET": self.fake.client_secret, "ANTS_HUB_HOST": "127.0.0.1",
               "ANTS_HUB_TEST_DISCORD_URL": self.fake.url}
        return discordmod.Discord(configmod.load(env), limitmod.Limits(**limits))

    def picture(self, client, account=ACCOUNT, avatar=fd.AVATAR):
        return asyncio.run(client.picture(account, avatar))

    def asked(self):
        return [r for r in self.fake.requests if r[1].startswith("/cdn/")]

    def who(self, avatar):
        self.fake.avatar = avatar
        code = "code-%d" % len(self.fake.codes)
        self.fake.codes[code] = self.fake.user
        return asyncio.run(self.client().who(code))

    def test_the_picture_comes_with_its_status_its_type_and_its_bytes_and_the_request_is_a_plain_one(self):
        status, content_type, body = self.picture(self.client())
        self.assertEqual((status, content_type, body), (200, "image/png", self.fake.picture))
        (method, path, query, headers, _), = self.asked()
        self.assertEqual((method, path, query), ("GET", "/cdn/avatars/%s/%s.png" % (ACCOUNT, fd.AVATAR), "size=64"))
        self.assertEqual(headers["accept"], "image/png")
        self.assertEqual(headers["accept-encoding"], "identity")
        self.assertTrue(headers["user-agent"].startswith("AntsHub/1"))
        for name in ("cookie", "authorization", "origin", "referer"):
            self.assertNotIn(name, headers)

    def test_an_id_or_a_code_that_is_not_the_shape_of_one_makes_no_request_at_all(self):
        ids = ["", "0", "01", "12a", "1" * 21, "../1", "1/2", "1%2f2", "1?x=1", "1#x", "1@x", "1 2", "1\n", "\uff11\uff12", 12345, None, ["1"], {"1": 1}, b"123"]
        codes = ["", "../x", "/", "%2f", "?", "#", "@", " ", "a_", fd.AVATAR[:-1], fd.AVATAR + "0", fd.AVATAR.upper(), fd.AVATAR + "\n", "x" * 32, "a_" + fd.AVATAR + "0", "b_" + fd.AVATAR,
                 "a_a_" + fd.AVATAR[:30], 12345, None, [fd.AVATAR], {"a": 1}, fd.AVATAR[:16] + "/" + fd.AVATAR[17:], fd.AVATAR[:16] + "." + fd.AVATAR[17:], fd.AVATAR[:30] + "%0"]
        client = self.client()
        for odd in ids:
            with self.subTest(account=repr(odd)), self.assertRaises(discordmod.DiscordError):
                self.picture(client, account=odd)
        for odd in codes:
            with self.subTest(code=repr(odd)), self.assertRaises(discordmod.DiscordError):
                self.picture(client, avatar=odd)
        self.assertEqual(self.asked(), [])
        self.picture(client, avatar="a_" + fd.AVATAR)                         # (the animated one's code is a code: its .png is the still picture)
        self.assertEqual(self.asked()[0][1], "/cdn/avatars/%s/a_%s.png" % (ACCOUNT, fd.AVATAR))

    def test_the_login_reads_the_code_and_keeps_it_only_when_it_has_the_shape_of_one(self):
        self.assertEqual(self.who(fd.AVATAR)[3], fd.AVATAR)
        self.assertEqual(self.who("a_" + fd.AVATAR)[3], "a_" + fd.AVATAR)
        for odd in (None, "canary-avatar-hash", "", fd.AVATAR.upper(), fd.AVATAR + "0", "../" + fd.AVATAR, 12345, ["x"], {"a": 1}, True):
            with self.subTest(code=repr(odd)):
                account, name, shown, avatar = self.who(odd)
                self.assertEqual((account, name, avatar), (ACCOUNT, "maple.leaf", None))

    def test_a_redirect_is_not_followed(self):
        self.fake.cdn_mode = "redirect"
        with self.assertRaises(discordmod.DiscordError) as e:
            self.picture(self.client())
        self.assertEqual(e.exception.kind, "down")
        self.assertEqual([r[1] for r in self.asked()], ["/cdn/avatars/%s/%s.png" % (ACCOUNT, fd.AVATAR)])        # (the address the redirect named was never asked for)

    def test_no_more_than_the_cap_is_read_and_a_slow_host_is_cut_off(self):
        for mode in ("huge", "huge_stream", "huge_chunked"):
            self.fake.cdn_mode = mode
            started = time.monotonic()
            with self.assertRaises(discordmod.DiscordError) as e:
                self.picture(self.client(discord_seconds=3.0))
            self.assertEqual(e.exception.kind, "big", mode)
            self.assertLess(time.monotonic() - started, 2.0, mode)
            time.sleep(0.3)
            self.assertLess(self.fake.sent, 30000000, "%s: the hub kept reading (%d bytes of about 98 million)" % (mode, self.fake.sent))
        self.fake.cdn_mode = "drip"
        started = time.monotonic()
        with self.assertRaises(discordmod.DiscordError) as e:
            self.picture(self.client(discord_seconds=1.0))
        self.assertEqual(e.exception.kind, "down")
        self.assertLess(time.monotonic() - started, 3.0)

    def test_the_read_cap_is_the_limit_and_not_a_number_of_the_code(self):
        self.fake.picture = make_png(before=[(b"tEXt", b"x" * 3000)])
        self.assertEqual(self.picture(self.client())[0], 200)
        with self.assertRaises(discordmod.DiscordError) as e:
            self.picture(self.client(picture_read_max=2048))
        self.assertEqual(e.exception.kind, "big")

    def test_an_error_has_no_cause_and_says_nothing_of_the_address(self):
        for mode in ("close", "partial", "huge", "redirect", "500"):
            self.fake.cdn_mode = mode
            try:
                status = self.picture(self.client())[0]
            except discordmod.DiscordError as e:
                self.assertIsNone(e.__cause__, mode)
                self.assertIsNone(e.__context__, mode)                                    # (nothing of what went wrong first hangs on it either)
                self.assertEqual(sorted(vars(e)), ["kind", "status"])
                self.assertNotIn(ACCOUNT, repr(e) + str(e))
                self.assertNotIn(fd.AVATAR, repr(e) + str(e))
            else:
                self.assertEqual((mode, status), ("500", 500))

    def test_the_fixed_host_is_the_images_and_plain_http_is_only_for_the_fake(self):
        env = {"ANTS_HUB_PUBLIC_URL": hc.SITE, "ANTS_HUB_DISCORD_CLIENT_ID": self.fake.client_id, "ANTS_HUB_DISCORD_CLIENT_SECRET": self.fake.client_secret}
        real = discordmod.Discord(configmod.load(env), limitmod.Limits())
        self.assertEqual(real.cdn, "https://cdn.discordapp.com")
        real.cdn = self.fake.url + "/cdn"                                      # (as if a bug or an edit pointed it at a plain address)
        with self.assertRaises(discordmod.DiscordError):
            self.picture(real)
        self.assertEqual(self.asked(), [])


class PictureTest(unittest.TestCase):
    """The real hub with the fake Discord and its picture host."""

    limits = {}
    env = None

    def setUp(self):
        self.start()
        self.addCleanup(self.finish)

    def start(self):
        self.fake = fd.FakeDiscord(CALLBACK, user=(ACCOUNT, "maple.leaf"))
        self.fake.consent = False
        self.fake.avatar = fd.AVATAR
        self.rig = hub_rig.Rig(discord=self.fake, env=self.env, **dict(dict(sockets_per_address=100), **self.limits))
        self.rig.__enter__()
        self.count = 0

    def restart(self, **limits):
        """The hub again, with other limits (and a new fake Discord)."""
        self.finish()
        self.limits = limits
        self.start()

    def finish(self):
        self.rig.__exit__(None, None, None)
        self.fake.close()

    # ---- helpers -----------------------------------------------------------------------------------------------------------------------------

    def visitor(self):
        self.count += 1
        return Visitor(self.rig.port, self.fake, address="198.51.100.%d" % self.count)

    def login(self, user=None):
        if user:
            self.fake.user = user
        v = self.visitor()
        r = v.login()
        self.assertEqual((r.status, r.get("location")), (302, "/"), r.text())
        return v

    def log(self):
        return self.rig.log_text.getvalue()

    def settle(self, count=1, since=0):
        """The kinds of the picture lines of the log, once there are `count` more than there were `since`."""
        for _ in range(200):
            kinds = kinds_of(self.log())
            if len(kinds) >= since + count:
                time.sleep(0.05)
                return kinds_of(self.log())
            time.sleep(0.05)
        self.fail("no picture line in the log: " + self.log())

    def until_logged(self, text, count=1):
        for _ in range(200):
            if self.log().count(text) >= count:
                time.sleep(0.05)
                return
            time.sleep(0.05)
        self.fail("%r not in the log: %s" % (text, self.log()))

    def dropped(self):
        return sum(int(n) for n in re.findall(r" picture kind=busy dropped=(\d+)", self.log()))

    def rows(self):
        db = sqlite3.connect(self.rig.db)
        try:
            return db.execute("SELECT ref, show, name, png, until FROM pictures ORDER BY rowid").fetchall()
        finally:
            db.close()

    def kept(self):
        return [r for r in self.rows() if r[3] is not None]

    def ref(self, account=ACCOUNT):
        return pseudo.reference(self.rig.call(self.rig.store.secret), account)

    def name(self, account=ACCOUNT):
        return self.rig.call(self.rig.store.picture_names).get(self.ref(account))

    def get(self, name, method="GET", address=ADDRESS, path=None):
        return hc.request(self.rig.port, method, path or "/hub/pic/%s.png" % name, [("X-Client-Address", address)])

    def asked(self):
        return [r for r in self.fake.requests if r[1].startswith("/cdn/")]

    def store(self, function, *args):
        return self.rig.call(getattr(self.rig.store, function), *args)


class TheLoginWithAPicture(PictureTest):
    def test_a_login_with_a_picture_ends_with_the_picture_served_at_a_name(self):
        self.login()
        self.assertEqual(self.settle(), ["ok"])
        name = self.name()
        self.assertRegex(name, NAME)
        r = self.get(name)
        self.assertEqual((r.status, r.body), (200, picturesmod.rewrite(self.fake.picture)))
        self.assertEqual(self.asked()[0][1], "/cdn/avatars/%s/%s.png" % (ACCOUNT, fd.AVATAR))

    def test_what_is_served_has_the_five_kinds_of_chunk_at_most_and_nothing_of_what_the_host_sent_besides(self):
        self.fake.picture = make_png(color=3, before=[(b"tEXt", b"canary-text"), (b"tRNS", b"\x00\x01"), (b"eXIf", b"canary-exif")], after=[(b"tIME", b"\x07\xe9\x0b\x0d\x00\x00\x00")])
        self.login()
        self.settle()
        body = self.get(self.name()).body
        kinds, at = [], 8
        while at < len(body):
            kinds.append(body[at + 4:at + 8])
            at += 12 + int.from_bytes(body[at:at + 4], "big")
        self.assertEqual(kinds, [b"IHDR", b"PLTE", b"tRNS", b"IDAT", b"IEND"])
        self.assertNotIn(b"canary", body)

    def test_the_answer_has_the_same_headers_every_time_and_a_refusal_has_none_of_them(self):
        self.login()
        self.settle()
        r = self.get(self.name())
        got = {k: v for k, v in r.headers}
        self.assertEqual(got["content-type"], "image/png")
        self.assertEqual(got["x-content-type-options"], "nosniff")
        self.assertEqual(got["content-security-policy"], "default-src 'none'; sandbox")
        self.assertEqual(got["cross-origin-resource-policy"], "same-origin")
        self.assertEqual(got["cache-control"], "private, max-age=86400, immutable")
        self.assertEqual(got["content-length"], str(len(r.body)))
        for name in ("set-cookie", "access-control-allow-origin", "etag", "last-modified", "content-disposition"):
            self.assertNotIn(name, got)
        self.assertEqual([k for k, _ in r.headers].count("cache-control"), 1)
        gone = self.get("0" * 32)
        got = {k: v for k, v in gone.headers}
        self.assertEqual((gone.status, gone.json(), got["content-type"], got["cache-control"], got["x-content-type-options"]), (404, {"error": "not_found"}, "application/json", "no-store", "nosniff"))
        self.assertNotIn("content-security-policy", got)

    def test_the_login_does_not_wait_for_a_slow_picture_host_and_its_answer_is_the_same_one(self):
        self.login()
        self.settle()
        self.fake.cdn_mode = "slow:2.0"
        started = time.monotonic()
        v = self.visitor()
        r = v.login()
        took = time.monotonic() - started
        self.assertEqual((r.status, r.get("location")), (302, "/"))
        self.assertLess(took, 1.5)
        self.assertEqual(self.settle(2), ["ok", "ok"])                                  # (the slow fetch ends by itself, a login later)

    def test_every_way_the_picture_host_goes_wrong_leaves_the_letter_and_the_login_the_same(self):
        oversize = make_png(before=[(b"tEXt", b"x" * 40000)])
        bad_crc = bytearray(make_png())
        bad_crc[40] ^= 1
        text = b"<html>nope</html>"
        modes = [("404", "none"), ("500", "down"), ("status:429", "down"), ("status:403", "down"), ("redirect", "down"), ("type:text/html", "type"), ("type:image/png; charset=utf-8", "type"), ("type:image/jpeg", "type"),
                 ("notype", "type"), ("close", "down"), ("partial", "down"), ("hang", "down"), ("drip", "down"), ("huge", "big"), ("huge_stream", "big"), ("huge_chunked", "big")]
        bodies = [(text, "shape"), (b"", "shape"), (bytes(bad_crc), "shape"), (make_png(4096, 8), "shape"), (make_png(8, 4096), "shape"), (make_png()[:-3], "shape"), (make_png() + b"x", "shape"),
                  (oversize, "big"), (b"\x89PNG\r\n\x1a\n" + b"\x00" * 40, "shape"), (b"<svg xmlns='http://www.w3.org/2000/svg'><script>1</script></svg>", "shape")]
        seen = []
        for index, (mode, kind) in enumerate([(m, k) for m, k in modes] + [("ok", k) for _, k in bodies]):
            self.fake.cdn_mode = mode
            if mode == "ok":
                self.fake.picture = bodies[index - len(modes)][0]
            before = len(kinds_of(self.log()))
            started = time.monotonic()
            v = self.visitor()
            r = v.login()
            self.assertLess(time.monotonic() - started, 2.0, mode)
            seen.append((r.status, r.get("location")))
            self.assertEqual(self.settle(1, before)[before], kind, (mode, index))
            self.assertEqual(self.kept(), [], (mode, index))
        self.assertEqual(set(seen), {(302, "/")})
        self.assertNotIn("/cdn/followed.png", [r[1] for r in self.fake.requests])

    def test_the_picture_that_a_failure_could_not_replace_stays_and_a_404_takes_it_away(self):
        self.login()
        self.settle()
        first = self.kept()
        self.assertEqual(len(first), 1)
        for mode in ("500", "type:text/html", "huge", "redirect"):
            self.fake.cdn_mode = mode
            before = len(kinds_of(self.log()))
            self.login()
            self.settle(1, before)
            self.assertEqual(self.kept(), first, mode)
            self.assertEqual(self.get(first[0][2]).status, 200)
        self.fake.cdn_mode = "ok"
        self.fake.picture = b"not a picture"
        before = len(kinds_of(self.log()))
        self.login()
        self.settle(1, before)
        self.assertEqual(self.kept(), first)
        self.fake.cdn_mode = "404"
        before = len(kinds_of(self.log()))
        self.login()
        self.assertEqual(self.settle(1, before)[-1], "none")
        self.assertEqual(self.rows(), [])
        self.assertEqual(self.get(first[0][2]).status, 404)

    def test_an_account_with_no_picture_at_discord_loses_the_copy_it_had_and_asks_for_none(self):
        self.login()
        self.settle()
        name = self.name()
        asked = len(self.asked())
        self.fake.avatar = None
        self.login()
        self.assertEqual(self.rows(), [])
        self.assertEqual(self.get(name).status, 404)
        self.assertEqual(len(self.asked()), asked)
        self.assertEqual(kinds_of(self.log())[-1], "none")

    def test_a_code_that_is_not_the_shape_of_one_asks_for_nothing_and_the_login_is_the_same(self):
        for odd in ("../../x", "canary-avatar-hash", fd.AVATAR.upper(), "a_" + "z" * 32, 12345, ["x"]):
            self.fake.avatar = odd
            self.login()
        time.sleep(0.3)
        self.assertEqual((self.asked(), self.rows(), kinds_of(self.log())), ([], [], ["none"] * 6))             # (a code that is not one is no picture: the same as none)

    def test_every_login_gets_a_new_name_and_the_old_one_is_gone(self):
        names = []
        for _ in range(3):
            before = len(kinds_of(self.log()))
            self.login()
            self.settle(1, before)
            names.append(self.name())
        self.assertEqual(len(set(names)), 3)
        self.assertTrue(all(NAME.match(n) for n in names))
        self.assertEqual([self.get(n).status for n in names], [404, 404, 200])
        self.assertEqual(len(self.kept()), 1)

    def test_a_name_is_not_made_from_anything_of_the_account_the_code_or_the_bytes(self):
        import hashlib
        self.login()
        self.settle()
        name = self.name()
        png = picturesmod.rewrite(self.fake.picture)
        secret = self.rig.call(self.rig.store.secret)
        for source in (ACCOUNT.encode(), fd.AVATAR.encode(), png, self.ref().encode(), secret, ACCOUNT.encode() + fd.AVATAR.encode()):
            for digest in (hashlib.sha256, hashlib.md5, hashlib.sha1, hashlib.blake2b):
                self.assertNotIn(name, digest(source).hexdigest())
        self.assertNotIn(self.ref(), name)
        self.assertNotIn(ACCOUNT, name)

    def test_two_accounts_never_share_a_name_and_each_has_its_own_row(self):
        names = {}
        for account in (ACCOUNT, OTHER, THIRD):
            before = len(kinds_of(self.log()))
            self.login((account, "user" + account[-3:]))
            self.settle(1, before)
            names[account] = self.name(account)
        self.assertEqual(len(set(names.values())), 3)
        self.assertEqual(len(self.kept()), 3)
        self.assertEqual({r[0] for r in self.kept()}, {self.ref(a) for a in names})

    def test_logins_of_one_account_while_its_fetch_is_on_its_way_are_one_fetch_and_the_next_login_is_a_new_one(self):
        self.fake.cdn_mode = "slow:0.6"
        for _ in range(3):
            self.login()
        self.until_logged("picture kind=ok")
        self.rig.call(self.rig.hub.sessions.pictures.forget_idle, self.rig.hub.clock())
        self.assertEqual((len(self.asked()), self.dropped()), (1, 2))                 # (the two that came while it ran were dropped and counted)
        self.fake.cdn_mode = "ok"
        self.login()                                                                 # (a login after it: a fetch of its own, at once)
        self.until_logged("picture kind=ok", 2)
        self.assertEqual(len(self.asked()), 2)

    def test_logins_that_come_together_make_two_fetches_at_the_most_at_a_time_and_every_picture(self):
        self.fake.cdn_mode = "slow:0.6"
        accounts = (ACCOUNT, OTHER, THIRD, fd.snowflake(600))
        for account in accounts:
            self.login((account, "user" + account[-3:]))                           # (a login is quick: the fetches of the four overlap)
        self.assertEqual(self.settle(4), ["ok"] * 4)
        self.assertEqual(self.fake.cdn_peak, 2)
        self.assertEqual(len(self.kept()), 4)

    def test_a_queue_that_is_full_drops_the_fetches_and_the_logins_are_the_same(self):
        self.restart(picture_waiting=1, picture_inflight=1)
        self.fake.cdn_mode = "slow:0.8"
        for account in (ACCOUNT, OTHER, THIRD, fd.snowflake(600), fd.snowflake(500)):
            self.assertEqual(self.login((account, "user" + account[-3:])).session() is not None, True)
        self.settle(2)
        time.sleep(1.0)
        self.assertIn("picture kind=busy dropped=1", self.log())                     # (the first drop is told at once)
        pictures = self.rig.hub.sessions.pictures
        self.rig.call(pictures.forget_idle, self.rig.hub.clock())                    # (what is counted and not told yet is told by the clock's minute)
        self.assertEqual([k for k in kinds_of(self.log()) if k != "busy"], ["ok", "ok"])        # (one ran, one waited, the others were dropped: letters)
        self.assertEqual(self.dropped(), 3)
        self.assertNotIn(ACCOUNT, self.log())
        self.assertEqual(len(self.kept()), 2)

    def test_a_burst_of_fetches_goes_at_the_pace_of_the_whole_hub(self):
        self.restart(picture_burst=1, picture_refill_seconds=0.5, picture_inflight=2)
        started = time.monotonic()
        for account in (ACCOUNT, OTHER, THIRD):
            self.login((account, "user" + account[-3:]))
        self.settle(3)
        self.assertGreaterEqual(time.monotonic() - started, 0.9)


class TheEndOfAPicture(PictureTest):
    def kept_after_login(self, user=None):
        before = len(kinds_of(self.log()))
        v = self.login(user)
        self.settle(1, before)
        return v

    def test_the_last_log_out_takes_the_picture_and_one_with_another_login_left_does_not(self):
        first = self.kept_after_login()
        second = self.kept_after_login()
        name = self.name()
        self.assertEqual(first.call("POST", "/hub/logout").status, 200)
        self.assertEqual(self.get(name).status, 200)
        self.assertEqual(len(self.kept()), 1)
        self.assertEqual(second.call("POST", "/hub/logout").status, 200)
        self.assertEqual(self.get(name).status, 404)
        self.assertEqual(self.rows(), [])

    def test_a_log_out_of_another_account_leaves_a_picture_alone(self):
        mine = self.kept_after_login()
        other = self.kept_after_login((OTHER, "other.one"))
        self.assertEqual(other.call("POST", "/hub/logout").status, 200)
        self.assertEqual([r[0] for r in self.kept()], [self.ref()])
        self.assertEqual(self.get(self.name()).status, 200)
        self.assertIsNotNone(mine.session())

    def test_forget_me_takes_the_picture_and_the_name(self):
        v = self.kept_after_login()
        name = self.name()
        self.assertEqual(v.call("POST", "/hub/forget").status, 200)
        self.assertEqual(self.rows(), [])
        self.assertEqual(self.get(name).status, 404)

    def test_a_kick_takes_the_picture_with_the_logins(self):
        self.kept_after_login()
        name = self.name()
        self.assertEqual(self.store("end_sessions", ACCOUNT), 1)
        self.assertEqual(self.rows(), [])
        self.assertEqual(self.get(name).status, 404)

    def test_a_picture_is_not_served_past_the_time_of_its_login_and_the_purge_deletes_it(self):
        self.kept_after_login()
        name = self.name()
        self.rig.call(lambda: self.rig.store.db.execute("UPDATE pictures SET until = ?", (int(time.time()) - 1,)))
        self.assertEqual(self.get(name).status, 404)
        self.assertEqual(len(self.rows()), 1)
        self.store("purge")
        self.assertEqual(self.rows(), [])

    def test_the_time_of_a_picture_is_the_time_of_the_login_that_fetched_it(self):
        started = time.time()
        self.kept_after_login()
        until = self.kept()[0][4]
        self.assertAlmostEqual(until - started, self.rig.limits.session_seconds, delta=10)

    def test_a_picture_that_is_switched_off_or_blocked_is_not_fetched_and_not_served(self):
        for state in (0, 2):
            with self.subTest(state=state):
                self.store("set_picture_show", ACCOUNT, state, True)
                asked = len(self.asked())
                self.login()
                time.sleep(0.4)
                self.assertEqual((len(self.asked()), self.kept()), (asked, []))
                self.assertEqual([r[1] for r in self.rows()], [state])
                self.assertEqual(kinds_of(self.log()), [])
        self.store("set_picture_show", ACCOUNT, 1, True)
        self.kept_after_login()
        self.assertEqual(self.get(self.name()).status, 200)

    def test_switching_it_off_deletes_the_copy_at_once_and_the_old_name_is_gone(self):
        self.kept_after_login()
        name = self.name()
        self.assertEqual(self.store("set_picture_show", ACCOUNT, 0), 0)
        self.assertEqual(self.kept(), [])
        self.assertEqual(self.get(name).status, 404)
        self.assertEqual(self.store("set_picture_show", ACCOUNT, 1), 1)
        self.assertEqual(self.rows(), [])                                              # (back on: nothing to show until the next login)

    def test_a_picture_of_a_banned_account_is_not_fetched_and_one_it_had_is_taken(self):
        self.kept_after_login()
        name = self.name()
        self.store("ban", ACCOUNT, "command line", "maple.leaf")
        self.login()
        time.sleep(0.4)
        self.assertEqual(self.kept(), [])
        self.assertEqual(self.get(name).status, 404)
        self.assertEqual(len(self.asked()), 1)


class TheLateFetch(PictureTest):
    """A fetch that ends after the login, the account or the person was taken away keeps nothing."""

    def late(self, act, nothing_at_all=True):
        self.fake.cdn_mode = "slow:0.8"
        v = self.login()
        time.sleep(0.2)                                                              # (the fetch is on its way)
        act(v)
        self.assertEqual(self.settle(), ["none"])
        time.sleep(0.2)
        self.assertEqual(self.kept(), [])
        self.assertEqual(self.rows() if nothing_at_all else [], [])                  # (a switch that was made stays as its row, with no picture)
        self.assertEqual(self.rig.call(self.rig.store.picture_names), {})

    def test_forget_me(self):
        self.late(lambda v: self.assertEqual(v.call("POST", "/hub/forget").status, 200))

    def test_the_last_log_out(self):
        self.late(lambda v: self.assertEqual(v.call("POST", "/hub/logout").status, 200))

    def test_a_kick(self):
        self.late(lambda v: self.store("end_sessions", ACCOUNT))

    def test_a_ban(self):
        self.late(lambda v: self.store("ban", ACCOUNT, "command line", "maple.leaf"))

    def test_the_switch_off(self):
        self.late(lambda v: self.store("set_picture_show", ACCOUNT, 0), nothing_at_all=False)

    def test_a_log_out_with_another_login_left_does_not_stop_the_fetch(self):
        other = self.login()
        self.fake.cdn_mode = "slow:0.8"
        self.login()
        time.sleep(0.2)
        self.assertEqual(other.call("POST", "/hub/logout").status, 200)
        self.assertEqual(self.settle(2)[-1], "ok")
        self.assertEqual(len(self.kept()), 1)


class TheStoreEndsAPictureWithTheLastLogin(PictureTest):
    def test_a_browser_that_logs_in_as_another_account_ends_the_picture_of_the_first(self):
        v = self.login()
        self.settle()
        name = self.name()
        self.fake.user = (OTHER, "other.one")
        r = v.login()                                                                # (the same browser: the callback replaces its old login, which was the first account's last)
        self.assertEqual(r.status, 302)
        self.assertEqual(self.get(name).status, 404)
        self.assertEqual(self.settle(2)[-1], "ok")
        self.assertEqual([r[0] for r in self.kept()], [self.ref(OTHER)])

    def test_two_logins_of_which_the_newer_is_logged_out_leave_no_picture_when_the_older_runs_out(self):
        self.login()
        self.settle()
        newer = self.login()
        self.settle(2)
        self.rig.call(lambda: self.rig.store.db.execute("UPDATE sessions SET expires = ? WHERE rowid = (SELECT MIN(rowid) FROM sessions)", (int(time.time()) + 1,)))
        self.assertEqual(newer.call("POST", "/hub/logout").status, 200)
        self.assertEqual(len(self.kept()), 1)                                        # (the older login is left, and so is the picture)
        time.sleep(2.2)
        self.store("purge")
        self.assertEqual(self.rows(), [])

    def test_a_ban_made_by_the_command_line_takes_the_picture_at_once(self):
        self.login()
        self.settle()
        name = self.name()
        self.store("ban", ACCOUNT, "command line", "maple.leaf")
        self.assertEqual(self.get(name).status, 404)


class TheNextLoginIsTheRetry(PictureTest):
    def test_a_log_out_and_a_login_is_a_new_fetch(self):
        v = self.login()
        self.settle()
        self.assertEqual(v.call("POST", "/hub/logout").status, 200)
        self.login()
        self.assertEqual(self.settle(2), ["ok", "ok"])
        self.assertEqual(len(self.kept()), 1)

    def test_off_then_on_then_a_login_is_a_new_fetch(self):
        self.login()
        self.settle()
        self.store("set_picture_show", ACCOUNT, 0)
        self.store("set_picture_show", ACCOUNT, 1)
        self.login()
        self.assertEqual(self.settle(2), ["ok", "ok"])
        self.assertEqual(len(self.kept()), 1)

    def test_a_failed_fetch_and_a_login_is_a_new_fetch(self):
        self.fake.cdn_mode = "500"
        self.login()
        self.assertEqual(self.settle(), ["down"])
        self.fake.cdn_mode = "ok"
        self.login()
        self.assertEqual(self.settle(2), ["down", "ok"])
        self.assertEqual(len(self.kept()), 1)


class TheWorker(PictureTest):
    def test_a_bug_inside_the_worker_is_caught_logged_by_its_class_and_gives_its_place_back(self):
        def boom(*args, **kwargs):
            raise RuntimeError("canary-worker-text")
        self.rig.call(lambda: setattr(self.rig.store, "set_picture", boom))
        for _ in range(4):                                                           # (more than the two places that there are)
            self.login()
            self.settle(1, len(kinds_of(self.log())))
        text = self.log()
        self.assertEqual(kinds_of(text), ["bug"] * 4)
        self.assertEqual(text.count("picture kind=bug error=RuntimeError"), 4)
        self.assertNotIn("canary-worker-text", text)
        pictures = self.rig.hub.sessions.pictures
        self.assertEqual(self.rig.call(lambda: (pictures.inflight, len(pictures.queued), len(pictures.waiting), len(pictures.tasks))), (0, 0, 0, 0))
        self.rig.call(lambda: delattr(self.rig.store, "set_picture"))
        self.login()
        self.assertEqual(self.settle(1, 4)[-1], "ok")
        self.assertEqual(len(self.kept()), 1)

    def test_a_name_is_what_the_random_source_gave_and_nothing_made_of_the_account_the_code_the_time_or_the_key(self):
        asked = []

        class Source:
            @staticmethod
            def token_hex(n):
                asked.append(n)
                return "ab" * 16

        with mock.patch.object(picturesmod, "secrets", Source):
            self.login()
            self.settle()
        self.assertEqual((asked, [r[2] for r in self.kept()]), ([16], ["ab" * 16]))       # (one call of 16 bytes, and its answer is the name: not a digest of anything)


class TheDoor(PictureTest):
    def test_a_name_of_any_other_shape_and_any_other_way_to_ask_is_a_refusal(self):
        self.login()
        self.settle()
        name = self.name()
        cases = [("GET", "/hub/pic/%s.png" % name[:31]), ("GET", "/hub/pic/%s0.png" % name), ("GET", "/hub/pic/%s.png" % name.upper()), ("GET", "/hub/pic/%s.PNG" % name),
                 ("GET", "/hub/pic/%s" % name), ("GET", "/hub/pic/%s.png.png" % name), ("GET", "/hub/pic/%%%s.png" % name[:30]), ("GET", "/hub/pic/%s/.png" % name),
                 ("GET", "/hub/pic/../pic/%s.png" % name), ("GET", "/hub/pic/.png"), ("GET", "/hub/pic/"), ("GET", "/hub/pic"), ("GET", "/hub/pic/%s.png?x=1" % name), ("GET", "/hub/pic/%s.png?" % name),
                 ("GET", "/hub/pic/%s.png%%00" % name), ("GET", "/hub/pic/%s.png/" % name), ("GET", "/hub/pic/%s.jpg" % name), ("GET", "/hub/pic/%s.png" % name[:30] + "%2e")]
        for method, path in cases:
            with self.subTest(path=path):
                r = hc.request(self.rig.port, method, path, [("X-Client-Address", ADDRESS)])
                self.assertEqual(r.status, 404, (path, r.text()[:80]))
                self.assertEqual(r.json(), {"error": "not_found"})
        for method in ("POST", "PUT", "DELETE", "HEAD", "OPTIONS"):
            r = hc.request(self.rig.port, method, "/hub/pic/%s.png" % name, [("X-Client-Address", ADDRESS)])
            self.assertEqual(r.status, 405, method)
        self.assertEqual(self.get(name).status, 200)

    def test_the_door_gives_a_picture_to_anyone_who_has_the_name_and_sets_nothing_on_the_visitor(self):
        self.login()
        self.settle()
        name = self.name()
        r = hc.request(self.rig.port, "GET", "/hub/pic/%s.png" % name, [("X-Client-Address", ADDRESS), ("Cookie", "a=b"), ("Origin", "https://elsewhere.example")])
        self.assertEqual(r.status, 200)
        self.assertNotIn("set-cookie", dict(r.headers))
        self.assertNotIn("access-control-allow-origin", dict(r.headers))

    def test_an_address_may_ask_for_a_list_of_pictures_and_then_a_little_each_second(self):
        self.restart(picture_serve_burst=4, picture_serve_refill_seconds=5.0)
        statuses = [self.get("0" * 32, address="203.0.113.5").status for _ in range(7)]
        self.assertEqual(statuses, [404] * 4 + [429] * 3)
        self.assertEqual(self.get("0" * 32, address="203.0.113.6").status, 404)         # (another address has its own allowance)
        r = self.get("0" * 32, address="203.0.113.5")
        self.assertEqual((r.status, r.get("retry-after")), (429, "1"))

    def test_the_allowances_of_addresses_are_forgotten_by_the_clock_when_they_are_full_again(self):
        pictures = self.rig.hub.sessions.pictures
        self.get("0" * 32, address="203.0.113.7")
        self.assertEqual(len(self.rig.call(lambda: pictures.rate.buckets)), 1)
        self.rig.call(pictures.forget_idle, self.rig.hub.clock() + 3600)
        self.assertEqual(self.rig.call(lambda: pictures.rate.buckets), {})

    def test_a_hub_without_the_discord_login_has_no_door(self):
        with hub_rig.Rig(test_login=True) as rig:
            r = hc.request(rig.port, "GET", "/hub/pic/%s.png" % ("0" * 32), [("X-Client-Address", ADDRESS)])
            self.assertEqual((r.status, r.json()), (404, {"error": "not_found"}))


class TheStoreAndTheHub(PictureTest):
    def test_the_database_has_the_reference_and_never_the_id_and_the_schema_is_where_it_was(self):
        self.login()
        self.settle()
        db = sqlite3.connect(self.rig.db)
        self.addCleanup(db.close)
        self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0], 4)
        self.assertEqual([c[1] for c in db.execute("PRAGMA table_info(pictures)")], ["ref", "show", "name", "png", "until", "at"])
        for row in db.execute("SELECT ref, show, name, until, at FROM pictures"):
            self.assertNotIn(ACCOUNT, " ".join(str(x) for x in row))
        self.assertEqual(db.execute("SELECT ref FROM pictures").fetchone()[0], self.ref())
        blob = db.execute("SELECT png FROM pictures").fetchone()[0]
        self.assertNotIn(ACCOUNT.encode(), blob)
        self.assertNotIn(fd.AVATAR.encode(), blob)

    def test_the_names_are_read_again_when_another_process_changed_a_picture(self):
        self.login()
        self.settle()
        pictures = self.rig.hub.sessions.pictures
        first = self.rig.call(pictures.names)
        self.assertEqual(list(first.values()), [self.name()])
        other = sqlite3.connect(self.rig.db, isolation_level=None)                    # (the command line: a process of its own on the same file; the SQL here is its change)
        self.addCleanup(other.close)
        other.execute("DELETE FROM pictures")
        self.assertEqual(self.rig.call(pictures.names), first)                      # (nothing says that it changed: the list is the one it had)
        other.execute("INSERT OR IGNORE INTO meta (k, v) VALUES ('pic_seq', 0)")
        other.execute("UPDATE meta SET v = v + 1 WHERE k = 'pic_seq'")
        self.assertEqual(self.rig.call(pictures.names), {})

    def test_a_store_that_refuses_costs_the_login_nothing(self):
        self.rig.call(lambda: setattr(self.rig.store, "picture_state", lambda *a, **k: (_ for _ in ()).throw(RuntimeError("canary-db-text"))))
        v = self.login()
        self.assertIsNotNone(v.session())
        time.sleep(0.3)
        self.assertEqual(kinds_of(self.log()), ["bug"])
        self.assertIn("picture kind=bug error=RuntimeError", self.log())
        self.assertNotIn("canary-db-text", self.log())


class TheLog(PictureTest):
    def test_the_log_of_a_hub_that_fetched_pictures_has_no_id_code_name_or_reference(self):
        self.login()
        self.settle()
        name, ref = self.name(), self.ref()
        for mode in ("500", "huge", "404"):
            self.fake.cdn_mode = mode
            before = len(kinds_of(self.log()))
            self.login()
            self.settle(1, before)
        text = self.log()
        for canary in (ACCOUNT, fd.AVATAR, name, ref, "maple.leaf", "cdn.discordapp", "/avatars/", self.fake.url):
            self.assertNotIn(canary, text, "the log shows " + canary)
        self.assertEqual(kinds_of(text), ["ok", "down", "big", "none"])
        for line in text.splitlines():
            if " picture " in line:
                self.assertRegex(line, r"^\S+ picture kind=(ok|none|down|type|big|shape)$")


class TheProcess(unittest.TestCase):
    def test_the_real_process_fetches_a_picture_and_its_log_and_its_files_hold_no_id_code_name_or_reference(self):
        fake = fd.FakeDiscord(CALLBACK, user=(ACCOUNT, "maple.leaf"))
        fake.consent = False
        fake.avatar = fd.AVATAR
        folder = tempfile.mkdtemp(prefix="hub-pictures-proc-")
        self.addCleanup(lambda: __import__("shutil").rmtree(folder, ignore_errors=True))
        self.addCleanup(fake.close)
        env = {k: v for k, v in os.environ.items() if not k.startswith("ANTS_HUB_")}
        env.update({"ANTS_HUB_PORT": "0", "ANTS_HUB_HOST": "127.0.0.1", "ANTS_HUB_DB": os.path.join(folder, "hub.db"), "ANTS_HUB_PUBLIC_URL": hc.SITE, "PYTHONDONTWRITEBYTECODE": "1",
                    "ANTS_HUB_DISCORD_CLIENT_ID": fake.client_id, "ANTS_HUB_DISCORD_CLIENT_SECRET": fake.client_secret, "ANTS_HUB_TEST_DISCORD_URL": fake.url})
        log_path = os.path.join(folder, "hub.log")
        with open(log_path, "w") as log:
            proc = subprocess.Popen([sys.executable, "-m", "hub"], cwd=REPO, env=env, stdout=log, stderr=subprocess.STDOUT)
        self.addCleanup(lambda: proc.poll() is None and proc.kill())
        port = None
        for _ in range(100):
            with open(log_path) as f:
                m = re.search(r" listening port=(\d+)", f.read())
            if m:
                port = int(m.group(1))
                break
            time.sleep(0.1)
        self.assertIsNotNone(port)
        v = Visitor(port, fake, address="198.51.100.9")
        self.assertEqual(v.login().status, 302)
        db = sqlite3.connect(os.path.join(folder, "hub.db"))
        self.addCleanup(db.close)
        row = None
        for _ in range(100):
            row = db.execute("SELECT name FROM pictures WHERE png IS NOT NULL").fetchone()
            if row:
                break
            time.sleep(0.1)
        self.assertIsNotNone(row)
        name = row[0]
        r = hc.request(port, "GET", "/hub/pic/%s.png" % name, [("X-Client-Address", "198.51.100.9")])
        self.assertEqual((r.status, r.body), (200, picturesmod.rewrite(fake.picture)))
        ref = pseudo.reference(bytes(db.execute("SELECT v FROM secrets WHERE k = 'account'").fetchone()[0]), ACCOUNT)
        proc.send_signal(signal.SIGTERM)
        self.assertEqual(proc.wait(10), 0)
        with open(log_path) as f:
            text = f.read()
        for canary in (ACCOUNT, fd.AVATAR, name, ref, "maple.leaf", "avatars", "Traceback", folder):
            self.assertNotIn(canary, text, "the log shows " + str(canary))
        self.assertIn(" picture kind=ok", text)
        for line in text.splitlines():
            self.assertRegex(line, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ [a-z_]+( [a-z_]+=[A-Za-z0-9_.:/-]*)*$", line)


if __name__ == "__main__":
    unittest.main()
