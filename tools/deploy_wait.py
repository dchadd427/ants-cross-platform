#!/usr/bin/env python3
"""Waits until the game server is idle, so that the deploy job does not end the matches that people are playing (a deploy recreates the server's container).

usage: deploy_wait.py [--url URL] [--label NAME] [--max-wait-minutes M] [--grace-minutes G] [--interval SECONDS]

URL is the site's public busy answer (GET https://<site>/busy: {"matches": N, "players": M}, plain counts; docs/WORKFLOW.md, "ants_server"). It is polled every INTERVAL seconds
(60 by default) and the deploy goes on, which here means the script ends with status 0, when

  - matches is 0: the server is idle (people may wait in rooms that have not started: they are not a match), or
  - M minutes have passed (180 by default, 300 at the most: the job's time limit is above that) whatever runs: "deploying anyway", said in the log, or
  - the address has not answered properly for G minutes (5 by default): the site is down anyway, or it is a build that has no /busy yet. Anything but a well-formed answer counts
    as "does not answer": a refused connection, a timeout, an HTTP error, a page that is not the two counts. None of them is ever taken for an idle server.

Without a URL it ends at once. The exit status is 0 in every case that ends the wait, 2 for a wrong argument and 130 when the wait is cancelled (a newer push cancels the job of an
older one, which writes that nothing was deployed). One line is written for every poll. The address is never printed (it is the site's own, public, but the staging site's need not be).
A longest wait that is no number of minutes is replaced by the default, with a line that says so (a mistyped repository variable must not stop a deploy).
"""
import argparse
import json
import math
import signal
import socket
import ssl
import sys
import time
import urllib.error
import urllib.request

DEFAULT_MAX_WAIT_MINUTES = 180.0
MAX_WAIT_CAP_MINUTES = 300.0
DEFAULT_GRACE_MINUTES = 5.0
DEFAULT_INTERVAL_SECONDS = 60.0
MAX_BODY_BYTES = 2048


def describe(reason):
    """Why a connection failed, in a few words and without the address (a reason's text may hold it)."""
    if isinstance(reason, (socket.timeout, TimeoutError)):
        return "timed out"
    if isinstance(reason, ConnectionRefusedError):
        return "connection refused"
    if isinstance(reason, socket.gaierror):
        return "name not found"
    if isinstance(reason, ssl.SSLError):
        return "TLS error"
    return type(reason).__name__


