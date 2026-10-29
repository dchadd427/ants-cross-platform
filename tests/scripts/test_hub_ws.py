#!/usr/bin/env python3
"""The hub's hand-rolled WebSocket (hub/ws.py) and its HTTP request reader (hub/http.py), fuzzed (docs/HUB.md "The handshake", "Frames").

The framing is the riskiest part of the hub (it reads bytes from strangers), so it is tested three ways: by the cases of RFC 6455 and of the hub's own rules (the 2 KB cap,
fragments refused), by an exhaustive sweep of every first byte with every length form against a model written separately in this file, and by random streams: pure noise,
valid frames cut at random places, valid frames with a bit flipped. What must hold for all of them: the parser raises nothing but ProtocolError, never keeps more than a header and
a frame's worth of bytes, gives the same answer however the bytes are cut, and stops for good after an error or a close frame. The client side of these tests is tests/scripts/hub_client.py,
which shares no code with hub/ws.py. Reproducible (fixed seeds); ANTS_HUB_FUZZ_SCALE=10 runs ten times more.
"""
import os
import random
import struct
import sys
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
from hub import http, ws  # noqa: E402
import hub_client as hc  # noqa: E402

SCALE = int(os.environ.get("ANTS_HUB_FUZZ_SCALE", "1"))
CAP = 2048


def feed_all(data, cap=CAP, chunk=None):
    """(frames, error code or None) of a whole stream, given in one piece or in chunks of `chunk` bytes."""
    parser = ws.Parser(cap)
    frames = []
    for i in range(0, len(data), chunk or max(1, len(data))):
        frames += parser.feed(data[i:i + (chunk or len(data))])
        assert len(parser.buffer) <= cap + 14, "the parser keeps %d bytes" % len(parser.buffer)
    return frames, parser.error.code if parser.error else None


