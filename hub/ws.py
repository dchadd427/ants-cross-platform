"""WebSocket framing (RFC 6455), by hand: the server side of text frames and nothing else. The tests fuzz it (tests/scripts/test_hub_ws.py).

What a client may send: a masked text frame of at most `max_payload` bytes in one piece, and the control frames ping, pong and close. Everything else is refused with the
close code the RFC gives it: 1002 for a broken frame (no mask, a reserved bit, an unknown opcode, a control frame that is long or split, a length written in more bytes than it
needs), 1003 for a kind of data this service does not take (binary frames, and any fragment: a text frame without FIN, or a continuation), 1007 for text that is not UTF-8, 1009
for a frame over the cap (refused from its header, before its payload is read). Nothing here reads a socket: Parser.feed(bytes) returns the frames that the bytes completed.
"""
import base64
import binascii
import hashlib
import struct

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
CONT, TEXT, BINARY, CLOSE, PING, PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA
CONTROL_MAX = 125
# the close codes that a peer may send (RFC 6455, 7.4): 1004 - 1006 and 1015 are for local use only
VALID_CLOSE = frozenset(range(1000, 1004)) | frozenset(range(1007, 1015)) | frozenset(range(3000, 5000))


class ProtocolError(Exception):
    """The peer broke the protocol: answer with a close frame of `code` and drop the socket."""

    def __init__(self, code, reason):
        super().__init__(reason)
        self.code = code
        self.reason = reason


def accept_key(key):
    return base64.b64encode(hashlib.sha1((key + GUID).encode("ascii")).digest()).decode("ascii")


def valid_key(key):
    """A Sec-WebSocket-Key: 16 random bytes in base64, 24 characters."""
    if len(key) != 24:
        return False
    try:
        return len(base64.b64decode(key, validate=True)) == 16
    except (binascii.Error, ValueError):
        return False


def encode_frame(opcode, payload=b""):
    """A server frame: FIN set, never masked."""
    n = len(payload)
    if n < 126:
        head = struct.pack("!BB", 0x80 | opcode, n)
    elif n < 65536:
        head = struct.pack("!BBH", 0x80 | opcode, 126, n)
    else:
        head = struct.pack("!BBQ", 0x80 | opcode, 127, n)
    return head + payload


def encode_close(code=1000, reason=""):
    return encode_frame(CLOSE, struct.pack("!H", code) + reason.encode("utf-8")[:CONTROL_MAX - 2])


def unmask(data, mask):
    n = len(data)
    if not n:
        return b""
    key = (mask * (n // 4 + 1))[:n]
    return (int.from_bytes(data, "big") ^ int.from_bytes(key, "big")).to_bytes(n, "big")


def parse_close(payload):
    """(code, reason) of a close frame's payload; an empty one is a normal close."""
    if not payload:
        return 1000, ""
    if len(payload) == 1:
        raise ProtocolError(1002, "a close frame of one byte")
    code = struct.unpack("!H", payload[:2])[0]
    if code not in VALID_CLOSE:
        raise ProtocolError(1002, "a close code that is not for the wire")
    try:
        return code, payload[2:].decode("utf-8")
    except UnicodeDecodeError:
        raise ProtocolError(1007, "the close reason is not UTF-8")


class Parser:
    """Frames from the bytes of one connection, as they arrive. feed() never raises: when the stream breaks, `error` is set (a ProtocolError) and the frames that came whole before the
    break are still returned; after an error, and after a close frame, nothing more is read."""

    def __init__(self, max_payload):
        self.max_payload = max_payload
        self.buffer = bytearray()
        self.finished = False
        self.error = None

    def feed(self, data):
        if self.finished:
            return []
        self.buffer += data
        frames = []
        while True:
            try:
                frame = self.next_frame()
            except ProtocolError as e:
                self.error = e
                self.finished = True
                break
            if frame is None:
                break
            frames.append(frame)
            if frame[0] == CLOSE:
                self.finished = True
                break
        if self.finished:
            self.buffer.clear()
        return frames

    def next_frame(self):
        """(opcode, payload) of the next whole frame in the buffer, or None when it is not all here yet. The payload of a text frame is its str, that of a close frame is (code, reason),
        that of a ping or pong is bytes."""
        buf = self.buffer
        if len(buf) < 2:
            return None
        b0, b1 = buf[0], buf[1]
        if b0 & 0x70:
            raise ProtocolError(1002, "a reserved bit is set")
        fin, opcode = bool(b0 & 0x80), b0 & 0x0F
        if not b1 & 0x80:
            raise ProtocolError(1002, "a client frame is not masked")
        if opcode == CONT:
            raise ProtocolError(1003, "fragments are not taken")
        if opcode == BINARY:
            raise ProtocolError(1003, "binary frames are not taken")
        if opcode not in (TEXT, CLOSE, PING, PONG):
            raise ProtocolError(1002, "an unknown opcode")
        control = opcode >= CLOSE
        if not fin:
            if control:
                raise ProtocolError(1002, "a control frame is split")
            raise ProtocolError(1003, "fragments are not taken")
        length, offset = b1 & 0x7F, 2
        if length == 126:
            if len(buf) < 4:
                return None
            length, offset = struct.unpack("!H", bytes(buf[2:4]))[0], 4
            if length < 126:
                raise ProtocolError(1002, "a length that is not the shortest")
        elif length == 127:
            if len(buf) < 10:
                return None
            length, offset = struct.unpack("!Q", bytes(buf[2:10]))[0], 10
            if length < 65536 or length >> 63:
                raise ProtocolError(1002, "a length that is not the shortest")
        if control and length > CONTROL_MAX:
            raise ProtocolError(1002, "a control frame is long")
        if length > self.max_payload:
            raise ProtocolError(1009, "the frame is over %d bytes" % self.max_payload)
        end = offset + 4 + length
        if len(buf) < end:
            return None
        mask = bytes(buf[offset:offset + 4])
        payload = unmask(bytes(buf[offset + 4:end]), mask)
        del buf[:end]
        if opcode == TEXT:
            try:
                return opcode, payload.decode("utf-8")
            except UnicodeDecodeError:
                raise ProtocolError(1007, "the text is not UTF-8")
        if opcode == CLOSE:
            return opcode, parse_close(payload)
        return opcode, payload