def read_counts(url, timeout):
    """((matches, players), None) from the busy answer, or (None, why) when the address does not answer properly."""
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "ants-deploy-wait", "Accept": "application/json"})
        with urllib.request.urlopen(request, timeout=timeout) as answer:
            if answer.status != 200:
                return None, "HTTP %d" % answer.status
            body = answer.read(MAX_BODY_BYTES + 1)
    except urllib.error.HTTPError as error:
        error.close()                                           # (it holds the connection and the body)
        return None, "HTTP %d" % error.code
    except urllib.error.URLError as error:
        return None, describe(error.reason)
    except (OSError, ValueError) as error:                      # (a timeout while reading the body, a reset, an address that is no address)
        return None, describe(error)
    except Exception as error:                                  # (http.client's own errors: a half answer, a bad status line)
        return None, type(error).__name__
    if len(body) > MAX_BODY_BYTES:
        return None, "the answer is too long"
    try:
        data = json.loads(body.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return None, "the answer is not JSON"
    if not isinstance(data, dict) or sorted(data) != ["matches", "players"]:
        return None, "the answer is not the two counts"
    matches, players = data["matches"], data["players"]
    if not all(isinstance(v, int) and not isinstance(v, bool) and v >= 0 for v in (matches, players)):
        return None, "the counts are not numbers"
    return (matches, players), None


def say(text):
    print(text, flush=True)


def clock_text(seconds):
    minutes, rest = divmod(int(seconds), 60)
    return "%d min %02d s" % (minutes, rest)


def wait(poll, interval, max_wait_s, grace_s, clock=time.monotonic, sleep=time.sleep, out=say):
    """Polls until the deploy may go on. `poll()` is read_counts without its arguments. Returns 'idle', 'maximum' or 'unreachable'."""
    started = clock()
    down_since = None
    polls = 0
    while True:
        polls += 1
        counts, problem = poll()
        now = clock()
        elapsed = now - started
        if counts is not None:
            down_since = None
            matches, players = counts
            if matches == 0:
                out("deploy wait: poll %d (%s): no match runs (%d %s in rooms that have not started): the server is idle, deploying" % (
                    polls, clock_text(elapsed), players, "person" if players == 1 else "people"))
                return "idle"
            state = "%d %s running with %d %s: waiting" % (matches, "match" if matches == 1 else "matches", players, "person" if players == 1 else "people")
        else:
            if down_since is None:
                down_since = now
            down_for = now - down_since
            if down_for >= grace_s:
                out("deploy wait: poll %d (%s): the busy address has not answered properly for %s (%s): the site is down or has no busy answer yet, deploying" % (
                    polls, clock_text(elapsed), clock_text(down_for), problem))
                return "unreachable"
            state = "the busy address does not answer properly (%s) for %s of %s: waiting" % (problem, clock_text(down_for), clock_text(grace_s))
        if elapsed + interval > max_wait_s:
            if counts is not None:
                out("deploy wait: poll %d (%s): %d %s still running with %d %s: the longest wait of %d minutes is over, DEPLOYING ANYWAY (the matches end when the server restarts)" % (
                    polls, clock_text(elapsed), counts[0], "match" if counts[0] == 1 else "matches", counts[1], "person" if counts[1] == 1 else "people", int(max_wait_s // 60)))
            else:
                out("deploy wait: poll %d (%s): the busy address does not answer properly (%s) and the longest wait of %d minutes is over, DEPLOYING ANYWAY" % (
                    polls, clock_text(elapsed), problem, int(max_wait_s // 60)))
            return "maximum"
        out("deploy wait: poll %d (%s of at most %d min): %s" % (polls, clock_text(elapsed), int(max_wait_s // 60), state))
        sleep(interval)


def cancelled(*_):
    raise KeyboardInterrupt


def parse_minutes(text, default, what):
    """A number of minutes from the command line, or the default (with a line that says so) for something that is none."""
    try:
        value = float(text)
    except (TypeError, ValueError):
        value = math.nan
    if math.isnan(value) or value < 0:
        say("deploy wait: %s %r is not a number of minutes: using %g" % (what, str(text)[:40], default))
        return default
    return value


def main(argv):
    parser = argparse.ArgumentParser(description="Waits until the game server is idle (see the top of this file).")
    parser.add_argument("--url", default="", help="the site's busy answer, https://<site>/busy (empty: do not wait)")
    parser.add_argument("--label", default="the site", help="what the lines call the site (production, staging)")
    parser.add_argument("--max-wait-minutes", default=str(DEFAULT_MAX_WAIT_MINUTES), help="the longest wait, whatever runs (default 180, at most 300; 0: do not wait, look once)")
    parser.add_argument("--grace-minutes", default=str(DEFAULT_GRACE_MINUTES), help="how long an address that does not answer is waited for (default 5)")
    parser.add_argument("--interval", type=float, default=DEFAULT_INTERVAL_SECONDS, help="seconds between two polls (default 60; the tests use less)")
    args = parser.parse_args(argv[1:])
    if args.interval <= 0 or math.isnan(args.interval):
        print("deploy_wait: --interval must be positive", file=sys.stderr)
        return 2
    if args.url and not args.url.startswith(("http://", "https://")):
        print("deploy_wait: --url must be an http(s) address (or empty)", file=sys.stderr)
        return 2
    max_minutes = parse_minutes(args.max_wait_minutes, DEFAULT_MAX_WAIT_MINUTES, "the longest wait")
    grace_minutes = parse_minutes(args.grace_minutes, DEFAULT_GRACE_MINUTES, "the grace")
    if max_minutes > MAX_WAIT_CAP_MINUTES:
        say("deploy wait: %g minutes is more than a job may take: waiting at most %g" % (max_minutes, MAX_WAIT_CAP_MINUTES))
        max_minutes = MAX_WAIT_CAP_MINUTES
    if not args.url:
        say("deploy wait: no busy address is set for %s: deploying without a wait" % args.label)
        return 0
    for name in ("SIGINT", "SIGTERM", "SIGHUP"):                # (a cancelled job gets SIGINT, then SIGTERM; set here, not left to the defaults: a program that a script started in the
        if hasattr(signal, name):                               # background inherits SIGINT as ignored, and Python then raises no KeyboardInterrupt for it)
            signal.signal(getattr(signal, name), cancelled)
    say("deploy wait: polling the busy address of %s every %g s: the deploy goes on when no match runs, after %g minutes at the most, or when the address has not answered for %g minutes" % (
        args.label, args.interval, max_minutes, grace_minutes))
    timeout = min(10.0, max(1.0, args.interval))
    try:
        wait(lambda: read_counts(args.url, timeout), args.interval, max_minutes * 60.0, grace_minutes * 60.0)
    except KeyboardInterrupt:
        say("deploy wait: cancelled (a newer push, or a person): nothing is deployed from this run")
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