class TheFrames(unittest.TestCase):
    def test_the_accept_key_is_the_one_of_the_rfc(self):
        self.assertEqual(ws.accept_key("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")

    def test_keys(self):
        self.assertTrue(ws.valid_key("dGhlIHNhbXBsZSBub25jZQ=="))
        for bad in ("", "short", "dGhlIHNhbXBsZSBub25jZQ", "dGhlIHNhbXBsZSBub25jZQ===", "dGhlIHNhbXBsZSBub25jZQ=!", "!" * 24, "A" * 24, "dGhlIHNhbXBsZSBub25jZQ=\n", "dGhlIHNhbXBsZSBub25jZQ= "):
            self.assertFalse(ws.valid_key(bad), repr(bad))

    def test_text_frames_of_every_length_up_to_the_cap_are_read(self):
        for n in (0, 1, 2, 3, 4, 5, 124, 125, 126, 127, 128, 255, 256, 1000, 2047, 2048):
            payload = bytes((i * 7 + 3) % 256 for i in range(n))
            text = "".join(chr(0x61 + b % 26) for b in payload)
            frames, error = feed_all(hc.frame(1, text.encode()))
            self.assertEqual((frames, error), ([(1, text)], None), n)

    def test_the_unmasking_is_the_xor_of_the_rfc(self):
        for n in range(0, 40):
            data = bytes(range(n))
            mask = bytes((0x12, 0x34, 0x56, 0x78))
            self.assertEqual(ws.unmask(data, mask), bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def test_a_frame_of_2049_bytes_is_refused_from_its_header_alone(self):
        header = struct.pack("!BBH", 0x81, 0x80 | 126, CAP + 1)
        parser = ws.Parser(CAP)
        self.assertEqual(parser.feed(header), [])                     # not one byte of payload has come
        self.assertEqual(parser.error.code, 1009)
        self.assertEqual(feed_all(hc.frame(1, b"a" * (CAP + 1)))[1], 1009)
        self.assertEqual(feed_all(hc.frame(1, b"a" * CAP))[1], None)
        # the huge lengths are refused unread too, and so is a length of the wrong form
        self.assertEqual(feed_all(struct.pack("!BBQ", 0x81, 0x80 | 127, 1 << 40))[1], 1009)
        self.assertEqual(feed_all(struct.pack("!BBQ", 0x81, 0x80 | 127, (1 << 63) | 70000))[1], 1002)

    def test_the_cap_is_a_parameter(self):
        self.assertEqual(feed_all(hc.frame(1, b"a" * 100), cap=100)[1], None)
        self.assertEqual(feed_all(hc.frame(1, b"a" * 101), cap=100)[1], 1009)

    def test_the_servers_own_frames(self):
        self.assertEqual(ws.encode_frame(1, b"hi"), b"\x81\x02hi")
        self.assertEqual(ws.encode_frame(9), b"\x89\x00")
        self.assertEqual(ws.encode_frame(1, b"a" * 125)[:2], b"\x81\x7d")
        self.assertEqual(ws.encode_frame(1, b"a" * 126)[:4], b"\x81\x7e\x00\x7e")
        self.assertEqual(ws.encode_frame(1, b"a" * 65535)[:4], b"\x81\x7e\xff\xff")
        self.assertEqual(ws.encode_frame(1, b"a" * 65536)[:10], b"\x81\x7f" + struct.pack("!Q", 65536))
        self.assertEqual(ws.encode_close(1001, "bye"), b"\x88\x05\x03\xe9bye")
        self.assertEqual(len(ws.encode_close(1000, "x" * 500)), 2 + 125)                # the reason is cut to fit a control frame
        # a client reads them: a masked-free frame is what hc.Client expects; the lengths round-trip through an independent reader
        for n in (0, 125, 126, 65535, 65536, 70000):
            frame = ws.encode_frame(1, b"x" * n)
            b1 = frame[1]
            length, offset = (b1, 2) if b1 < 126 else ((struct.unpack("!H", frame[2:4])[0], 4) if b1 == 126 else (struct.unpack("!Q", frame[2:10])[0], 10))
            self.assertEqual((length, len(frame) - offset), (n, n))


class TheRefusals(unittest.TestCase):
    """Each broken thing gets the close code of the RFC."""

    def code(self, data):
        return feed_all(data)[1]

    def test_an_unmasked_client_frame_is_1002(self):
        self.assertEqual(self.code(hc.frame(1, b"{}", masked=False)), 1002)
        self.assertEqual(self.code(hc.frame(9, b"", masked=False)), 1002)

    def test_reserved_bits_are_1002(self):
        for rsv in (1, 2, 4, 7):
            self.assertEqual(self.code(hc.frame(1, b"{}", rsv=rsv)), 1002, rsv)

    def test_unknown_opcodes_are_1002(self):
        for opcode in (3, 4, 5, 6, 7, 0xB, 0xC, 0xD, 0xE, 0xF):
            self.assertEqual(self.code(hc.frame(opcode, b"")), 1002, opcode)

    def test_binary_and_every_fragment_are_1003(self):
        self.assertEqual(self.code(hc.frame(2, b"\x00\x01")), 1003)
        self.assertEqual(self.code(hc.frame(1, b"{}", fin=False)), 1003)              # the first piece of a message
        self.assertEqual(self.code(hc.frame(0, b"{}")), 1003)                         # a continuation, last piece
        self.assertEqual(self.code(hc.frame(0, b"{}", fin=False)), 1003)              # a continuation, middle piece
        self.assertEqual(self.code(hc.frame(1, b"{", fin=False) + hc.frame(0, b"}")), 1003)

    def test_control_frames_are_short_and_whole(self):
        for opcode in (8, 9, 10):
            self.assertEqual(self.code(hc.frame(opcode, b"x" * 126)), 1002, opcode)
            self.assertEqual(self.code(hc.frame(opcode, b"x" * 125 if opcode != 8 else b"\x03\xe8" + b"x" * 123)), None, opcode)
            self.assertEqual(self.code(hc.frame(opcode, b"", fin=False)), 1002, opcode)

    def test_a_length_written_the_long_way_is_1002(self):
        self.assertEqual(self.code(hc.frame(1, b"{}", length_form=16)), 1002)
        self.assertEqual(self.code(hc.frame(1, b"{}", length_form=64)), 1002)
        self.assertEqual(self.code(hc.frame(1, b"a" * 125, length_form=16)), 1002)
        self.assertEqual(self.code(hc.frame(1, b"a" * 126, length_form=16)), None)    # (126 is the first length that needs 16 bits)
        self.assertEqual(self.code(hc.frame(1, b"a" * 2000, length_form=64)), 1002)
        self.assertEqual(self.code(hc.frame(10, b"", length_form=16)), 1002)

    def test_text_that_is_not_utf8_is_1007(self):
        for bad in (b"\xff", b"\xc0\xaf", b"\xed\xa0\x80", b"\xe2\x82", b"\xf4\x90\x80\x80", b"ok\x80", b"\xc3"):
            self.assertEqual(self.code(hc.frame(1, bad)), 1007, bad)
        self.assertEqual(self.code(hc.frame(1, "\u20ac\U0001f600".encode())), None)

    def test_close_frames(self):
        for payload, expected in ((b"", (1000, "")), (b"\x03\xe8", (1000, "")), (b"\x03\xe9bye", (1001, "bye")), (b"\x03\xef", (1007, "")), (b"\x03\xf6", (1014, "")),
                                  (b"\x0b\xb8", (3000, "")), (b"\x13\x87", (4999, "")), (b"\x03\xe8" + "\u20ac".encode(), (1000, "\u20ac"))):
            self.assertEqual(feed_all(hc.frame(8, payload)), ([(8, expected)], None), payload)
        for payload in (b"\x03", b"\x00\x00", b"\x03\xe7", b"\x03\xec", b"\x03\xed", b"\x03\xee", b"\x03\xf7", b"\x0b\xb7", b"\x13\x88", b"\xff\xff"):
            self.assertEqual(feed_all(hc.frame(8, payload)), ([], 1002), payload)
        self.assertEqual(feed_all(hc.frame(8, b"\x03\xe8\xff")), ([], 1007))
        self.assertEqual(feed_all(hc.frame(8, b"\x03\xe8\xed\xa0\x80")), ([], 1007))

    def test_nothing_is_read_after_an_error_or_a_close(self):
        parser = ws.Parser(CAP)
        self.assertEqual(parser.feed(hc.frame(8, b"\x03\xe8") + hc.frame(1, b"{}")), [(8, (1000, ""))])      # the frame after a close is dropped
        self.assertEqual(parser.feed(hc.frame(1, b"{}")), [])
        self.assertIsNone(parser.error)
        parser = ws.Parser(CAP)
        self.assertEqual(parser.feed(hc.frame(1, b"{}") + hc.frame(2, b"") + hc.frame(1, b"{}")), [(1, "{}")])      # the good frame before the break is kept
        self.assertEqual(parser.error.code, 1003)
        self.assertEqual(parser.feed(hc.frame(1, b"{}")), [])
        self.assertEqual(len(parser.buffer), 0)

    def test_several_frames_in_one_read_and_a_frame_in_pieces(self):
        stream = hc.frame(1, b'{"t":"ping"}') + hc.frame(9, b"abc") + hc.frame(1, b'{"t":"say"}') + hc.frame(10, b"")
        expected = [(1, '{"t":"ping"}'), (9, b"abc"), (1, '{"t":"say"}'), (10, b"")]
        self.assertEqual(feed_all(stream), (expected, None))
        for chunk in (1, 2, 3, 5, 7, 64):
            self.assertEqual(feed_all(stream, chunk=chunk), (expected, None), chunk)


def model(b0, masked, kind, length, payload=b"", cap=CAP):
    """The close codes that RFC 6455 and the hub's rules allow for a frame (an empty set: the frame is fine), written down separately from the parser: every rule that the frame
    breaks adds its code, and the parser has to answer with one of them."""
    opcode, fin, rsv = b0 & 0x0F, bool(b0 & 0x80), b0 & 0x70
    codes = set()
    if rsv:
        codes.add(1002)
    if not masked:
        codes.add(1002)
    if opcode in (3, 4, 5, 6, 7) or opcode > 10:
        codes.add(1002)
    if opcode in (0, 2):
        codes.add(1003)
    control = 8 <= opcode <= 10
    if not fin:
        codes.add(1002 if control else 1003)
    if (kind == 16 and length < 126) or (kind == 64 and length < 65536) or (kind == 64 and length >= 1 << 63):
        codes.add(1002)
    if control and length > 125:
        codes.add(1002)
    if length > cap:
        codes.add(1009)
    if opcode == 8 and length == 1:
        codes.add(1002)
    if opcode == 8 and 2 <= length <= 125 and payload[:2] == b"\x00\x00":
        codes.add(1002)
    return codes


class TheSweep(unittest.TestCase):
    """Every first byte, with the mask bit on and off and each way of writing a length, against the model above."""

    LENGTHS = {0: [0, 1, 5, 125], 16: [0, 1, 100, 125, 126, 127, 200, 2047, 2048, 2049, 65535], 64: [0, 125, 126, 2048, 2049, 65535, 65536, 70000, (1 << 63) - 1, 1 << 63, (1 << 64) - 1]}

    def stream(self, b0, masked, kind, length):
        mask_bit = 0x80 if masked else 0
        payload = bytes(min(length, 3000))
        if kind == 0:
            head = bytes((b0, mask_bit | length))
        elif kind == 16:
            head = struct.pack("!BBH", b0, mask_bit | 126, length)
        else:
            head = struct.pack("!BBQ", b0, mask_bit | 127, length)
        return head + (b"\x00\x00\x00\x00" if masked else b"") + payload

    def test_the_sweep(self):
        runs = 0
        for b0 in range(256):
            for masked in (True, False):
                for kind, lengths in self.LENGTHS.items():
                    for length in lengths:
                        data = self.stream(b0, masked, kind, length)
                        frames, error = feed_all(data)
                        allowed = model(b0, masked, kind, length, bytes(min(length, 3000)))
                        label = "b0=%02x masked=%s kind=%d length=%d" % (b0, masked, kind, length)
                        if allowed:
                            self.assertIn(error, allowed, label)
                        else:
                            self.assertEqual((error, len(frames)), (None, 1), label)
                            self.assertEqual(frames[0][0], b0 & 0x0F, label)
                        runs += 1
        self.assertGreater(runs, 8000)


def random_stream(rnd):
    kind = rnd.random()
    if kind < 0.25:
        return bytes(rnd.getrandbits(8) for _ in range(rnd.randint(0, 3000)))
    parts = []
    for _ in range(rnd.randint(1, 6)):
        opcode = rnd.choice([1, 1, 1, 1, 8, 9, 10, 0, 2, 3, 15])
        n = rnd.choice([0, 1, 2, 10, 125, 126, 200, 2048, 2049, 3000]) if rnd.random() < 0.7 else rnd.randint(0, 2100)
        payload = bytes(rnd.getrandbits(8) for _ in range(n)) if rnd.random() < 0.5 else bytes(0x61 + rnd.getrandbits(4) for _ in range(n))
        parts.append(hc.frame(opcode, payload, fin=rnd.random() < 0.9, rsv=0 if rnd.random() < 0.95 else rnd.randint(1, 7), masked=rnd.random() < 0.95,
                              length_form=rnd.choice([None, None, None, 16, 64])))
    data = b"".join(parts)
    if kind < 0.6:
        return data
    if kind < 0.8 and data:
        return data[:rnd.randint(0, len(data))]
    data = bytearray(data)
    for _ in range(rnd.randint(1, 4)):
        if data:
            data[rnd.randrange(len(data))] ^= 1 << rnd.randrange(8)
    return bytes(data)


class TheFuzz(unittest.TestCase):
    CASES = 6000 * SCALE

    def test_random_streams_never_break_the_parser_and_every_cut_gives_the_same_answer(self):
        rnd = random.Random(1998)
        errors = frames_seen = 0
        for _ in range(self.CASES):
            data = random_stream(rnd)
            whole = feed_all(data)
            for chunk in (1, rnd.randint(2, 40), rnd.randint(41, 700)):
                self.assertEqual(feed_all(data, chunk=chunk), whole, "%d bytes cut in %d" % (len(data), chunk))
            frames, error = whole
            errors += error is not None
            frames_seen += len(frames)
            if error is None:
                for opcode, payload in frames:
                    self.assertIn(opcode, (1, 8, 9, 10))
                    self.assertLessEqual(len(payload), CAP)
        self.assertGreater(errors, self.CASES // 10)
        self.assertGreater(frames_seen, self.CASES // 10)

    def test_valid_messages_cut_anywhere_come_out_whole(self):
        rnd = random.Random(2026)
        for _ in range(self.CASES // 2):
            texts = ["".join(chr(rnd.choice([0x41, 0x7a, 0xe9, 0x4f60, 0x1f600, 0x20])) for _ in range(rnd.randint(0, 400))) for _ in range(rnd.randint(1, 5))]
            texts = [t for t in texts if len(t.encode()) <= CAP]
            stream = b"".join(hc.frame(1, t.encode()) for t in texts)
            parser = ws.Parser(CAP)
            got = []
            i = 0
            while i < len(stream):
                j = i + rnd.randint(1, 120)
                got += [p for _, p in parser.feed(stream[i:j])]
                i = j
            self.assertEqual(got, texts)

    def test_noise_after_a_good_frame_does_not_take_the_good_frame_back(self):
        rnd = random.Random(5)
        for _ in range(500 * SCALE):
            noise = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(1, 50)))
            parser = ws.Parser(CAP)
            try:
                frames = parser.feed(hc.frame(1, b'{"t":"ping"}') + noise)
            except ws.ProtocolError:
                continue
            self.assertEqual(frames[0], (1, '{"t":"ping"}'))


GOOD_HEAD = (b"GET /hub/ws HTTP/1.1\r\nHost: chat.example.org\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             b"Sec-WebSocket-Version: 13\r\nOrigin: https://chat.example.org\r\nX-Client-Address: 203.0.113.9\r\n\r\n")


class TheRequestReader(unittest.TestCase):
    def test_a_good_head(self):
        request = http.parse_head(GOOD_HEAD)
        self.assertEqual((request.method, request.path), ("GET", "/hub/ws"))
        self.assertEqual(request.headers["origin"], "https://chat.example.org")
        self.assertEqual(request.headers["x-client-address"], "203.0.113.9")
        self.assertTrue(request.has_token("connection", "upgrade"))

    def test_connection_lists_and_case(self):
        head = GOOD_HEAD.replace(b"Connection: Upgrade", b"CONNECTION: keep-alive,  UPGRADE")
        self.assertTrue(http.parse_head(head).has_token("connection", "upgrade"))
        self.assertFalse(http.parse_head(GOOD_HEAD.replace(b"Connection: Upgrade", b"Connection: keep-alive")).has_token("connection", "upgrade"))

    def refused(self, head, status=400):
        with self.assertRaises(http.HttpError) as caught:
            http.parse_head(head)
        self.assertEqual(caught.exception.status, status, head)
        return caught.exception.code

    def test_the_head_is_strict(self):
        self.refused(GOOD_HEAD.replace(b"\r\n", b"\n"))                                         # a bare line feed
        self.refused(GOOD_HEAD.replace(b"Upgrade: websocket\r\n", b"Upgrade: websocket\rX: y\r\n"))     # a bare carriage return
        self.refused(GOOD_HEAD.replace(b"Upgrade: websocket\r\n", b"Upgrade:\r\n  websocket\r\n"))      # a folded line
        self.refused(GOOD_HEAD.replace(b"Upgrade: websocket\r\n", b" Upgrade: websocket\r\n"))
        self.refused(GOOD_HEAD.replace(b"Upgrade:", b"Upgrade :"))                               # a blank before the colon
        self.refused(GOOD_HEAD.replace(b"Upgrade: websocket", b"Upgrade: web\x00socket"))
        self.refused(GOOD_HEAD.replace(b"Upgrade: websocket", b"Upgrade: web\x7fsocket"))
        self.refused(GOOD_HEAD.replace(b"GET /hub/ws HTTP/1.1", b"GET /hub/ws HTTP/2.0"))
        self.refused(GOOD_HEAD.replace(b"GET /hub/ws HTTP/1.1", b"GET hub/ws HTTP/1.1"))
        self.refused(GOOD_HEAD.replace(b"GET /hub/ws HTTP/1.1", b"get /hub/ws HTTP/1.1"))
        self.refused(GOOD_HEAD.replace(b"GET /hub/ws HTTP/1.1", b"GET  /hub/ws HTTP/1.1"))
        self.refused(GOOD_HEAD.replace(b"GET /hub/ws HTTP/1.1", b"GET /hub/ws HTTP/1.1 x"))
        self.refused(GOOD_HEAD.replace(b"GET /hub/ws HTTP/1.1", b"GET /hub/ws\x00 HTTP/1.1"))
        self.refused(b"\r\n\r\n")
        self.refused(b"GET /hub/ws HTTP/1.1\r\nnocolon\r\n\r\n")
        self.refused(b"GET /hub/ws HTTP/1.1\r\n\r\n"[:-2])

    def test_a_bare_line_feed_after_a_line_is_refused(self):
        self.refused(b"GET /hub/ws HTTP/1.1\n\r\nHost: x\r\n\r\n")                           # (the end of a text, which `$` would accept before a last line feed, is not a line end)
        self.refused(b"GET /hub/ws HTTP/1.1\r\nHost: x\n\r\n\r\n")
        self.refused(b"GET /hub/ws HTTP/1.1\r\nHost: x\r\nX-A: b\n\r\n\r\n")

    def test_the_blanks_around_a_value_are_not_part_of_it(self):
        head = http.parse_head(b"GET /hub/ws HTTP/1.1\r\nX-A:   v  w \t \r\nX-B:\r\nX-C:\t\tz\r\n\r\n")
        self.assertEqual((head.headers["x-a"], head.headers["x-b"], head.headers["x-c"]), ("v  w", "", "z"))

    def test_only_spaces_and_tabs_are_cut_from_the_ends_of_a_value(self):
        # a no-break space and the next-line character are not blanks of HTTP: they stay where the client put them (the old pattern cut a space and a tab and nothing else)
        head = http.parse_head("GET /hub/ws HTTP/1.1\r\nX-A: \xa0v\xa0\r\nX-B:\x85w\x85 \t\r\nX-C:  \xa0 \r\n\r\n".encode("latin-1"))
        self.assertEqual((head.headers["x-a"], head.headers["x-b"], head.headers["x-c"]), ("\xa0v\xa0", "\x85w\x85", "\xa0"))

    def test_a_long_run_of_blanks_costs_nothing(self):
        # a header line is read in one pass: a pattern that cut the blanks around the value itself took a quarter of a second on one like this, on the one thread that serves every socket
        for tail in ("b", "\x01", "b\x01", "\t", " "):
            head = b"GET /hub/ws HTTP/1.1\r\nCookie: a" + b" " * 7600 + tail.encode() + b"\r\nHost: x\r\n\r\n"
            best = 1.0
            for _ in range(3):
                started = time.perf_counter()
                try:
                    http.parse_head(head)
                except http.HttpError:
                    pass
                best = min(best, time.perf_counter() - started)
            self.assertLess(best, 0.05, "a head with %r after 7600 blanks took %.3f s" % (tail, best))
        # the same for every header name that is believed, and for a request line with a long path
        for name in ("Origin", "Sec-Fetch-Site", "Cookie", "X-Client-Address"):
            head = ("GET /hub/ws HTTP/1.1\r\n%s: a%s\x01\r\n\r\n" % (name, " " * 7500)).encode("latin-1")
            started = time.perf_counter()
            with self.assertRaises(http.HttpError):
                http.parse_head(head)
            self.assertLess(time.perf_counter() - started, 0.05, name)
        started = time.perf_counter()
        http.parse_head(b"GET /" + b"a" * 1990 + b" HTTP/1.1\r\n\r\n")
        self.assertLess(time.perf_counter() - started, 0.05)

    def test_a_header_that_is_believed_may_not_be_repeated(self):
        for name in ("Origin: https://chat.example.org", "Upgrade: websocket", "Connection: Upgrade", "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", "Sec-WebSocket-Version: 13",
                     "X-Client-Address: 203.0.113.9", "Content-Length: 0", "Transfer-Encoding: chunked", "Host: x", "Cookie: a=b", "X-Hub-Test-Login: 1:ab"):
            head = GOOD_HEAD.replace(b"\r\n\r\n", ("\r\n" + name + "\r\n" + name + "\r\n\r\n").encode())
            self.assertIn("repeated_", self.refused(head), name)
        # the same name in another case is the same header
        self.assertIn("repeated_origin", self.refused(GOOD_HEAD.replace(b"\r\n\r\n", b"\r\nORIGIN: https://evil.example\r\n\r\n")))
        # a header that nothing here believes may be repeated
        head = http.parse_head(GOOD_HEAD.replace(b"\r\n\r\n", b"\r\nAccept: a\r\nAccept: b\r\n\r\n"))
        self.assertEqual(head.headers["accept"], "a, b")

    def test_too_many_headers(self):
        many = b"".join(b"X-%d: y\r\n" % i for i in range(http.MAX_HEADERS + 1))
        self.refused(b"GET /hub/ws HTTP/1.1\r\n" + many + b"\r\n", 431)
        enough = b"".join(b"X-%d: y\r\n" % i for i in range(http.MAX_HEADERS))
        self.assertEqual(len(http.parse_head(b"GET /hub/ws HTTP/1.1\r\n" + enough + b"\r\n").headers), http.MAX_HEADERS)

    def test_the_answer_bytes(self):
        out = http.response(403, {"error": "origin"})
        self.assertTrue(out.startswith(b"HTTP/1.1 403 Forbidden\r\n"))
        self.assertIn(b"Content-Length: 18\r\n", out)
        self.assertIn(b"Connection: close\r\n", out)
        self.assertTrue(out.endswith(b'\r\n\r\n{"error":"origin"}'))
        switching = http.response(101, headers={"Upgrade": "websocket"})
        self.assertNotIn(b"Content-Length", switching)
        self.assertTrue(switching.endswith(b"\r\n\r\n"))

    def test_the_fuzz(self):
        rnd = random.Random(13)
        refused = parsed = 0
        for _ in range(3000 * SCALE):
            head = bytearray(GOOD_HEAD)
            for _ in range(rnd.randint(1, 6)):
                where = rnd.randrange(len(head))
                action = rnd.random()
                if action < 0.4:
                    head[where] ^= 1 << rnd.randrange(8)
                elif action < 0.6:
                    del head[where]
                elif action < 0.8:
                    head.insert(where, rnd.choice(b"\r\n :\t\x00\xff"))
                else:
                    head[where:where] = head[rnd.randrange(len(head)):][:rnd.randint(1, 30)]
            try:
                http.parse_head(bytes(head))
                parsed += 1
            except http.HttpError:
                refused += 1
        self.assertGreater(refused, 500)
        self.assertGreater(parsed, 50)
        for _ in range(500 * SCALE):
            noise = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(0, 300)))
            with self.assertRaises(http.HttpError):
                http.parse_head(noise)


if __name__ == "__main__":
    unittest.main()
