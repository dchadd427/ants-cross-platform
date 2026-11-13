#!/usr/bin/env python3
"""The reading helpers of the hub's test client (tests/scripts/hub_client.py), on a scripted socket: what recv_reply skips and what it returns, what wait_line waits for and when it fails.

About fifty sites of the hub's tests rest on these two functions (an error answer that must not be the online count, a chat line that may be late), so they are pinned here with no hub: a frame
that is the count is skipped, any other frame is the answer (a wrong one too, the test then fails on it), a line is waited for through every other frame, and a socket that closes or stays
silent is a failure that names what was seen.
"""
import json
import os
import socket
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hub_client as hc  # noqa: E402


class ScriptedSocket:
    """recv() hands out the scripted bytes, then what `ending` says: 'silent' (a read that times out) or 'closed' (an end of stream)."""

    def __init__(self, frames, ending="silent", late=None, delay=0.0):
        self.data = b"".join(frames)
        self.ending = ending
        self.late = late                       # (bytes that come `delay` seconds after the others have been read, if the reader is still waiting then)
        self.delay = delay

    def settimeout(self, seconds):
        self.seconds = seconds

    def recv(self, n):
        if self.data:
            chunk, self.data = self.data[:n], self.data[n:]
            return chunk
        if self.ending == "counting":                                           # (a count in every read, and the answer only after a second: a reader with no deadline gets it)
            if time.monotonic() > self.until:
                self.data = ERR
            else:
                self.data = COUNT
                time.sleep(0.001)
            return self.recv(n)
        if self.late is not None and self.seconds >= self.delay:
            time.sleep(self.delay)
            self.data, self.late = self.late, None
            return self.recv(n)
        if self.ending == "closed":
            return b""
        time.sleep(min(self.seconds, 0.05))
        raise socket.timeout("scripted silence")

    def sendall(self, data):
        pass


def text(**fields):
    return hc.frame(1, json.dumps(fields).encode(), masked=False)


def client(frames, ending="silent"):
    c = hc.Client.__new__(hc.Client)
    c.sock = ScriptedSocket(frames, ending)
    c.timeout = 0.2
    c.buffer = b""
    return c


COUNT = text(t="online", n=2)
ERR = text(t="err", code="young")
LINE = text(t="line", text="hello", who="maple")


class TheReplyReader(unittest.TestCase):
    def test_the_counts_are_skipped_and_the_first_other_frame_is_the_answer(self):
        c = client([COUNT, COUNT, ERR, COUNT])
        self.assertEqual(c.recv_reply(), {"t": "err", "code": "young"})
        self.assertEqual(c.recv_json(), {"t": "online", "n": 2})                 # (what came after the answer is still there)

    def test_a_line_a_removal_and_a_who_are_answers_and_not_skipped(self):
        for frame, want in ((LINE, "line"), (text(t="del", id=3), "del"), (text(t="who", list=[]), "who"), (text(t="you", you=None), "you")):
            self.assertEqual(client([COUNT, frame]).recv_reply()["t"], want)

    def test_a_frame_that_only_looks_like_the_count_is_an_answer(self):
        for frame in (text(t="online", n=2, extra=1), text(t="online"), text(t="online", n=2, code="young")):
            self.assertEqual(client([frame]).recv_reply()["t"], "online")

    def test_no_answer_is_a_failure_that_names_the_counts_skipped(self):
        with self.assertRaises(AssertionError) as caught:
            client([COUNT, COUNT]).recv_reply()
        self.assertIn("'n': 2", str(caught.exception))

    def test_a_hub_that_never_stops_counting_cannot_hold_the_test(self):
        c = client([], "counting")
        c.sock.until = time.monotonic() + 1.0
        started = time.monotonic()
        with self.assertRaises(AssertionError):
            c.recv_reply(total=0.3)
        self.assertLess(time.monotonic() - started, 0.9)

    def test_a_closed_socket_is_an_end_of_stream(self):
        with self.assertRaises(EOFError):
            client([COUNT], "closed").recv_reply()


class TheLineWaiter(unittest.TestCase):
    def test_it_returns_the_first_line_through_every_other_frame(self):
        c = client([COUNT, ERR, text(t="del", id=1), LINE, text(t="line", text="second", who="maple")])
        self.assertEqual(c.wait_line(seconds=2.0, settle=0.05)["text"], "hello")

    def test_it_returns_only_a_line(self):
        for frame in (ERR, COUNT, text(t="del", id=1), text(t="who", list=[])):
            with self.assertRaises(AssertionError):
                client([frame]).wait_line(seconds=0.3, settle=0.05)

    def test_a_line_that_comes_late_is_waited_for(self):
        c = client([COUNT])
        c.sock.late, c.sock.delay = LINE, 0.6                                    # (longer than the read of the test client and than a drain's silence)
        self.assertEqual(c.wait_line(seconds=5.0, settle=0.05)["text"], "hello")

    def test_it_fails_at_the_timeout_and_says_what_it_saw(self):
        with self.assertRaises(AssertionError) as caught:
            client([ERR]).wait_line(seconds=0.4, settle=0.05)
        self.assertIn("no chat line", str(caught.exception))
        self.assertIn("'code': 'young'", str(caught.exception))

    def test_it_fails_when_the_socket_closes(self):
        with self.assertRaises(AssertionError) as caught:
            client([COUNT], "closed").wait_line(seconds=2.0, settle=0.05)
        self.assertIn("closed", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
