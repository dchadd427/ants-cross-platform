#!/usr/bin/env python3
"""The referee's tick rate of a room, measured with the real clock over a window (for tests/scripts/test_ants_server.sh, whose rate checks used to divide two readings by a nominal 3 s).

usage: measure_tick_rate.py CONTROL_URL ROOM SECRET WINDOW_SECONDS [START_WAIT_SECONDS]

  Asks GET CONTROL_URL/rooms/ROOM about every 0.25 s with the bearer SECRET. The window starts at the first answer that shows a tick (the match's first 5 s are the "Get ready to play!"
  dialog, in which nothing ticks) and ends WINDOW_SECONDS later on this program's own monotonic clock. Each reading is stamped with the middle of its request, so a slow answer (a loaded
  machine, a python or curl that starts late) moves a stamp by half its delay and not by all of it. The rate is the slope of the line through all the readings, in ticks a second: the
  server seals its turns on a fixed schedule and catches up after a stall of its own, so a stall and the burst after it bend one stretch of the readings, not the line.

  Prints one line: "RATE TICKS SECONDS READINGS" (RATE 0.00 when the room never ticked, or fewer than four readings came, within START_WAIT_SECONDS (30) + the window).
"""
import json
import sys
import time
import urllib.request

START_WAIT = 30.0      # seconds the room has to show its first tick
EVERY = 0.25           # seconds between two readings
MIN_READINGS = 4


def slope(points):
    """Least-squares slope of [(seconds, ticks)], 0.0 when it has no width."""
    n = len(points)
    mean_t = sum(t for t, _ in points) / n
    mean_y = sum(y for _, y in points) / n
    width = sum((t - mean_t) ** 2 for t, _ in points)
    return sum((t - mean_t) * (y - mean_y) for t, y in points) / width if width > 0 else 0.0


def measure(read, clock, sleep, window, every=EVERY, start_wait=START_WAIT):
    """read() -> ticks or None (no answer); clock() -> seconds; sleep(seconds). Returns (rate, ticks, seconds, readings)."""
    began = clock()
    points = []
    while True:
        before = clock()
        ticks = read()
        after = clock()
        if ticks is not None and (points or ticks > 0):
            points.append(((before + after) / 2.0, ticks))
        if points and points[-1][0] - points[0][0] >= window:
            break
        if after - began > start_wait + window:
            break
        sleep(every)
    span = points[-1][0] - points[0][0] if points else 0.0
    if len(points) < MIN_READINGS or span < window / 2.0:
        return 0.0, 0, span, len(points)
    return slope(points), points[-1][1] - points[0][1], span, len(points)


def main(argv):
    if len(argv) not in (5, 6):
        print(__doc__)
        return 2
    base, room, secret, window = argv[1], argv[2], argv[3], float(argv[4])
    start_wait = float(argv[5]) if len(argv) == 6 else START_WAIT
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))       # (the server is on this machine: no proxy)
    request = urllib.request.Request("%s/rooms/%s" % (base, room), headers={"Authorization": "Bearer " + secret})

    def read():
        try:
            with opener.open(request, timeout=3.0) as answer:
                return int(json.loads(answer.read().decode()).get("ticks", 0))
        except Exception:
            return None

    rate, ticks, span, readings = measure(read, time.monotonic, time.sleep, window, start_wait=start_wait)
    print("%.2f %d %.2f %d" % (rate, ticks, span, readings))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
