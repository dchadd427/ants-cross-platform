#!/usr/bin/env python3
"""The counter of the site's visits (hub/analytics*.py, hub/useragent.py, hub/geodata.py; docs/ANALYTICS.md): what is counted, what is never kept, and what the admin page is told.

  TheVisitorsCode          the same browser is the same visitor for a day and nobody the day after; the secret of a day is gone, from both files, when the day is over; a restart in the day keeps it
  NothingThatNamesAPerson  the address and the browser's header are in no file, no table and no line of the log; only the country, the browser, the system and the kind of device are kept
  TheBrowserThatAskedNotToBeTracked   Do Not Track and Global Privacy Control (the header, or the page's own word): one visit and a country, once in 30 minutes, nothing else
  TheCountry               from the local list, by IPv4, IPv6 and IPv4 in IPv6; nothing for an address that is not in it; the build step that makes the list
  TheCrawlers              a crawler, a monitor or a script is not counted
  TheEvents                the fields of an event: pages, referrers, widths, languages, timings, errors (their message and their file are cleaned)
  TheDays                  the Pacific day, its turn at midnight and the summer time
  TheNumbers               the sums of a day: visits, visitors, entry and exit, time on the site, new and returning, timings, errors, not tracked
  TheRetention             raw events for 30 days, the sums of every day for good, made before the raw events go
  TheHourNotTheSecond      no moment finer than the hour is kept, and the visits and their times are still told; an error keeps the day it was seen
  TheReferrers             a referrer is kept as its registered name; the sums name a site only when three visitors came from it
  TheRangeLimit            one address range may make only so many new visitors a day
  TheVisitorsAddress       the visitors are told apart by the address that nginx passes; a proxy that passes none is no household, and the log says so
  TheSizeOfTheFile         the file's limit: the oldest raw days go, then new events are dropped; the memory of the counter at its caps
  TheClockAndTheLog        a clock that steps back is not a new day; a cut of the log file that a reader kept from happening is made later
  TheDoor                  the HTTP door: method, path, origin, type, size, body, limits, and a 204 that does not say whether an event was kept
  TheAdminQuestion         the admin page's question: who may ask, what it must hold, the range
  TheSignups               the first login of an account is a number of the day and nothing of the person
  TheSwitch                ANTS_HUB_ANALYTICS=off and the counter's file that cannot be opened
"""
import contextlib
import gzip
import http.server
import io
import ipaddress
import json
import os
import re
import sqlite3
import sys
import tempfile
import threading
import unittest
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import quick_http  # noqa: E402
import test_hub_admin_door as base  # noqa: E402
import fake_discord as fd  # noqa: E402
from login_flow import Visitor  # noqa: E402
from hub import analytics, analyticsdoor, analytics_events as events, analytics_sums as sums, analytics_store, config as configmod, geodata, limits as limitmod, useragent  # noqa: E402

ORIGIN = hc.SITE
UA_WINDOWS = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
UA_IPHONE = "Mozilla/5.0 (iPhone; CPU iPhone OS 17_4 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Mobile/15E148 Safari/604.1"
UA_LINUX = "Mozilla/5.0 (X11; Linux x86_64; rv:125.0) Gecko/20100101 Firefox/125.0"
UA_ANDROID_TABLET = "Mozilla/5.0 (Linux; Android 13; SM-X700) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
UA_EDGE = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0"
UA_GOOGLEBOT = "Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)"
FIXTURE = ["1.0.0.0,1.0.0.255,AU\n", "8.8.8.0,8.8.8.255,US\n", "2001:db8::,2001:db8:ffff:ffff::,DE\n", "5.5.5.0,5.5.5.255,ZZ\n"]
IP_US, IP_AU, IP_DE, IP_NONE = "8.8.8.8", "1.0.0.7", "2001:db8::5", "9.9.9.9"
DAY = 1796000000.0                                  # 2026-11-30 00:53 UTC: Sunday 29 November 2026, 16:53 Pacific (standard time)
HOUR = 3600.0


def view(path="/", **more):
    return dict({"t": "view", "p": path, "w": 1300, "l": "en-US"}, **more)


class Clock:
    def __init__(self, now=DAY):
        self.now = now

    def __call__(self):
        return self.now


def file_bytes(path):
    """Everything that the counter's file holds, with its log and shared-memory files."""
    out = b""
    for suffix in ("", "-wal", "-shm"):
        if os.path.exists(path + suffix):
            with open(path + suffix, "rb") as f:
                out += f.read()
    return out


class CounterTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.path = os.path.join(self.folder.name, "analytics.db")
        self.clock = Clock()
        self.geo = geodata.Geo(geodata.build(FIXTURE, "2026-11"))
        self.open()

    def open(self):
        self.store = analytics_store.Store(self.path, self.clock)
        self.c = analytics.Collector(self.store, self.geo, ORIGIN, self.clock)
        self.addCleanup(self.close)

    def close(self):
        try:
            self.store.close()
        except sqlite3.Error:
            pass

    def reopen(self):
        self.c.stop()
        self.open()

    def send(self, body, ip=IP_US, ua=UA_WINDOWS, untracked=False):
        return self.c.accept(ip, ua, untracked, body)

    def rows(self, where="1"):
        return self.store.db.execute("SELECT * FROM events WHERE %s ORDER BY id" % where).fetchall()

    def columns(self):
        return [r[1] for r in self.store.db.execute("PRAGMA table_info(events)")]

    def row(self, i=0, where="1"):
        return dict(zip(self.columns(), self.rows(where)[i]))

    def day(self, n=0):
        return events.add_days(events.pacific_day(self.clock.now), n)

    def visitors_from(self, ref, n=3, path="/", start=0):
        """`n` different visitors (each from a range of its own) that were sent by the site `ref`."""
        for i in range(start, start + n):
            self.send(view(path, r=ref), ip="9.%d.%d.1" % (i // 250 + 1, i % 250))

    def report(self, first=None, last=None):
        return self.c.report(first or self.day(), last or self.day())


class TheVisitorsCode(CounterTest):
    def test_the_same_browser_is_one_visitor_all_day(self):
        self.send(view("/"))
        self.clock.now += 3000
        self.send(view("/matches"))
        codes = {r[0] for r in self.store.db.execute("SELECT vis FROM events")}
        self.assertEqual(len(codes), 1)
        self.assertEqual(self.report()["days"][0]["visitors"], 1)

    def test_another_address_or_another_browser_is_another_visitor(self):
        self.send(view())
        self.send(view(), ip=IP_AU)
        self.send(view(), ua=UA_LINUX)
        self.assertEqual(self.report()["days"][0]["visitors"], 3)

    def test_the_next_day_is_another_code_for_the_same_browser(self):
        self.send(view())
        first = self.row()["vis"]
        self.clock.now += 86400
        self.c.on_tick()
        self.send(view())
        self.assertNotEqual(self.row(1)["vis"], first)
        self.assertEqual(len(self.row(1)["vis"]), 16)

    def test_the_secret_of_a_day_is_gone_from_the_file_and_its_log_when_the_day_is_over(self):
        secret = self.store.db.execute("SELECT salt FROM salt").fetchone()[0]
        self.assertGreater(len(secret), 16)
        self.send(view())
        self.assertIn(secret, file_bytes(self.path), "the secret of today is kept (a restart in the day must not split a visitor in two)")
        self.clock.now += 86400
        self.c.on_tick()
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM salt").fetchone()[0], 1, "one secret: today's")
        self.assertNotIn(secret, file_bytes(self.path), "the old secret is overwritten and cut out of both files")

    def test_a_secret_that_a_hub_that_was_off_left_is_deleted_at_the_start(self):
        old = self.store.db.execute("SELECT salt FROM salt").fetchone()[0]
        self.reopen()
        self.clock.now += 3 * 86400
        self.reopen()
        self.assertNotIn(old, file_bytes(self.path))
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM salt").fetchone()[0], 1)

    def test_a_restart_in_the_same_day_keeps_the_visitor(self):
        self.send(view("/"))
        before = self.row()["vis"]
        self.reopen()
        self.send(view("/matches"))
        self.assertEqual(self.row(1)["vis"], before)

    def test_a_new_secret_is_made_every_day(self):
        first = self.store.db.execute("SELECT salt FROM salt").fetchone()[0]
        self.clock.now += 86400
        self.c.on_tick()
        self.assertNotEqual(self.store.db.execute("SELECT salt FROM salt").fetchone()[0], first)

    def test_the_file_is_private(self):
        self.assertEqual(os.stat(self.path).st_mode & 0o777, 0o600)


class TheClockAndTheLog(CounterTest):
    def test_a_clock_that_steps_back_across_midnight_is_not_a_new_day(self):
        self.clock.now += 86400
        self.c.on_tick()
        self.send(view())
        code, day, salt = self.row()["vis"], self.row()["day"], self.store.db.execute("SELECT salt FROM salt").fetchone()[0]
        self.clock.now -= 20 * 3600                           # (back into the day before)
        self.c.on_tick()
        self.send(view())
        self.assertEqual((self.row(1)["vis"], self.row(1)["day"]), (code, day), "the same visitor, on the day that the counter was in")
        self.assertEqual([bytes(r[0]) for r in self.store.db.execute("SELECT salt FROM salt")], [salt], "no new secret, and the one that is kept is not deleted")
        self.clock.now += 20 * 3600
        self.c.on_tick()
        self.send(view())
        self.assertEqual(self.row(2)["vis"], code, "and when the clock is back where it was, still the same visitor")

    def test_a_hub_that_starts_with_a_clock_that_is_a_day_back_keeps_the_day_and_its_secret(self):
        self.send(view())
        code, salt = self.row()["vis"], self.store.db.execute("SELECT salt FROM salt").fetchone()[0]
        self.clock.now -= 86400
        self.reopen()
        self.send(view())
        self.assertEqual(self.row(1)["vis"], code)
        self.assertEqual([bytes(r[0]) for r in self.store.db.execute("SELECT salt FROM salt")], [salt])

    def test_a_clock_that_is_corrected_by_more_than_a_few_days_is_a_real_change_of_the_day(self):
        self.send(view())
        first = self.row()["vis"]
        self.clock.now -= (analytics.BACK_DAYS + 3) * 86400
        self.c.on_tick()
        self.send(view())
        self.assertNotEqual(self.row(1)["vis"], first)
        self.assertEqual(self.row(1)["day"], self.day())
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM salt").fetchone()[0], 1)

    def hold_the_file(self):
        reader = sqlite3.connect(self.path, isolation_level=None)
        self.addCleanup(reader.close)
        reader.execute("BEGIN")
        reader.execute("SELECT COUNT(*) FROM salt").fetchone()          # (a reader that holds the file keeps its log from being cut)
        return reader

    def test_a_cut_of_the_log_that_a_reader_kept_from_happening_is_made_when_it_lets_go(self):
        self.send(view())
        secret = self.store.db.execute("SELECT salt FROM salt").fetchone()[0]
        reader = self.hold_the_file()
        self.clock.now += 86400
        self.c.on_tick()
        self.assertIn(secret, file_bytes(self.path), "a reader holds the file: the old secret is still in the log")
        self.clock.now += 3 * analytics_store.CUT_RETRY_SECONDS
        self.c.on_tick()
        self.assertIn(secret, file_bytes(self.path), "and still while the reader holds it")
        reader.execute("ROLLBACK")
        self.clock.now += 3 * analytics_store.CUT_RETRY_SECONDS
        self.c.on_tick()
        self.assertNotIn(secret, file_bytes(self.path), "the next try cuts it")

    def test_a_cut_that_did_not_happen_is_tried_again_every_few_seconds_and_not_at_every_tick(self):
        self.send(view())
        reader = self.hold_the_file()
        self.clock.now += 86400
        self.c.on_tick()
        calls = []
        real = self.store.cut_log
        self.store.cut_log = lambda: calls.append(1) or real()
        try:
            for _ in range(5):
                self.clock.now += 1
                self.c.on_tick()
            self.assertLessEqual(len(calls), 1, "within CUT_RETRY_SECONDS there is at most one more try")
            self.clock.now += 2 * analytics_store.CUT_RETRY_SECONDS
            self.c.on_tick()
            self.assertGreaterEqual(len(calls), 1)
            reader.execute("ROLLBACK")
            self.clock.now += 2 * analytics_store.CUT_RETRY_SECONDS
            self.c.on_tick()
            before = len(calls)
            for _ in range(5):
                self.clock.now += 20
                self.c.on_tick()
            self.assertEqual(len(calls), before, "a log that is cut is left alone")
        finally:
            self.store.cut_log = real


class NothingThatNamesAPerson(CounterTest):
    def send_many(self):
        for path in ("/", "/matches", "/play"):
            self.send(view(path, r="discord.com"), ip=IP_US)
        self.send({"t": "perf", "n": "load", "ms": 800, "v": "0.22.0"})
        self.send({"t": "error", "k": "Page", "n": "TypeError", "m": "x is not a function", "s": "front/a.js:3"})
        self.send(view(), ip=IP_DE, ua=UA_IPHONE)
        self.send({"t": "visit"}, ip=IP_AU, untracked=True)
        self.c.flush(force=True)

    def test_no_address_and_no_header_is_in_the_file(self):
        self.send_many()
        data = file_bytes(self.path)
        for secret in (IP_US, IP_AU, IP_DE, "2001:db8", "8.8.8", "Mozilla", "AppleWebKit", "Windows NT", "Gecko", "iPhone OS", "Googlebot"):
            self.assertNotIn(secret.encode(), data, secret)

    def test_a_row_has_only_the_names_that_the_page_shows(self):
        self.send(view("/", r="discord.com"))
        row = self.row()
        self.assertEqual({k: v for k, v in row.items() if k not in ("id", "at", "vis")},
                         {"day": self.day(), "kind": "view", "ret": 0, "vs": 1, "lb": 0, "country": "US", "browser": "Chrome", "system": "Windows", "device": "Desktop", "wb": 3, "lang": "en",
                          "path": "/", "ref": "discord.com", "ms": 0, "name": "", "msg": "", "src": "", "ver": ""})
        self.assertRegex(row["vis"], r"[0-9a-f]{16}\Z")

    def test_the_columns_of_the_events_are_these_and_no_others(self):
        self.assertEqual(self.columns(), ["id", "at", "day", "kind", "vis", "ret", "vs", "lb", "country", "browser", "system", "device", "wb", "lang", "path", "ref", "ms", "name", "msg", "src", "ver"])

    def test_the_log_has_nothing_of_a_visitor(self):
        import io
        from hub.log import Log
        out = io.StringIO()
        self.c.log = Log(out)
        self.c.trouble(RuntimeError("8.8.8.8 Mozilla"))
        self.assertNotIn("8.8.8.8", out.getvalue())
        self.assertNotIn("Mozilla", out.getvalue())
        self.assertIn("RuntimeError", out.getvalue())

    def test_what_the_page_calls_a_path_is_one_of_the_pages_and_never_the_visitors_text(self):
        self.send(view("/matches?name=Secret+Person#frag"))
        self.send(view("/map/some-map-name"))
        self.send(view("/who/is/this?x=1"))
        self.assertEqual([r[0] for r in self.store.db.execute("SELECT path FROM events ORDER BY id")], ["/matches", "/map", "other"])

    def test_a_referrer_is_the_name_of_a_site_only(self):
        for ref in ("https://secret.example/path?q=1", "10.1.2.3", "Evil Site", "x" * 200, "localhost", "www.Google.com", "beta.example.org", "chat.example.org", "www.CHAT.example.org"):
            self.send(view("/", r=ref))
        self.assertEqual([r[0] for r in self.store.db.execute("SELECT ref FROM events ORDER BY id")], ["", "", "", "", "", "google.com", "example.org", "", ""], "this site itself, with or without www, is no referrer")

    def test_only_a_known_set_of_fields_is_taken(self):
        self.assertEqual(self.send(dict(view(), extra="x")), "shape")
        self.assertEqual(self.send({"t": "view"}), "shape", "a view with no path")
        self.assertEqual(self.send({"t": "click", "p": "/"}), "shape", "an event type that is not counted")
        self.assertEqual(self.send([1]), "shape")
        self.assertEqual(self.send({"t": "visit", "p": "/"}), "shape")
        self.assertEqual(self.rows(), [])


class TheBrowserThatAskedNotToBeTracked(CounterTest):
    def test_it_is_one_visit_and_a_country_and_nothing_else(self):
        self.assertEqual(self.send(view("/matches", r="discord.com"), ip=IP_AU, ua=UA_IPHONE, untracked=True), "ok")
        row = self.row()
        self.assertEqual({k: v for k, v in row.items() if k not in ("id", "at")},
                         {"day": self.day(), "kind": "visit", "vis": "", "ret": 0, "vs": 0, "lb": 0, "country": "AU", "browser": "", "system": "", "device": "", "wb": -1, "lang": "", "path": "", "ref": "",
                          "ms": 0, "name": "", "msg": "", "src": "", "ver": ""})

    def test_the_pages_own_word_counts_as_much_as_the_header(self):
        self.assertEqual(self.send({"t": "visit"}, ip=IP_AU), "ok")
        self.assertEqual(self.row()["kind"], "visit")
        self.assertEqual(self.row()["vis"], "")

    def test_the_visit_is_counted_once_in_half_an_hour_and_again_after(self):
        self.assertEqual(self.send({"t": "visit"}, ip=IP_AU), "ok")
        self.clock.now += 1700
        self.assertEqual(self.send({"t": "visit"}, ip=IP_AU), "again")
        self.assertEqual(self.send(view("/play"), ip=IP_AU, untracked=True), "again", "every page of the visit")
        self.assertEqual(len(self.rows()), 1)
        self.clock.now += 200
        self.assertEqual(self.send({"t": "visit"}, ip=IP_AU), "ok")
        self.assertEqual(len(self.rows()), 2)

    def test_another_browser_is_another_visit(self):
        self.send({"t": "visit"}, ip=IP_AU)
        self.assertEqual(self.send({"t": "visit"}, ip=IP_AU, ua=UA_LINUX), "ok")
        self.assertEqual(self.send({"t": "visit"}, ip=IP_US), "ok")
        self.assertEqual(len(self.rows()), 3)

    def test_what_is_remembered_to_count_it_once_is_in_memory_only(self):
        self.send({"t": "visit"}, ip="1.0.0.9")
        self.assertNotIn(b"1.0.0.9", file_bytes(self.path))
        self.reopen()
        self.assertEqual(self.send({"t": "visit"}, ip="1.0.0.9"), "ok", "a restart forgets the memory: the visit is counted again")

    def test_the_sums_show_it_as_a_visit_in_its_country_and_in_no_other_list(self):
        self.send({"t": "visit"}, ip=IP_AU)
        self.send(view("/"), ip=IP_US)
        s = self.report()
        self.assertEqual((s["days"][0]["visitors"], s["days"][0]["visits"], s["days"][0]["nt"]), (1, 1, 1))
        self.assertEqual(s["lists"]["countries"], {"AU": [0, 1], "US": [1, 0]})
        for name in ("browsers", "systems", "devices", "entry", "exit", "refs", "langs"):
            self.assertEqual(sum(s["lists"][name].values()), 1, name + ": only the tracked visit is in it")

    def test_a_minimal_visit_asks_nothing_of_the_codes(self):
        self.send({"t": "visit"}, ip=IP_AU)
        self.assertEqual(self.report()["lists"]["visitors"], 0)
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM events WHERE vis != ''").fetchone()[0], 0)


class TheCountry(CounterTest):
    def test_the_country_of_an_address(self):
        for ip, want in ((IP_US, "US"), (IP_AU, "AU"), (IP_DE, "DE"), ("::ffff:8.8.8.8", "US"), (IP_NONE, ""), ("5.5.5.5", ""), ("not an address", ""), ("", ""), ("10.0.0.1", "")):
            with self.subTest(ip):
                self.assertEqual(self.geo.country(ip), want)

    def test_the_edges_of_a_range(self):
        for ip, want in (("1.0.0.0", "AU"), ("1.0.0.255", "AU"), ("0.255.255.255", ""), ("1.0.1.0", ""), ("2001:db8:ffff:ffff:1::", "DE"), ("2001:db9::", "")):
            with self.subTest(ip):
                self.assertEqual(self.geo.country(ip), want)

    def test_a_visit_is_kept_with_its_country_and_the_address_is_not(self):
        self.send(view(), ip=IP_AU)
        self.assertEqual(self.row()["country"], "AU")
        self.send(view(), ip=IP_NONE)
        self.assertEqual(self.row(1)["country"], "")

    def test_no_list_means_every_country_is_unknown_and_the_counter_says_so(self):
        self.c.geo = geodata.Geo()
        self.send(view(), ip=IP_US)
        self.assertEqual(self.row()["country"], "")
        self.assertIsNone(self.report()["counter"]["geo"])

    def test_the_list_says_its_month(self):
        self.assertEqual(self.report()["counter"]["geo"], {"name": "DB-IP Lite", "month": "2026-11"})

    def test_a_file_that_is_not_a_list_is_no_list(self):
        for data in (b"", b"ANTSGEO1", b"x" * 200, geodata.build(FIXTURE, "2026-11")[:-3]):
            path = os.path.join(self.folder.name, "bad.bin")
            with open(path, "wb") as f:
                f.write(data)
            with self.subTest(len(data)):
                self.assertEqual(geodata.Geo.open(path).month, "")
        self.assertEqual(geodata.Geo.open(os.path.join(self.folder.name, "none.bin")).month, "")

    def test_the_rows_of_the_csv(self):
        v4, v6 = geodata.parse_rows(["1.0.0.0,1.0.0.255,au\n", "1.0.1.0,1.0.1.255,AU\n", "1.0.4.0,1.0.4.9,NZ\n", "junk\n", "1.0.9.0,1.0.8.0,AU\n", "::1,1.0.0.1,AU\n", "1.0.7.0,1.0.7.1,ZZ\n", "1.0.7.0,1.0.7.1,A\n"])
        self.assertEqual(v4, [(16777216, 16777727, "AU"), (16778240, 16778249, "NZ")])
        self.assertEqual(v6, [])

    def test_a_list_without_a_range_is_refused(self):
        with self.assertRaises(geodata.GeoError):
            geodata.build(["junk\n"], "2026-11")
        with self.assertRaises(geodata.GeoError):
            geodata.build(FIXTURE, "November")


class Site(http.server.BaseHTTPRequestHandler):
    """A little db-ip.com: the files that `months` holds, and the filter of the real site, which answers 403 to the name of Python's own client (the cause of the first builds' empty list)."""
    months = {}
    asked = []
    agents = []

    def do_GET(self):
        agent = self.headers.get("User-Agent", "")
        Site.asked.append(self.path)
        Site.agents.append(agent)
        body = Site.months.get(self.path)
        status = 403 if agent.startswith("Python-urllib") else 404 if body is None else 200
        self.send_response(status)
        self.end_headers()
        if status == 200:
            self.wfile.write(body)

    def log_message(self, *args):
        pass


class TheBuildOfTheCountryList(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.out = os.path.join(self.folder.name, "geo", "country.bin")
        Site.months, Site.asked, Site.agents = {}, [], []
        self.server = quick_http.QuickHTTPServer(("127.0.0.1", 0), Site)
        self.addCleanup(self.server.server_close)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.shutdown)
        self.packed = gzip.compress("".join(FIXTURE).encode())

    def build_it(self, args):
        """geodata.main with the download from the little site of this test, and what it says kept (a test prints nothing)."""
        with contextlib.redirect_stdout(io.StringIO()) as out, contextlib.redirect_stderr(io.StringIO()) as err:
            status = geodata.main(args, self.opener)
        self.said = out.getvalue() + err.getvalue()
        return status

    def opener(self, request, timeout):
        """urlopen for the request that the build made, sent to the little site of this test instead (the same headers, the same file name)."""
        import urllib.request
        return urllib.request.urlopen(urllib.request.Request("http://127.0.0.1:%d/%s" % (self.server.server_port, request.full_url.rsplit("/", 1)[-1]), headers=dict(request.header_items())), timeout=timeout)

    def summary(self):
        """The lines of the build log that say which list the image has: there must be exactly one."""
        return [line for line in self.said.splitlines() if line.startswith("hub.geodata: country list:")]

    def test_it_writes_the_list_of_the_month_that_is_asked_for(self):
        Site.months["/dbip-country-lite-2026-10.csv.gz"] = self.packed
        self.assertEqual(self.build_it(["build", self.out, "2026-10"]), 0)
        if os.name == "posix":
            self.assertEqual(os.stat(self.out).st_mode & 0o777, 0o644, "the image is run by a user that is not the one that built the file: it must be readable")
        geo = geodata.Geo.open(self.out)
        self.assertEqual((geo.month, geo.country(IP_US)), ("2026-10", "US"))

    def test_with_no_month_it_goes_back_one_month_when_the_new_one_is_not_published_yet(self):
        this, previous = geodata.months()
        Site.months["/dbip-country-lite-%s.csv.gz" % previous] = self.packed
        self.assertEqual(self.build_it(["build", self.out]), 0)
        self.assertEqual(geodata.Geo.open(self.out).month, previous)
        self.assertEqual(Site.asked, ["/dbip-country-lite-%s.csv.gz" % this, "/dbip-country-lite-%s.csv.gz" % previous])

    def test_the_request_names_the_build_because_the_real_site_refuses_pythons_own_name(self):
        """The first builds of the image got no list at all: db-ip.com answers 403 to `Python-urllib/3.x`, which is what urllib sends when it is not told a name, and the build went on without a list."""
        Site.months["/dbip-country-lite-2026-10.csv.gz"] = self.packed
        self.assertEqual(self.build_it(["build", self.out, "2026-10"]), 0)
        self.assertEqual(Site.agents, [geodata.USER_AGENT])
        self.assertFalse(geodata.USER_AGENT.startswith("Python-urllib"))
        self.assertRegex(geodata.USER_AGENT, r"\A[A-Za-z0-9._/-]{3,40}\Z", "a name and a number: no site, no address, no person")
        import urllib.request
        with self.assertRaises(geodata.GeoError) as refused:                                  # (and the site's filter is really there in this test: a request with no name of its own is a 403)
            geodata.fetch("2026-10", lambda request, timeout: self.opener(urllib.request.Request(request.full_url), timeout))
        self.assertEqual(str(refused.exception), "HTTP 403")

    def test_a_month_that_is_not_published_yet_falls_back_to_the_one_before_even_when_it_was_asked_for(self):
        Site.months["/dbip-country-lite-2026-11.csv.gz"] = self.packed
        self.assertEqual(self.build_it(["build", self.out, "2026-12"]), 0)
        self.assertEqual(geodata.Geo.open(self.out).month, "2026-11")
        self.assertEqual(Site.asked, ["/dbip-country-lite-2026-12.csv.gz", "/dbip-country-lite-2026-11.csv.gz"])
        self.assertEqual(self.summary(), ["hub.geodata: country list: 2026-11"])
        self.assertIn("hub.geodata: 2026-12: HTTP 404", self.said, "and the month that failed is told, with the answer of the site")

    def test_the_month_that_is_asked_for_is_used_when_it_is_there_and_the_one_before_is_not_asked(self):
        Site.months["/dbip-country-lite-2026-12.csv.gz"] = self.packed
        Site.months["/dbip-country-lite-2026-11.csv.gz"] = gzip.compress(b"1.0.0.0,1.0.0.255,DE\n")
        self.assertEqual(self.build_it(["build", self.out, "2026-12"]), 0)
        self.assertEqual((geodata.Geo.open(self.out).month, geodata.Geo.open(self.out).country(IP_US)), ("2026-12", "US"))
        self.assertEqual(Site.asked, ["/dbip-country-lite-2026-12.csv.gz"])

    def test_the_months_that_are_tried(self):
        from datetime import datetime, timezone
        self.assertEqual(geodata.months(now=datetime(2026, 11, 28, tzinfo=timezone.utc)), ["2026-11", "2026-10"])
        self.assertEqual(geodata.months(now=datetime(2027, 1, 1, tzinfo=timezone.utc)), ["2027-01", "2026-12"], "the year turns")
        self.assertEqual(geodata.months("2027-01"), ["2027-01", "2026-12"])
        self.assertEqual(geodata.months("2026-12"), ["2026-12", "2026-11"])
        self.assertEqual(geodata.months("13/2026"), ["13/2026"], "a typo is tried alone: it fails, and no other month takes its place")
        self.assertEqual(geodata.months("2026-13"), ["2026-13"])
        self.assertEqual(len(geodata.months()), 2)

    def test_the_build_log_has_one_line_that_says_which_list_was_made(self):
        Site.months["/dbip-country-lite-2026-10.csv.gz"] = self.packed
        self.build_it(["build", self.out, "2026-10"])
        self.assertEqual(self.summary(), ["hub.geodata: country list: 2026-10"])

    def test_no_download_is_no_file_a_failure_and_one_line_that_says_so(self):
        self.assertEqual(self.build_it(["build", self.out, "2026-10"]), 1)
        self.assertFalse(os.path.exists(self.out))
        self.assertEqual(len(self.summary()), 1)
        self.assertEqual(self.summary()[0], "hub.geodata: country list: NONE (tried 2026-10, 2026-09): every country of the counter will be unknown")
        self.assertIn("hub.geodata: 2026-10: HTTP 404", self.said)
        self.assertIn("hub.geodata: 2026-09: HTTP 404", self.said)

    def test_a_site_that_cannot_be_reached_is_told_by_its_error_class(self):
        def unreachable(request, timeout):
            raise OSError("no route")
        with contextlib.redirect_stdout(io.StringIO()) as out, contextlib.redirect_stderr(io.StringIO()) as err:
            status = geodata.main(["build", self.out, "2026-10"], unreachable)
        self.assertEqual(status, 1)
        self.assertIn("hub.geodata: 2026-10: OSError", err.getvalue())
        self.assertEqual(out.getvalue() + err.getvalue(), "hub.geodata: 2026-10: OSError\nhub.geodata: 2026-09: OSError\nhub.geodata: country list: NONE (tried 2026-10, 2026-09): every country of the counter will be unknown\n")

    def test_a_file_that_cannot_be_written_is_no_list_and_not_a_traceback(self):
        Site.months["/dbip-country-lite-2026-10.csv.gz"] = self.packed
        with mock.patch.object(geodata, "write_atomically", side_effect=PermissionError("read only")):
            self.assertEqual(self.build_it(["build", self.out, "2026-10"]), 1)
        self.assertEqual(len(self.summary()), 1)
        self.assertIn("NONE", self.summary()[0])

    def test_a_download_that_is_not_a_list_is_no_file(self):
        Site.months["/dbip-country-lite-2026-10.csv.gz"] = gzip.compress(b"nothing,here\n")
        self.assertEqual(self.build_it(["build", self.out, "2026-10"]), 1)
        Site.months["/dbip-country-lite-2026-10.csv.gz"] = b"not gzip"
        self.assertEqual(self.build_it(["build", self.out, "2026-10"]), 1)
        self.assertFalse(os.path.exists(self.out))

    def test_a_bad_command_line(self):
        self.assertEqual(self.build_it([]), 2)
        self.assertEqual(self.build_it(["fetch", "x"]), 2)
        self.assertEqual(self.build_it(["build", self.out, "13/2026"]), 1)
        self.assertEqual(Site.asked, [], "a month that is not a month is not asked for")


class TheCrawlers(CounterTest):
    def test_a_crawler_a_monitor_or_a_script_is_not_counted(self):
        for ua in (UA_GOOGLEBOT, "curl/8.4.0", "python-requests/2.31", "Mozilla/5.0 (X11; Linux x86_64) HeadlessChrome/120.0 Safari/537.36", "Pingdom.com_bot_version_1.4", "", "x" * 600,
                   "Mozilla/5.0 (compatible; bingbot/2.0; +http://www.bing.com/bingbot.htm)", "Slackbot-LinkExpanding 1.0", "UptimeRobot/2.0", "Go-http-client/1.1"):
            with self.subTest(ua[:30]):
                self.assertEqual(self.send(view(), ua=ua), "crawler")
                self.assertEqual(self.send({"t": "visit"}, ua=ua, untracked=True), "crawler")
        self.assertEqual(self.rows(), [])

    def test_a_crawler_that_asks_not_to_be_tracked_is_still_not_counted(self):
        self.assertEqual(self.send({"t": "visit"}, ua=UA_GOOGLEBOT, untracked=True), "crawler")

    def test_the_counter_says_how_many_it_left_out(self):
        self.send(view(), ua=UA_GOOGLEBOT)
        self.send(view(), ua="curl/8")
        self.assertEqual(self.report()["counter"]["crawlers_today"], 2)

    def test_the_names_of_the_browsers_the_systems_and_the_devices(self):
        want = {UA_WINDOWS: ("Chrome", "Windows", "Desktop"), UA_IPHONE: ("Safari", "iOS", "Phone"), UA_LINUX: ("Firefox", "Linux", "Desktop"),
                UA_ANDROID_TABLET: ("Chrome", "Android", "Tablet"), UA_EDGE: ("Edge", "Windows", "Desktop"),
                "Mozilla/5.0 (Linux; Android 14; SM-S918B) AppleWebKit/537.36 (KHTML, like Gecko) SamsungBrowser/24.0 Chrome/117.0.0.0 Mobile Safari/537.36": ("Samsung Internet", "Android", "Phone"),
                "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Safari/605.1.15": ("Safari", "macOS", "Desktop"),
                "Mozilla/5.0 (Windows NT 10.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36 OPR/110.0": ("Opera", "Windows", "Desktop"),
                "Mozilla/5.0 (X11; CrOS x86_64 14541.0.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36": ("Chrome", "ChromeOS", "Desktop"),
                "Mozilla/5.0 (iPad; CPU OS 17_4 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Mobile/15E148 Safari/604.1": ("Safari", "iOS", "Tablet"),
                "SomethingElse/1.0": ("Other", "Other", "Desktop")}
        for ua, names in want.items():
            with self.subTest(ua[:40]):
                self.assertEqual(useragent.parse(ua), names)


class TheEvents(CounterTest):
    def test_the_width_of_the_window_is_one_of_five_sizes(self):
        for width, want in ((1, 0), (399, 0), (400, 1), (767, 1), (768, 2), (1199, 2), (1200, 3), (1599, 3), (1600, 4), (20000, 4), (0, -1), (20001, -1), (-5, -1), (True, -1), (1.5, -1), ("1300", -1), (None, -1)):
            with self.subTest(width):
                self.assertEqual(events.width_bucket(width), want)

    def test_the_language_is_the_first_part(self):
        for text, want in (("en-US", "en"), ("de", "de"), ("zh_Hans_CN", "zh"), ("fil", "fil"), ("e", ""), ("english", ""), ("", ""), (5, ""), ("EN", "en"), ("en" + "-xx" * 20, "")):
            with self.subTest(text):
                self.assertEqual(events.language_of(text), want)

    def test_a_timing_has_a_name_a_size_and_may_have_the_version(self):
        self.assertEqual(self.send({"t": "perf", "n": "load", "ms": 900, "v": "0.22.0"}), "ok")
        self.assertEqual(self.send({"t": "perf", "n": "game", "ms": 4200, "v": "not a version"}), "ok")
        self.assertEqual([(r["name"], r["ms"], r["ver"]) for r in (self.row(0), self.row(1))], [("load", 900, "0.22.0"), ("game", 4200, "")])
        for bad in ({"t": "perf", "n": "other", "ms": 5}, {"t": "perf", "n": "load", "ms": 0}, {"t": "perf", "n": "load", "ms": 600001}, {"t": "perf", "n": "load", "ms": "5"},
                    {"t": "perf", "n": "load", "ms": 5.5}, {"t": "perf", "n": "load", "ms": True}, {"t": "perf", "ms": 5}):
            with self.subTest(bad):
                self.assertEqual(self.send(bad), "shape")

    def test_an_error_has_its_kind_its_class_a_cleaned_message_and_the_sites_own_file(self):
        body = {"t": "error", "k": "Page", "n": "TypeError", "m": "Cannot read properties of null (reading 'classList') at https://evil.example/x?token=abc123 for bob@example.org 10.1.2.3 \"secret words\"",
                "s": ORIGIN + "/front/lobby_net.js?v=3:412:5"}
        self.assertEqual(self.send(body), "ok")
        row = self.row()
        self.assertEqual((row["name"], row["src"]), ("Page", "front/lobby_net.js:412"))
        self.assertEqual(row["msg"], "TypeError: Cannot read properties of null (reading 'classList') at <url> for <email> <address> \"...\"")
        for kept in file_bytes(self.path).split(b"\0"):
            self.assertNotIn(b"bob@example.org", kept)
            self.assertNotIn(b"secret words", kept)
            self.assertNotIn(b"evil.example", kept)

    def test_a_file_of_another_site_or_of_no_shape_is_not_kept(self):
        for source in ("https://evil.example/front/a.js:3", "http://beta.example/front/a.js:3", "javascript:alert(1)", "front/a.js", "front/../../etc/passwd:1", "a" * 500, 5, None, ""):
            with self.subTest(str(source)[:30]):
                self.assertEqual(events.clean_source(source, ORIGIN), "")
        self.assertEqual(events.clean_source("front/a.js:3:9", ORIGIN), "front/a.js:3")
        self.assertEqual(events.clean_source("/ants.wasm:88", ORIGIN), "ants.wasm:88")

    def test_what_an_error_may_quote_is_cut(self):
        for text, want in (("Unexpected token 'h', \"hello world\"... is not valid JSON", "Unexpected token 'h', \"...\"... is not valid JSON"),
                           ("a 'long phrase with blanks' b", "a '...' b"), ("id 123456789012 here", "id <number> here"), ("key " + "A" * 40, "key <token>"),
                           ("word " * 100, ("word " * 32).rstrip()), ("caf\u00e9 \u202e", "caf"), (5, ""), ("[::1]:8080 and fe80::1:2:3", "[::1]:8080 and <address>")):
            with self.subTest(text if isinstance(text, int) else text[:30]):
                self.assertEqual(events.clean_message(text), want)

    def test_an_error_with_a_class_that_is_not_a_name_is_refused(self):
        for name in ("Type Error", "", "A", "x" * 41, "Error<script>", 5, None):
            with self.subTest(name):
                self.assertEqual(self.send({"t": "error", "k": "Page", "n": name, "m": "x"}), "shape")
        self.assertEqual(self.send({"t": "error", "k": "Other", "n": "Error", "m": "x"}), "shape")

    def test_a_browsers_harmless_note_is_no_error_of_the_site(self):
        """ResizeObserver's "loop completed with undelivered notifications" is raised by the browser's own layout and means nothing is wrong: it is not counted, and it is not a refusal either (the page that
        sent it is told what it is told for every event). The page's own script drops them first (web/analytics.js; its check has the same messages): this is for a page that was open before the update."""
        notes = ("ResizeObserver loop completed with undelivered notifications.", "ResizeObserver loop completed with undelivered notifications", "ResizeObserver loop limit exceeded",
                 "resizeobserver loop limit exceeded.")
        for note in notes:
            for name in ("Error", "ErrorEvent"):
                for kind in ("Page", "Game"):
                    with self.subTest((note, name, kind)):
                        self.assertEqual(self.send({"t": "error", "k": kind, "n": name, "m": note}), "benign")
        self.assertEqual(self.rows(), [])
        self.assertEqual(self.report()["errors"], [])
        self.assertEqual((self.report()["counter"]["refused_today"], self.report()["counter"]["accepted_today"]), (0, 0), "not refused and not accepted: dropped on purpose")

    def test_an_error_that_only_mentions_a_harmless_note_is_still_an_error(self):
        for text in ("Cannot recover from ResizeObserver loop limit exceeded", "ResizeObserver loop limit exceeded while sizing the board", "ResizeObserver is not defined",
                     "ResizeObserver loop limit exceeded\u0000x"):
            with self.subTest(text):
                self.assertEqual(self.send({"t": "error", "k": "Page", "n": "TypeError", "m": text}), "ok")
        self.assertEqual(len(self.report()["errors"]), 4)

    def test_a_note_that_was_counted_before_the_filter_is_not_in_the_report_and_the_kept_sums_stay_as_they_were(self):
        note = dict(events.EMPTY, kind="error", name="Page", msg="Error: ResizeObserver loop completed with undelivered notifications.")
        real = dict(events.EMPTY, kind="error", name="Page", msg="TypeError: x is null", src="front/a.js:3")
        for vis in ("a" * 16, "b" * 16):
            self.store.add_event(int(self.clock.now), self.day(), note, vis=vis)
        self.store.add_event(int(self.clock.now), self.day(), real, vis="a" * 16)
        today = self.report()
        self.assertEqual([e["msg"] for e in today["errors"]], ["TypeError: x is null"])
        self.assertEqual(today["days"][0]["errors"], 1)
        self.clock.now += 86400                                 # (the day is over: its sums are made and kept)
        self.c.on_tick()
        yesterday = self.report(self.day(-1), self.day(-1))
        self.assertEqual(([e["msg"] for e in yesterday["errors"]], yesterday["days"][0]["errors"]), (["TypeError: x is null"], 1))
        kept = json.loads(self.store.db.execute("SELECT sums FROM days WHERE day = ?", (self.day(-1),)).fetchone()[0])
        self.assertEqual(sorted(e[1] for e in kept["errors"]), ["Error: ResizeObserver loop completed with undelivered notifications.", "TypeError: x is null"], "what was kept is not rewritten")

    def test_the_paths_of_the_pages(self):
        for path, want in (("/", "/"), ("/play", "/play"), ("/play/", "/play"), ("/matches?page=2", "/matches"), ("/match#x", "/match"), ("/mapmaker", "/mapmaker"), ("/map-library", "/map-library"),
                           ("/map/treasure-island", "/map"), ("/map/", "other"), ("/map/Bad Name", "other"), ("/legal", "/legal"), ("/changelog", "/changelog"), ("/admin", "other"), ("/nothing", "other"),
                           ("play", "other"), ("/" + "a" * 300, "other"), (5, "other"), ("/map/" + "a" * 101, "other")):
            with self.subTest(path if isinstance(path, int) else path[:30]):
                self.assertEqual(events.page_of(path), want)


class TheDays(unittest.TestCase):
    def test_the_pacific_day_turns_at_midnight_pacific_time(self):
        start = 1795939200.0                                # 2026-11-29 08:00 UTC: 00:00 Pacific standard time (UTC-8)
        self.assertEqual(events.pacific_day(start - 1), "2026-11-28")
        self.assertEqual(events.pacific_day(start), "2026-11-29")
        self.assertEqual(events.pacific_day(start + 86399), "2026-11-29")
        self.assertEqual(events.pacific_day(start + 86400), "2026-11-30")

    def test_in_summer_time_it_turns_an_hour_earlier_in_utc(self):
        start = 1782889200.0                                # 2026-07-01 07:00 UTC: 00:00 Pacific daylight time (UTC-7)
        self.assertEqual(events.pacific_day(start - 1), "2026-06-30")
        self.assertEqual(events.pacific_day(start), "2026-07-01")

    def test_the_day_the_clocks_go_forward_has_23_hours(self):
        first, next_day = 1772956800.0, 1773039600.0        # 2026-03-08 08:00 UTC (00:00 PST) and 2026-03-09 07:00 UTC (00:00 PDT); the change is at 02:00 PST, 10:00 UTC on the 8th
        self.assertEqual(events.pacific_day(first - 1), "2026-03-07")
        self.assertEqual(events.pacific_day(first), "2026-03-08")
        self.assertEqual(events.pacific_day(1772964000.0), "2026-03-08")
        self.assertEqual(events.pacific_day(next_day - 1), "2026-03-08")
        self.assertEqual(events.pacific_day(next_day), "2026-03-09")

    def test_the_day_the_clocks_go_back_has_25_hours(self):
        first, next_day = 1793516400.0, 1793606400.0        # 2026-11-01 07:00 UTC (00:00 PDT) and 2026-11-02 08:00 UTC (00:00 PST); the change is at 02:00 PDT, 09:00 UTC on the 1st
        self.assertEqual(events.pacific_day(first - 1), "2026-10-31")
        self.assertEqual(events.pacific_day(first), "2026-11-01")
        self.assertEqual(events.pacific_day(1793523600.0), "2026-11-01")
        self.assertEqual(events.pacific_day(next_day - 1), "2026-11-01")
        self.assertEqual(events.pacific_day(next_day), "2026-11-02")

    def test_valid_days(self):
        for text, ok in (("2026-11-28", True), ("2026-02-30", False), ("2026-1-1", False), ("26-11-28", False), ("2026-11-28 ", False), ("", False), (5, False), ("2026-13-01", False), ("\u0662\u0660\u0662\u0666-\u0661\u0661-\u0662\u0668", False)):
            with self.subTest(text):
                self.assertEqual(events.valid_day(text) is not None, ok)


class TheNumbers(CounterTest):
    def test_pages_of_one_visitor_less_than_half_an_hour_apart_are_one_visit(self):
        self.send(view("/"))
        self.clock.now += 1700
        self.send(view("/matches"))
        self.clock.now += 1700
        self.send(view("/play"))
        s = self.report()
        self.assertEqual((s["days"][0]["visitors"], s["days"][0]["visits"]), (1, 1))
        self.assertEqual((s["lists"]["entry"], s["lists"]["exit"]), ({"/": 1}, {"/play": 1}))

    def test_a_gap_of_more_than_half_an_hour_makes_a_second_visit_of_the_same_visitor(self):
        self.send(view("/"))
        self.clock.now += 1801
        self.send(view("/matches"))
        s = self.report()
        self.assertEqual((s["days"][0]["visitors"], s["days"][0]["visits"]), (1, 2))
        self.assertEqual((s["lists"]["entry"], s["lists"]["exit"]), ({"/": 1, "/matches": 1}, {"/": 1, "/matches": 1}))

    def test_a_leave_ends_the_visit_later_and_counts_for_its_time(self):
        self.send(view("/"))
        self.clock.now += 100
        self.send({"t": "leave", "p": "/"})
        s = self.report()
        self.assertEqual(s["lists"]["dur"][sum(1 for e in sums.DURATION_EDGES if 100 >= e)], 1)
        self.assertTrue(90 <= s["lists"]["dur_sum"] < 120, "the time is the bin that holds it (90 to 120 seconds), by its middle")
        self.assertEqual(s["lists"]["exit"], {"/": 1})

    def test_a_leave_whose_view_was_another_day_is_not_a_visitor(self):
        self.send({"t": "leave", "p": "/"})
        self.assertEqual(self.report()["days"][0]["visitors"], 0)

    def test_the_time_on_the_site_has_a_middle(self):
        for i, seconds in enumerate((0, 3, 40, 40, 700)):
            self.send(view("/"), ip="8.8.8.%d" % (i + 1))
            self.clock.now += seconds
            self.send({"t": "leave", "p": "/"}, ip="8.8.8.%d" % (i + 1))
        lists = self.report()["lists"]
        self.assertEqual(sum(lists["dur"]), 5)
        self.assertEqual(lists["dur_mid"], sums.middle(lists["dur"]))
        self.assertTrue(30 <= lists["dur_mid"] < 45, lists["dur_mid"])

    def test_the_middle_of_nothing_is_nothing(self):
        self.assertEqual(sums.middle([0] * 15), 0)

    def test_the_lists_count_each_visitor_once_and_each_visit_once(self):
        for ua in (UA_WINDOWS, UA_IPHONE, UA_LINUX, UA_WINDOWS):
            self.send(view("/", w=390 if ua == UA_IPHONE else 1920, l="de-DE" if ua == UA_LINUX else "en-GB"), ua=ua, ip=IP_AU if ua == UA_LINUX else IP_US)
        lists = self.report()["lists"]
        self.assertEqual(lists["browsers"], {"Chrome": 1, "Safari": 1, "Firefox": 1})
        self.assertEqual(lists["systems"], {"Windows": 1, "iOS": 1, "Linux": 1})
        self.assertEqual(lists["devices"], {"Desktop": 2, "Phone": 1})
        self.assertEqual(lists["widths"], [1, 0, 0, 0, 2])
        self.assertEqual(lists["langs"], {"en": 2, "de": 1})
        self.assertEqual(lists["countries"], {"AU": [1, 0], "US": [2, 0]})

    def test_the_referrer_of_a_visit_is_the_one_of_its_first_page(self):
        self.visitors_from("discord.com", 3)
        self.clock.now += 5
        self.send(view("/matches", r="google.com"), ip="9.1.0.1")
        self.send(view("/", r=""), ip=IP_AU)
        self.assertEqual(self.report()["lists"]["refs"], {"discord.com": 3, "": 1})

    def test_the_timings_have_their_middle_and_their_slow_end(self):
        for ms in range(100, 2100, 100):
            self.send({"t": "perf", "n": "load", "ms": ms, "v": "0.22.0"})
        self.send({"t": "perf", "n": "game", "ms": 5000})
        day = self.report()["days"][0]
        self.assertEqual(day["perf"]["load"], [20, 1000, 1500, 1900])
        self.assertEqual(day["perf"]["game"], [1, 5000, 5000, 5000])
        self.assertEqual(day["vers"], {"0.22.0": 20})

    def test_errors_are_grouped_by_kind_message_and_file(self):
        same = {"t": "error", "k": "Page", "n": "TypeError", "m": "x is null", "s": "front/a.js:3"}
        self.send(same)
        self.send(same)
        self.send(same, ip=IP_AU)
        self.send(dict(same, s="front/a.js:4"))
        self.send(dict(same, k="Game"))
        errors = self.report()["errors"]
        self.assertEqual([(e["kind"], e["src"], e["n"], e["visitors"]) for e in errors], [("Page", "front/a.js:3", 3, 2), ("Game", "front/a.js:3", 1, 1), ("Page", "front/a.js:4", 1, 1)])
        self.assertEqual(self.report()["days"][0]["errors"], 5)

    def test_new_and_returning(self):
        self.send(view("/"))
        self.send(view("/"), ip=IP_AU)
        self.clock.now += 86400
        self.c.on_tick()
        self.send(view("/"))                                  # seen yesterday
        self.send(view("/"), ip=IP_DE)                         # not seen before
        today = self.report(self.day(), self.day())["lists"]
        self.assertEqual((today["new"], today["returning"]), (1, 1))
        yesterday = self.report(self.day(-1), self.day(-1))["lists"]
        self.assertEqual((yesterday["new"], yesterday["returning"]), (2, 0))

    def test_returning_means_seen_in_the_6_days_before_and_not_longer_ago(self):
        self.send(view("/"))
        for _ in range(6):
            self.clock.now += 86400
            self.c.on_tick()
        self.send(view("/"))                                  # 6 days later: still returning
        self.assertEqual(self.report()["lists"]["returning"], 1)
        self.clock.now += 86400
        self.c.on_tick()
        self.send(view("/"), ip=IP_AU)
        self.clock.now += 6 * 86400
        for _ in range(6):
            self.c.on_tick()
        self.clock.now += 86400
        self.c.on_tick()
        self.send(view("/"), ip=IP_AU)                         # 7 days later: new again
        self.assertEqual(self.report()["lists"]["returning"], 0)

    def test_the_link_between_days_is_in_memory_only(self):
        self.send(view("/"), ip="1.0.0.77")
        self.clock.now += 86400
        self.c.on_tick()
        self.send(view("/"), ip="1.0.0.77")
        self.assertEqual(self.row(1)["ret"], 1)
        data = file_bytes(self.path)
        self.assertNotIn(b"1.0.0.77", data)
        self.assertEqual(sorted({r[0] for r in self.store.db.execute("SELECT ret FROM events")}), [0, 1])
        self.reopen()
        self.send(view("/"), ip="1.0.0.77")
        self.assertEqual(self.row(2)["ret"], 0, "a restart forgets who was seen: the visitor counts as new")

    def test_the_flag_of_a_visitor_stays_the_same_all_day(self):
        self.send(view("/"))
        self.clock.now += 86400
        self.c.on_tick()
        self.send(view("/"))
        self.send(view("/matches"))
        self.assertEqual([r[0] for r in self.store.db.execute("SELECT ret FROM events WHERE day = ? ORDER BY id", (self.day(),))], [1, 1])

    def test_signups_are_a_number_of_the_day(self):
        self.c.signup()
        self.c.signup()
        self.assertEqual(self.report()["days"][0]["signups"], 2)
        self.clock.now += 86400
        self.c.on_tick()
        self.assertEqual(self.report(self.day(-1), self.day(-1))["days"][0]["signups"], 2)

    def test_the_change_is_against_the_days_before(self):
        self.send(view("/"))
        self.clock.now += 86400
        self.c.on_tick()
        for i in range(3):
            self.send(view("/"), ip="8.8.8.%d" % (i + 20))
        s = self.report(self.day(), self.day())
        self.assertEqual((s["days"][0]["visitors"], s["before"]["visitors"]), (3, 1))

    def test_a_range_is_checked(self):
        today = self.day()
        for first, last in (("2026-11-29", "2026-11-28"), ("garbage", today), (today, "2099-01-01"), (today, events.add_days(today, 1)), (events.add_days(today, 1), events.add_days(today, 2)), (events.add_days(today, -400), today), ("2026-02-30", today), (None, None), (5, today)):
            with self.subTest((first, last)):
                self.assertIsNone(self.c.report(first, last))
        self.assertEqual(len(self.c.report(events.add_days(today, -399), today)["days"]), 400)

    def test_an_empty_day_is_all_zeros(self):
        s = self.report()
        self.assertEqual((s["days"][0]["visitors"], s["days"][0]["visits"], s["days"][0]["nt"]), (0, 0, 0))
        self.assertEqual(s["errors"], [])
        self.assertEqual(s["counter"]["last_at"], None)

    def test_the_state_of_the_counter(self):
        self.send(view())
        self.send(dict(view(), x=1))
        self.send(view(), ua=UA_GOOGLEBOT)
        state = self.report()["counter"]
        self.assertEqual((state["accepted_today"], state["refused_today"], state["crawlers_today"]), (1, 1, 1))
        self.assertEqual((state["last_at"], state["raw_days"], state["oldest_raw"], state["first_day"]), (int(DAY), 30, self.day(), self.day()))

    def test_the_lists_keep_their_biggest_and_say_how_many_others(self):
        for i in range(sums.LIST_MAX + 5):
            self.visitors_from("site%d.example" % i, 3, start=3 * i)
        refs = self.report()["lists"]["refs"]
        self.assertLessEqual(len(refs), sums.LIST_MAX + 1)
        self.assertEqual(sum(refs.values()), 3 * (sums.LIST_MAX + 5), "every visit is in a list: the ones that are not among the biggest are in `other`")
        self.assertGreaterEqual(refs["other"], 15)


class TheRetention(CounterTest):
    def test_raw_events_are_kept_30_days_and_the_sums_of_every_day_for_good(self):
        self.send(view("/"))
        old_day = self.day()
        self.clock.now += 86400 * 30
        self.c.on_tick()
        self.assertEqual(len(self.rows()), 1, "the day 30 days back is still there")
        self.send(view("/"), ip=IP_AU)
        self.clock.now += 86400
        self.c.on_tick()
        self.assertEqual([r[0] for r in self.store.db.execute("SELECT DISTINCT day FROM events")], [self.day(-1)], "the first day's raw events are gone")
        kept = self.report(old_day, old_day)["days"][0]
        self.assertEqual((kept["visitors"], kept["visits"]), (1, 1), "its sums are not")
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM days WHERE day = ?", (old_day,)).fetchone()[0], 1)

    def test_the_sums_of_a_day_are_made_before_its_raw_events_go(self):
        self.send(view("/"))
        day = self.day()
        self.clock.now += 86400 * 40
        self.reopen()
        self.c.on_tick()
        self.assertEqual(self.rows(), [])
        self.assertEqual(self.report(day, day)["days"][0]["visitors"], 1)

    def test_the_sums_hold_nothing_that_names_a_person(self):
        self.send(view("/matches", r="discord.com"), ip="1.0.0.123")
        self.send({"t": "error", "k": "Page", "n": "TypeError", "m": "x", "s": "front/a.js:1"})
        self.clock.now += 86400
        self.c.on_tick()
        kept = self.store.db.execute("SELECT sums FROM days").fetchone()[0]
        vis = self.row()["vis"]
        for secret in ("1.0.0.123", "Mozilla", vis):
            self.assertNotIn(secret, kept)

    def test_what_is_deleted_is_overwritten_and_cut_from_both_files(self):
        self.send(view("/matches", r="needle-referrer.example"), ip=IP_AU)
        self.visitors_from("needle-referrer.example", 2)
        code = self.row()["vis"].encode()
        self.assertIn(code, file_bytes(self.path))
        self.assertIn(b"needle-referrer.example", file_bytes(self.path))
        self.clock.now += 86400 * 32
        self.c.on_tick()
        self.assertNotIn(code, file_bytes(self.path), "the visitor's code of a day that is past its 30 days is overwritten and cut out of both files")
        self.assertIn("needle-referrer.example", self.store.db.execute("SELECT sums FROM days").fetchone()[0], "the sums of the day, which are kept, still have the name of a site that three visitors came from")

    def test_the_counters_of_old_days_go_too(self):
        self.send(view())
        self.c.flush(force=True)
        self.assertEqual(self.store.counter(self.day(), "accepted"), 1)
        self.clock.now += 86400 * 32
        self.c.on_tick()
        self.assertEqual(self.store.counter(events.add_days(self.day(), -32), "accepted"), 0)

    def test_the_hours_work_is_done_once_an_hour(self):
        self.send(view())
        self.clock.now += 86400 * 31
        self.c.on_tick()
        self.send(view(), ip=IP_AU)
        self.clock.now += 60
        self.c.on_tick()
        self.assertEqual(self.c.next_maintain, self.clock.now - 60 + HOUR)

    def test_a_file_that_refuses_costs_the_event_and_never_the_hub(self):
        class Broken:
            def execute(self, *a, **k):
                raise sqlite3.OperationalError("database is locked")
        real = self.store.db
        self.store.db = Broken()
        try:
            self.assertEqual(self.send(view()), "busy")
            self.c.signup()
        finally:
            self.store.db = real
        self.assertEqual(self.rows(), [])


class TheLimitOfADay(CounterTest):
    """A flood that gets past the allowances of the door cannot fill the disk: a day keeps DAY_MAX events and the rest are dropped, and counted."""

    def test_events_over_the_limit_of_the_day_are_dropped_and_counted_and_the_next_day_starts_again(self):
        with mock.patch.object(analytics, "DAY_MAX", 3):
            answers = [self.send(view(), ip="8.8.8.%d" % (i + 1)) for i in range(5)]
            self.assertEqual(answers, ["ok", "ok", "ok", "full", "full"])
            self.assertEqual(len(self.rows()), 3)
            self.assertEqual(self.report()["counter"]["dropped_today"], 2)
            self.assertEqual(self.send({"t": "visit"}, ip=IP_AU), "full", "a not tracked visit too")
            self.assertEqual(self.send(dict(view(), x=1)), "shape", "what is no event is refused, as before")
            self.clock.now += 86400
            self.c.on_tick()
            self.assertEqual(self.send(view()), "ok")

    def test_a_restart_in_the_day_does_not_give_the_day_a_new_limit(self):
        with mock.patch.object(analytics, "DAY_MAX", 2):
            self.send(view(), ip="8.8.8.1")
            self.send(view(), ip="8.8.8.2")
            self.reopen()
            self.assertEqual(self.send(view(), ip="8.8.8.3"), "full")


class TheHourNotTheSecond(CounterTest):
    """The web server's own log holds the second, the address and the browser of every page that is opened, so nothing finer than the hour may be kept with an event."""

    def test_an_event_is_kept_with_the_hour_and_the_last_event_in_the_file_too(self):
        self.clock.now = DAY + 1234.567
        self.send(view("/"))
        self.clock.now += 61
        self.send({"t": "leave", "p": "/"})
        self.send({"t": "visit"}, ip=IP_AU)
        self.c.flush(force=True)
        hour = int(DAY + 1234) // 3600 * 3600
        self.assertEqual({r[0] for r in self.store.db.execute("SELECT at FROM events")}, {hour})
        self.assertEqual(int(self.store.meta("last_at")), hour, "the moment of the last event in the file is an hour too")
        self.assertEqual(self.report()["counter"]["last_at"], int(self.clock.now), "the page that asks, a moment later, still sees the exact one: it is in memory only")
        self.reopen()
        self.assertEqual(self.c.last_at, hour, "a restart knows only the hour")

    def test_a_visit_and_its_time_are_told_without_a_clock_in_the_file(self):
        self.send(view("/"))
        self.clock.now += 100
        self.send(view("/matches"))
        self.clock.now += 1900                                   # (more than half an hour: a second visit)
        self.send(view("/play"))
        self.clock.now += 30
        self.send({"t": "leave", "p": "/play"})
        rows = self.store.db.execute("SELECT path, vs, lb FROM events ORDER BY id").fetchall()
        self.assertEqual(rows, [("/", 1, 0), ("/matches", 0, sums.length_bin(100)), ("/play", 1, 0), ("/play", 0, sums.length_bin(30))])
        s = self.report()
        self.assertEqual((s["days"][0]["visits"], s["lists"]["entry"], s["lists"]["exit"]), (2, {"/": 1, "/play": 1}, {"/matches": 1, "/play": 1}))
        self.assertEqual(s["lists"]["dur"][sums.length_bin(100)] + s["lists"]["dur"][sums.length_bin(30)], 2)

    def test_a_visit_goes_on_across_a_gap_that_a_leave_closes_and_a_restart_starts_a_new_one(self):
        self.send(view("/"))
        self.clock.now += 1000
        self.send({"t": "leave", "p": "/"})
        self.clock.now += 1000                                   # (under half an hour since the leave: still the visit)
        self.send(view("/matches"))
        self.assertEqual(self.report()["days"][0]["visits"], 1)
        self.reopen()
        self.clock.now += 10
        self.send(view("/play"))
        s = self.report()["days"][0]
        self.assertEqual((s["visitors"], s["visits"]), (1, 2), "the memory of where a visit stands is not kept: a visit that a restart cuts in two is two")

    def test_an_error_keeps_the_day_it_was_seen_and_never_the_time(self):
        error = {"t": "error", "k": "Page", "n": "TypeError", "m": "x is null", "s": "front/a.js:3"}
        self.send(error)
        self.clock.now += 86400
        self.c.on_tick()
        self.send(error, ip=IP_AU)
        today, yesterday = self.day(), self.day(-1)
        kept = json.loads(self.store.db.execute("SELECT sums FROM days WHERE day = ?", (yesterday,)).fetchone()[0])
        self.assertEqual(kept["errors"][0][5], yesterday, "kept for good: the day")
        both = self.report(yesterday, today)["errors"][0]
        self.assertEqual((both["n"], both["first"], both["last"]), (2, yesterday, today))
        self.assertEqual(self.report(today, today)["errors"][0]["last"], today)

    def test_nothing_the_sums_keep_is_a_clock(self):
        self.clock.now = DAY + 777
        for i in range(4):
            self.send(view("/", r="x%d.example.com" % (i % 2)), ip="9.%d.0.1" % (i + 1))
        self.send({"t": "error", "k": "Page", "n": "TypeError", "m": "boom", "s": "front/a.js:3"})
        self.clock.now += 86400
        self.c.on_tick()
        kept = self.store.db.execute("SELECT sums FROM days").fetchone()[0]
        for moment in (int(DAY + 777), int(DAY + 777) // 3600 * 3600):
            self.assertNotIn(str(moment), kept)


class TheReferrers(CounterTest):
    def test_a_referrer_is_kept_as_the_registered_name_of_the_site_only(self):
        for ref, kept in (("mail.google.com", "google.com"), ("google.com", "google.com"), ("www.google.com", "google.com"), ("alice.github.io", "github.io"), ("github.io", "github.io"), ("a.b.c.example.com", "example.com"),
                          ("www.shop.example.co.uk", "example.co.uk"), ("alice.co.uk", "alice.co.uk"), ("bob.blogspot.com.br", "blogspot.com.br"), ("alice.example.org.au", "example.org.au"),
                          ("some.unknown-ending.zz", "unknown-ending.zz"), ("co.uk", "co.uk"), ("t.co", "t.co")):
            with self.subTest(ref):
                self.assertEqual(events.host_of(ref, "chat.example.org"), kept)
        self.assertEqual(events.host_of("chat.example.org", "chat.example.org"), "", "this site itself is no referrer")
        self.assertEqual(events.host_of("example.org", "chat.example.org"), "example.org", "the front page of the same name is")

    def test_the_raw_event_has_the_registered_name_and_not_the_personal_address(self):
        self.send(view("/", r="alice-personal.github.io"))
        self.assertEqual(self.row()["ref"], "github.io")
        self.assertNotIn(b"alice-personal", file_bytes(self.path))

    def test_the_sums_name_a_site_only_when_three_visitors_came_from_it_that_day(self):
        self.visitors_from("often-site.com", 3)
        self.visitors_from("seldom-site.com", 2, start=10)
        self.send(view("/matches", r="often-site.com"), ip="9.1.0.1")           # (a second visit of a visitor counts once)
        self.clock.now += 3000
        self.send(view("/play", r="often-site.com"), ip="9.1.0.1")
        self.send(view("/", r="alone-site.com"), ip="9.200.0.1")
        s = self.report()
        self.assertEqual(s["lists"]["refs"], {"often-site.com": 4, "other": 3}, "the visits of the sites that fewer than three visitors came from are one `other`; none of them is lost")
        self.assertEqual(s["days"][0]["visits"], 7)
        kept = self.store.db.execute("SELECT COUNT(*) FROM events WHERE ref = 'seldom-site.com'").fetchone()[0]
        self.assertEqual(kept, 2, "the raw events, which go in 30 days, still have it")

    def test_a_visitor_who_comes_twice_from_a_site_is_one_visitor_of_it(self):
        self.send(view("/", r="twice-site.com"))
        self.clock.now += 4000
        self.send(view("/", r="twice-site.com"))
        self.visitors_from("twice-site.com", 1, start=5)
        self.assertEqual(self.report()["lists"]["refs"], {"other": 3})


class TheRangeLimit(CounterTest):
    """One address that changes its header at every request is a new visitor each time: a range of addresses may make only so many a day, so that it cannot fill the day's room for everybody."""

    def header(self, i):
        return UA_WINDOWS.replace("124.0.0.0", "124.0.0.%d" % i)

    def test_one_address_that_changes_its_header_makes_only_so_many_visitors(self):
        n = analytics.RANGE_NEW_MAX
        answers = [self.send(view(), ua=self.header(i)) for i in range(n + 10)]
        self.assertEqual(answers, ["ok"] * n + ["limited"] * 10)
        self.assertEqual(self.report()["days"][0]["visitors"], n)
        self.assertEqual(self.report()["counter"]["dropped_today"], 10, "what was dropped is counted")
        self.assertEqual(self.c.kept_today, n, "and does not take the room of the day")

    def test_a_visitor_that_is_already_counted_goes_on_and_another_range_is_not_held_back(self):
        n = analytics.RANGE_NEW_MAX
        for i in range(n):
            self.send(view(), ua=self.header(i))
        self.assertEqual(self.send({"t": "leave", "p": "/"}, ua=self.header(0)), "ok", "a browser that was counted before the limit")
        self.assertEqual(self.send(view(), ua=self.header(n + 1)), "limited")
        self.assertEqual(self.send(view(), ip=IP_AU, ua=self.header(n + 1)), "ok", "another range")

    def test_the_range_is_a_24_of_ipv4_and_a_48_of_ipv6(self):
        n = analytics.RANGE_NEW_MAX
        for i in range(n):
            self.send(view(), ip="8.8.8.%d" % (i % 250), ua=self.header(i))
        self.assertEqual(self.send(view(), ip="8.8.8.250", ua=self.header(900)), "limited", "another address of the same /24")
        self.assertEqual(self.send(view(), ip="8.8.9.1", ua=self.header(900)), "ok", "the next /24")
        for i in range(n):
            self.send(view(), ip="2001:db8:5:%x::1" % i, ua=self.header(i))
        self.assertEqual(self.send(view(), ip="2001:db8:5:ffff::1", ua=self.header(900)), "limited", "another /64 of the same /48")
        self.assertEqual(self.send(view(), ip="2001:db8:6::1", ua=self.header(900)), "ok", "the next /48")

    def test_a_browser_that_asked_not_to_be_tracked_counts_for_its_range_too(self):
        n = analytics.RANGE_NEW_MAX
        answers = [self.send({"t": "visit"}, ua=self.header(i), untracked=True) for i in range(n + 3)]
        self.assertEqual(answers, ["ok"] * n + ["limited"] * 3)
        self.assertEqual(self.send({"t": "visit"}, ua=self.header(0), untracked=True), "again", "one that was counted a moment ago is answered as before")

    def test_the_next_day_starts_again_and_nothing_of_the_range_is_written(self):
        n = analytics.RANGE_NEW_MAX
        for i in range(n + 1):
            self.send(view(), ip="1.0.0.9", ua=self.header(i))
        self.c.flush(force=True)
        data = file_bytes(self.path)
        for text in (b"1.0.0.", b"1.0.0.0/24"):
            self.assertNotIn(text, data)
        self.assertEqual(len(self.c.ranges), 1)
        self.clock.now += 86400
        self.c.on_tick()
        self.assertEqual(self.c.ranges, {})
        self.assertEqual(self.send(view(), ip="1.0.0.9"), "ok")

    def test_the_memory_of_the_ranges_never_holds_more_than_the_events_of_the_day(self):
        with mock.patch.object(analytics, "DAY_MAX", 20):
            for i in range(40):
                self.send(view(), ip="9.%d.%d.1" % (i // 250, i % 250))
            self.assertLessEqual(len(self.c.ranges), 20)


class Lines:
    """A log that keeps what it is told, for the tests of what the counter says."""

    def __init__(self):
        self.lines = []

    def event(self, name, **fields):
        self.lines.append((name, fields))


class TheVisitorsAddress(CounterTest):
    """The visitor's address is the one that nginx puts in X-Client-Address (the right-most address of the proxy's X-Forwarded-For that is not a proxy: docker/nginx.conf, docker/trusted-proxies.conf). When the
    proxy in front of the site passes none, nginx has only the proxy's own address, which is an internal one, and every visitor comes from it: that is no household, so the limit of one (RANGE_NEW_MAX) must not
    drop people, and the hub says so in its log (once an hour, never with the address)."""

    def header(self, i):
        return UA_WINDOWS.replace("124.0.0.0", "124.0.0.%d" % i)

    def test_two_visitors_of_the_same_browser_from_other_addresses_are_two_visitors(self):
        for ip in ("203.0.113.7", "198.51.100.4", "2001:db8:1::5", "2001:db8:2::5"):
            self.assertEqual(self.send(view(), ip=ip), "ok")
        self.assertEqual(self.send(view("/matches"), ip="203.0.113.7"), "ok")
        self.assertEqual(self.report()["days"][0]["visitors"], 4)
        self.assertEqual(len({r[0] for r in self.store.db.execute("SELECT vis FROM events")}), 4)

    def test_a_proxy_that_passes_no_visitor_address_is_not_one_household_that_has_only_so_many_visitors(self):
        n = analytics.RANGE_NEW_MAX
        answers = [self.send(view(), ip="172.18.0.5", ua=self.header(i)) for i in range(n + 10)]
        self.assertEqual(answers, ["ok"] * (n + 10), "the browsers of the whole site, from the one address of the proxy")
        self.assertEqual(self.report()["days"][0]["visitors"], n + 10)
        self.assertEqual(self.report()["counter"]["dropped_today"], 0)

    def test_the_address_of_a_household_is_still_limited_and_so_is_the_proxy_at_its_own_higher_number(self):
        n = analytics.RANGE_NEW_MAX
        for i in range(n):
            self.send(view(), ip="203.0.113.7", ua=self.header(i))
        self.assertEqual(self.send(view(), ip="203.0.113.7", ua=self.header(n)), "limited")
        with mock.patch.object(analytics, "SHARED_NEW_MAX", 20):
            answers = [self.send(view(), ip="10.1.2.3", ua=self.header(i)) for i in range(30)]
        self.assertEqual(answers, ["ok"] * 20 + ["limited"] * 10, "an internal address has a limit too: a flood must not take the room of the day")
        self.assertEqual(self.send({"t": "visit"}, ip="192.168.1.1", ua=self.header(900), untracked=True), "ok", "a browser that asked not to be tracked counts for its range as before")

    def test_every_kind_of_internal_address_is_one(self):
        n = analytics.RANGE_NEW_MAX
        for ip in ("127.0.0.1", "10.9.8.7", "172.16.0.1", "172.31.255.254", "192.168.50.50", "::1", "fd00::7", "::ffff:10.0.0.9"):
            with self.subTest(ip):
                self.c.ranges.clear()
                self.assertEqual([self.send(view(), ip=ip, ua=self.header(1000 + i)) for i in range(n + 1)], ["ok"] * (n + 1))
        for ip in ("172.32.0.1", "172.15.0.1", "8.8.8.8", "100.64.0.1", "192.169.0.1", "2001:db8::1", "fe80::1"):
            with self.subTest(ip):
                self.c.ranges.clear()
                self.assertEqual([self.send(view(), ip=ip, ua=self.header(2000 + i)) for i in range(n + 1)][-1], "limited")

    def test_the_log_says_once_an_hour_that_the_proxy_passes_no_address_and_names_none(self):
        log = self.c.log = Lines()
        self.send(view(), ip="172.18.0.5")
        self.send(view(), ip="172.18.0.5", ua=UA_LINUX)
        self.send({"t": "leave", "p": "/"}, ip="172.18.0.5")
        self.assertEqual([name for name, _ in log.lines], ["warning"], "once")
        self.assertEqual(list(log.lines[0][1]), ["what"])
        self.assertNotIn("172", str(log.lines))
        self.clock.now += 3599
        self.send(view(), ip="172.18.0.5", ua=UA_IPHONE)
        self.assertEqual(len(log.lines), 1, "not yet an hour")
        self.clock.now += 2
        self.send(view(), ip="172.18.0.5", ua=UA_EDGE)
        self.assertEqual(len(log.lines), 2, "an hour later it is said again, as long as it goes on")

    def test_a_visitor_from_a_public_address_makes_no_such_line(self):
        log = self.c.log = Lines()
        for ip in (IP_US, IP_AU, IP_DE, "203.0.113.7"):
            self.send(view(), ip=ip)
        self.send({"t": "visit"}, ip="198.51.100.9", untracked=True)
        self.assertEqual(log.lines, [])

    def test_the_internal_addresses_are_the_ranges_that_nginx_takes_for_a_proxy(self):
        """One list: docker/trusted-proxies.conf names where a reverse proxy connects from, and an event that comes from there as the VISITOR's address is the proxy's own."""
        with open(os.path.join(REPO, "docker", "trusted-proxies.conf"), encoding="utf-8") as f:
            listed = sorted(str(ipaddress.ip_network(m)) for m in re.findall(r"(?m)^set_real_ip_from (\S+);", f.read()))
        self.assertEqual(sorted(str(n) for n in limitmod.INTERNAL_NETWORKS), listed)
        for text, want in (("127.0.0.1", True), ("10.0.0.1", True), ("172.20.1.1", True), ("192.168.0.9", True), ("::1", True), ("fc00::1", True), ("::ffff:192.168.1.1", True),
                           ("8.8.8.8", False), ("172.32.0.1", False), ("2001:db8::1", False), ("", False), ("junk", False), (None, False)):
            with self.subTest(text):
                self.assertEqual(limitmod.is_internal(text), want)


class TheSizeOfTheFile(CounterTest):
    """The file is held to DB_MAX_BYTES as hub.db is: the oldest raw days go from 90% (their sums are made first), and at the limit new events are dropped."""

    def fill_days(self, days=6, per_day=250):
        for d in range(days):
            for i in range(per_day):
                self.send(view("/", r="x.example.com"), ip="9.%d.%d.1" % (d + 1, i))
                self.clock.now += 1
            self.clock.now = DAY + (d + 1) * 86400
            self.c.today()                                       # (the hour's work, which makes the sums of the finished days, has not been done: only the room that the limit needs may do it)

    def test_at_the_limit_new_events_are_dropped_and_counted_and_taken_again_when_there_is_room(self):
        self.send(view())
        with mock.patch.object(analytics, "DB_MAX_BYTES", 1):
            self.c.watch_size()
            self.assertEqual(self.send(view(), ip=IP_AU), "full")
            self.assertEqual(self.send({"t": "visit"}, ip=IP_DE), "full", "a not tracked visit too")
            self.assertEqual(self.report()["counter"]["dropped_today"], 2)
            self.assertEqual(len(self.rows()), 1)
        self.c.watch_size()
        self.assertEqual(self.send(view(), ip=IP_AU), "ok")

    def test_the_oldest_raw_days_go_first_with_their_sums_and_the_last_two_days_never(self):
        self.fill_days()
        last = self.day()
        first = events.add_days(last, -6)
        used = self.store.used_bytes()
        self.assertEqual(self.store.db.execute("SELECT MIN(day) FROM events").fetchone()[0], first)
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM days").fetchone()[0], 0, "no sums are kept yet")
        with mock.patch.object(analytics, "DB_MAX_BYTES", used):
            self.c.next_room = 0.0
            self.c.watch_size()
        oldest = self.store.oldest_event_day()
        self.assertGreater(oldest, first, "the first day's raw events went")
        self.assertLessEqual(oldest, events.add_days(last, -1), "yesterday's stayed")
        self.assertLessEqual(self.store.used_bytes(), used * 0.8 + 4096 * 3, "down to about 80%")
        gone = self.report(first, first)["days"][0]
        self.assertEqual(gone["visitors"], 250, "the sums of a day whose events went are kept")

    def test_the_counter_never_deletes_today_or_yesterday_for_room(self):
        self.fill_days(days=2, per_day=100)
        with mock.patch.object(analytics, "DB_MAX_BYTES", 4096):
            self.c.next_room = 0.0
            self.c.watch_size()
        self.assertEqual([r[0] for r in self.store.db.execute("SELECT DISTINCT day FROM events")], [self.day(-1)], "the first day went, the day before today stayed, whatever the limit")

    def test_a_flood_at_the_cap_costs_the_counter_a_few_tens_of_megabytes_of_memory_and_the_report_next_to_nothing(self):
        """The memory of the counter at every one of its caps (a full day of DAY_MAX events from as many visitors, the week's codes at WEEK_MAX, a range for each), measured with tracemalloc:
        17 MB (the hub's container has 192 MB; the caps before this, 100,000 events a day and 300,000 codes, took 36 MB, and the report made from a day's events took 65 MB more because it
        held every row; it reads them one at a time now)."""
        import tracemalloc
        tracemalloc.start()
        try:
            base = tracemalloc.get_traced_memory()[0]
            for i in range(analytics.DAY_MAX):
                self.c.accept("%d.%d.%d.1" % (1 + i // 65536 % 200, i // 256 % 256, i % 256), UA_WINDOWS, False, view("/", r="site%d.example.com" % (i % 500)))
            while self.c.week_size < analytics.WEEK_MAX:
                self.c.week[1].add(os.urandom(12))
                self.c.week_size += 1
            resident = tracemalloc.get_traced_memory()[0] - base
            self.assertEqual((self.c.kept_today, len(self.c.visitors)), (analytics.DAY_MAX, analytics.DAY_MAX), "every cap was reached")
            self.c.flush(force=True)
            before = tracemalloc.get_traced_memory()[0]
            tracemalloc.reset_peak()
            made = self.store.sums_now(self.c.day)
            rebuild = tracemalloc.get_traced_memory()[1] - before
        finally:
            tracemalloc.stop()
        self.assertEqual(made["visitors"], analytics.DAY_MAX)
        self.assertLess(resident, 25e6, "resident at the caps: %.1f MB" % (resident / 1e6))
        self.assertLess(rebuild, 3e6, "the report's rebuild of a full day: %.2f MB over what is resident" % (rebuild / 1e6))
        self.assertLess(self.store.size(), analytics.DB_MAX_BYTES // 4, "a full day is a small part of the file's limit")


# ---- the door ----------------------------------------------------------------------------------------------------------------------------

class DoorTest(unittest.TestCase):
    options = {}
    env = {}

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.geo = os.path.join(self.folder.name, "country.bin")
        with open(self.geo, "wb") as f:
            f.write(geodata.build(FIXTURE, "2026-11"))
        self.rig = hub_rig.Rig(env=dict({"ANTS_HUB_GEO_FILE": self.geo}, **self.env), tick_seconds=0.05, **self.options)
        self.rig.__enter__()
        self.addCleanup(self.rig.__exit__, None, None, None)
        self.clock = Clock()
        self.collector = self.rig.hub.analytics.collector
        self.collector.clock = self.clock
        self.collector.bucket.at = 0.0
        self.rig.call(self.collector.today)                 # (the day of the test's clock begins: the counters are those of one day)
        self.count = 0
        self.path = os.path.join(self.folder.name, "analytics.db")

    def post(self, body=None, ip=IP_US, ua=UA_WINDOWS, origin=ORIGIN, site="same-origin", ctype="text/plain;charset=UTF-8", extra=(), raw=None, path="/hub/a", method="POST", length=None, address=True):
        data = raw if raw is not None else json.dumps(body).encode()
        pairs = []
        if address:
            pairs.append(("X-Client-Address", ip))
        if origin is not None:
            pairs.append(("Origin", origin))
        if site is not None:
            pairs.append(("Sec-Fetch-Site", site))
        if ctype is not None:
            pairs.append(("Content-Type", ctype))
        if ua is not None:
            pairs.append(("User-Agent", ua))
        return hc.request(self.rig.port, method, path, pairs + list(extra), body=data, timeout=10.0, send_length=length is None) if length is None else \
            hc.request(self.rig.port, method, path, pairs + list(extra) + [("Content-Length", str(length))], body=data, timeout=10.0, send_length=False)

    def rows(self):
        return self.rig.call(lambda: self.collector.store.db.execute("SELECT kind, vis, country, browser, path FROM events ORDER BY id").fetchall())


class TheDoor(DoorTest):
    def test_an_event_of_the_sites_own_page_is_counted_and_answered_with_nothing(self):
        r = self.post(view("/matches"))
        self.assertEqual((r.status, r.body), (204, b""))
        rows = self.rows()
        self.assertEqual([(k, c, b, p) for k, _, c, b, p in rows], [("view", "US", "Chrome", "/matches")])

    def test_what_is_dropped_on_purpose_is_answered_like_what_is_kept(self):
        kept = self.post(view())
        crawler = self.post(view(), ua=UA_GOOGLEBOT)
        self.assertEqual((kept.status, kept.body, crawler.status, crawler.body), (204, b"", 204, b""))
        self.assertEqual(len(self.rows()), 1)

    def test_an_event_over_the_limit_of_the_day_is_answered_as_one_that_is_kept(self):
        with mock.patch.object(analytics, "DAY_MAX", 1):
            first, second = self.post(view(), ip="8.8.8.1"), self.post(view(), ip="8.8.8.2")
        self.assertEqual((first.status, first.body, second.status, second.body), (204, b"", 204, b""))
        self.assertEqual(len(self.rows()), 1)

    def test_a_browsers_harmless_note_is_answered_like_an_event_that_was_kept_and_is_not_kept(self):
        note = {"t": "error", "k": "Page", "n": "Error", "m": "ResizeObserver loop completed with undelivered notifications."}
        kept = self.post(view())
        dropped = self.post(note)
        self.assertEqual((kept.status, kept.body, dropped.status, dropped.body), (204, b"", 204, b""))
        self.assertEqual([r[0] for r in self.rows()], ["view"])

    def test_the_header_of_a_browser_that_asked_not_to_be_tracked_makes_the_minimal_visit(self):
        for header in (("DNT", "1"), ("Sec-GPC", "1")):
            with self.subTest(header):
                self.rig.call(self.collector.nt_seen.clear)
                before = len(self.rows())
                self.assertEqual(self.post(view("/play", r="discord.com"), ip=IP_AU, extra=[header]).status, 204)
                self.assertEqual(self.rows()[before:], [("visit", "", "AU", "", "")])

    def test_dnt_zero_and_gpc_zero_do_not_ask(self):
        self.assertEqual(self.post(view("/play"), extra=[("DNT", "0"), ("Sec-GPC", "0")]).status, 204)
        self.assertEqual(self.rows()[0][0], "view")

    def test_the_address_for_the_country_is_the_visitors_whole_address(self):
        self.assertEqual(self.post(view(), ip=IP_DE).status, 204)
        self.assertEqual(self.post(view(), ip="::ffff:1.0.0.9").status, 204)
        self.assertEqual([r[2] for r in self.rows()], ["DE", "AU"])

    def test_nothing_of_the_request_is_in_the_log_or_the_file(self):
        self.post(view("/matches", r="discord.com"), ip=IP_AU, ua=UA_IPHONE)
        self.post(view(), ip=IP_AU, ua="curl/8")
        self.post(raw=b"{broken", ip=IP_AU)
        log = self.rig.log_text.getvalue()
        data = file_bytes(self.path)
        for secret in ("1.0.0.7", "iPhone", "Mozilla", "curl", "broken"):
            self.assertNotIn(secret, log, secret)
            self.assertNotIn(secret.encode(), data, secret)

    def test_only_post(self):
        for method in ("GET", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH"):
            with self.subTest(method):
                r = self.post(view(), method=method)
                self.assertEqual((r.status, r.get("allow")), (405, "POST"))
        self.assertEqual(self.rows(), [])

    def test_only_this_path_and_no_query(self):
        for path in ("/hub/a?x=1", "/hub/a?", "/hub/a/", "/hub/ab", "/hub/", "/hub/analytics"):
            with self.subTest(path):
                self.assertEqual(self.post(view(), path=path).status, 404)
        self.assertEqual(self.rows(), [])

    def test_only_the_sites_own_pages(self):
        for kind, kwargs in (("another origin", {"origin": "https://evil.example"}), ("no origin", {"origin": None}), ("null origin", {"origin": "null"}), ("same-site", {"site": "same-site"}),
                             ("cross-site", {"site": "cross-site"}), ("no fetch metadata", {"site": None}), ("none", {"site": "none"})):
            with self.subTest(kind):
                self.assertEqual(self.post(view(), **kwargs).status, 403)
        self.assertEqual(self.rows(), [])

    def test_the_type_of_the_body(self):
        for ctype, status in (("text/plain", 204), ("text/plain;charset=UTF-8", 204), ("application/json", 204), ("Application/JSON; charset=utf-8", 204),
                              ("text/html", 415), ("application/x-www-form-urlencoded", 415), ("multipart/form-data", 415), (None, 415)):
            with self.subTest(ctype):
                self.rig.call(self.collector.nt_seen.clear)
                self.assertEqual(self.post(view(), ctype=ctype, ip="8.8.8.%d" % (hash(str(ctype)) % 200 + 1)).status, status)

    def test_the_size_of_the_body(self):
        small = json.dumps(view()).encode()
        self.assertEqual(self.post(raw=small).status, 204)
        self.assertEqual(self.post(raw=b" " * 1024 + small[:0] + b"{}").status, 413, "over 1,024 bytes: the length is looked at before the body is read")
        self.assertEqual(self.post(raw=b"").status, 400, "a POST with no body")
        self.assertEqual(self.post(raw=b"x", length=0).status, 400, "a length of 0")
        self.assertEqual(self.post(raw=small, length=999999).status, 413)

    def test_a_body_that_is_not_one_json_object_of_an_event(self):
        for raw, status in ((b"not json", 400), (b"[]", 400), (b'{"t":"view","t":"view","p":"/"}', 400), (b'{"t":"view","p":NaN}', 400), (b"\xff\xfe", 400), (b'"view"', 400), (b"{}", 400),
                            (b'{"t":"click","p":"/"}', 400), (b'{"t":"view"}', 400), (b'{"t":"view","p":"/","x":1}', 400), (b'{"t":"view","p":"/"}', 204)):
            with self.subTest(raw):
                self.assertEqual(self.post(raw=raw).status, status)

    def test_a_promised_body_that_never_comes_is_given_up(self):
        import socket
        with socket.create_connection(("127.0.0.1", self.rig.port), timeout=10) as s:
            s.sendall(b"POST /hub/a HTTP/1.1\r\nHost: x\r\nX-Client-Address: 8.8.8.8\r\nOrigin: %s\r\nSec-Fetch-Site: same-origin\r\nContent-Type: text/plain\r\nContent-Length: 50\r\n\r\n{" % ORIGIN.encode())
            s.shutdown(socket.SHUT_WR)
            self.assertIn(b" 400 ", s.recv(4096).split(b"\r\n")[0])

    def test_no_address_is_a_400(self):
        self.assertEqual(self.post(view(), address=False).status, 400)
        self.assertIn("analytics_asked_without_x_client_address", self.rig.log_text.getvalue())

    def test_two_visitors_from_other_addresses_behind_the_proxy_are_two_visitors(self):
        """The door counts by the address that nginx puts in X-Client-Address (the visitor's, taken from the proxy's X-Forwarded-For): the connection's own address is the proxy's for everybody, as it is here (127.0.0.1)."""
        for ip in ("203.0.113.7", "198.51.100.4", "2001:db8::5"):
            self.assertEqual(self.post(view(), ip=ip).status, 204)
        self.assertEqual(self.post(view("/matches"), ip="203.0.113.7").status, 204)
        rows = self.rows()
        self.assertEqual(len({r[1] for r in rows}), 3)
        self.assertEqual(rows[0][1], rows[3][1], "the same address and browser again is the same visitor")
        day = events.pacific_day(self.clock.now)
        self.assertEqual(self.rig.call(lambda: self.collector.report(day, day))["days"][0]["visitors"], 3)

    def test_events_from_an_internal_address_are_counted_and_the_log_says_so_without_the_address(self):
        for i in range(3):
            self.assertEqual(self.post(view(), ip="172.18.0.5", ua=UA_WINDOWS.replace("124.0.0.0", "124.0.0.%d" % i)).status, 204)
        self.assertEqual(len(self.rows()), 3)
        log = self.rig.log_text.getvalue()
        self.assertEqual(log.count("analytics_events_arrive_from_an_internal_address"), 1)
        self.assertNotIn("172.18", log)

    def test_an_address_that_comes_too_often_is_refused_and_others_are_not(self):
        statuses = [self.post(view("/"), ip="8.8.4.4").status for _ in range(60)]
        self.assertEqual(statuses[:40], [204] * 40 if statuses[0] == 204 else statuses[:40])
        self.assertIn(429, statuses)
        self.assertEqual(self.post(view("/"), ip="8.8.4.5").status, 204)

    def test_the_whole_hub_has_a_limit_too(self):
        self.collector.bucket.tokens = 1.0
        self.collector.bucket.at = self.clock.now
        self.collector.bucket.refill = 10 ** 9
        self.assertEqual(self.post(view(), ip="8.8.8.1").status, 204)
        r = self.post(view(), ip="8.8.8.2")
        self.assertEqual((r.status, r.get("retry-after")), (429, "5"))

    def test_refused_events_are_counted(self):
        self.post(raw=b"nonsense")
        self.post(view(), origin="https://evil.example")
        self.post(view(), ctype="text/html")
        self.post({"t": "nope"})
        state = self.rig.call(lambda: self.collector.counter_state(self.collector.today()))
        self.assertEqual(state["refused_today"], 4)

    def test_a_bug_costs_a_request_and_never_the_hub(self):
        def broken(*args):
            raise ValueError("an address 8.8.8.8 in a message")
        self.collector.accept = broken
        self.assertEqual(self.post(view()).status, 500)
        self.assertNotIn("8.8.8.8", self.rig.log_text.getvalue())
        self.assertIn("ValueError", self.rig.log_text.getvalue())
        del self.collector.accept
        self.assertEqual(self.post(view(), ip="8.8.8.9").status, 204)

    def test_the_counters_are_written_by_the_hubs_clock(self):
        import time
        self.collector.clock = time.time                    # (the real clock: the hub's own tick is what writes the counters, once a second)
        self.collector.next_flush = 0.0
        self.rig.call(self.collector.today)
        self.post(view())
        end = time.time() + 5
        while time.time() < end:
            if self.rig.call(lambda: self.collector.store.counter(self.collector.day, "accepted")):
                break
            time.sleep(0.05)
        self.assertEqual(self.rig.call(lambda: self.collector.store.counter(self.collector.day, "accepted")), 1)

    def test_the_answer_of_a_refusal_holds_a_word_and_never_the_text_sent(self):
        r = self.post(raw=b'{"t":"view","p":"/secret-page-name","x":"needle-text"}')
        self.assertEqual(r.status, 400)
        self.assertNotIn(b"needle", r.body)
        self.assertNotIn(b"secret-page-name", r.body)
        self.assertEqual(r.json(), {"error": "shape"})


class TheAdminQuestion(base.DoorTest):
    """The Analytics tab's question to the admin door: POST /hub/admin/api/analytics {"from", "to"} (the same fences as a change, and it changes nothing)."""

    def setUp(self):
        super().setUp()
        self.collector = self.rig.hub.analytics.collector
        self.today = self.rig.call(self.collector.today)

    def send(self, body, ip=IP_US, ua=UA_WINDOWS, untracked=False):
        return self.rig.call(self.collector.accept, ip, ua, untracked, body)

    def ask(self, first=None, last=None, account=base.ADMIN, **kw):
        return self.post("analytics", {"from": first or self.today, "to": last or self.today}, account, **kw)

    def test_who_may_ask(self):
        want = {base.OWNER: 200, base.ADMIN: 200, base.MOD: 403, base.PLAIN: 403, base.AWAY: 403, None: 401}
        for account, status in want.items():
            with self.subTest(base.NAMES.get(account, "nobody")):
                self.assertEqual(self.ask(account=account).status, status)

    def test_the_fences_of_a_change_are_the_questions_too(self):
        self.assertEqual(self.ask(csrf="0" * 64).status, 403)
        self.assertEqual(self.ask(csrf=None).status, 403)
        self.assertEqual(self.ask(ctype="text/plain").status, 415)
        self.assertEqual(self.ask(origin="https://evil.example").status, 403)
        self.assertEqual(self.ask(site="cross-site").status, 403)
        self.assertEqual(self.get("analytics").status, 405)
        self.assertEqual(self.call("POST", "/hub/admin/api/analytics?x=1", base.ADMIN, body={"from": self.today, "to": self.today}).status, 404)

    def test_the_answer_for_a_day(self):
        self.send(view("/", r="discord.com"))
        self.send({"t": "visit"}, ip=IP_AU)
        self.send({"t": "perf", "n": "load", "ms": 900, "v": "0.22.0"})
        self.send({"t": "error", "k": "Page", "n": "TypeError", "m": "x is null", "s": "front/a.js:3"})
        r = self.ask()
        self.assertEqual(r.status, 200)
        body = r.json()
        day = body["days"][0]
        self.assertEqual((day["day"], day["visitors"], day["visits"], day["nt"], day["errors"]), (self.today, 1, 1, 1, 1))
        self.assertEqual(body["lists"]["refs"], {"other": 1}, "a site that fewer than three visitors came from is not named")
        self.assertEqual(body["errors"][0]["msg"], "TypeError: x is null")
        self.assertEqual((body["from"], body["to"], body["today"]), (self.today, self.today, self.today))
        self.assertEqual(body["counter"]["accepted_today"], 4)
        self.assertTrue(body["untrusted"])
        self.assertEqual(body["untrusted_fields"], ["lists.refs", "errors"])

    def test_the_answer_has_no_code_no_address_and_no_header(self):
        self.send(view("/matches", r="discord.com"), ip="1.0.0.55", ua=UA_IPHONE)
        code = self.rig.call(lambda: self.collector.store.db.execute("SELECT vis FROM events").fetchone()[0])
        text = self.ask().text()
        for secret in (code, "1.0.0.55", "iPhone", "Mozilla"):
            self.assertNotIn(secret, text)

    def test_the_range_is_checked(self):
        for first, last in (("2099-01-02", "2099-01-01"), ("nonsense", self.today), (self.today, "2099-01-01"), (events.add_days(self.today, -400), self.today), (5, self.today), (None, None)):
            with self.subTest((first, last)):
                self.assertEqual(self.post("analytics", {"from": first, "to": last}).json(), {"error": "range"})
        for body in ({}, {"from": self.today}, {"from": self.today, "to": self.today, "x": 1}, [], "x", 5):
            with self.subTest(body):
                self.assertEqual(self.post("analytics", body).json(), {"error": "bad"})
        self.assertEqual(self.ask(raw=b"{broken", ctype="application/json").json(), {"error": "bad"})
        self.assertEqual(self.ask(raw=b'{"from":"2026-11-29","from":"2026-11-29","to":"2026-11-29"}').json(), {"error": "bad"})

    def test_it_asks_nothing_of_discord_that_a_page_would_not(self):
        asked = len(self.fake.member_ids)
        self.assertEqual(self.ask(account=base.OWNER).status, 200)
        self.assertEqual(len(self.fake.member_ids), asked, "the operator's account needs no call to Discord")

    def test_a_hub_without_the_counter_says_so(self):
        self.rig.hub.analytics = analyticsdoor.Off()
        r = self.ask()
        self.assertEqual((r.status, r.json()), (503, {"error": "analytics_off"}))


class TheSignups(unittest.TestCase):
    """A person who logs in with Discord for the first time is one more sign-up of the day: a number, with no name (the callback of hub/login.py tells the counter)."""

    def setUp(self):
        self.fake = fd.FakeDiscord(hc.SITE + "/hub/callback", user=(base.BOSS, "maple.leaf"))
        self.fake.consent = False
        self.addCleanup(self.fake.close)
        self.rig = hub_rig.Rig(discord=self.fake, member_fake=self.fake, **dict(base.OPTIONS, sockets_per_address=100))
        self.rig.__enter__()
        self.addCleanup(self.rig.__exit__, None, None, None)
        self.count = 0
        self.collector = self.rig.hub.analytics.collector

    def login(self, user):
        self.fake.user = user
        self.count += 1
        r = Visitor(self.rig.port, self.fake, address="198.51.100.%d" % self.count).login("")
        self.assertEqual(r.status, 302)

    def signups(self):
        return self.rig.call(lambda: self.collector.store.counter(self.collector.today(), "signups"))

    def test_the_first_login_of_an_account_counts_and_the_next_ones_do_not(self):
        self.assertEqual(self.signups(), 0)
        self.login((base.BOSS, "maple.leaf"))
        self.assertEqual(self.signups(), 1)
        self.login((base.BOSS, "maple.leaf"))
        self.assertEqual(self.signups(), 1, "the same account again")
        self.login((base.OTHER, "oak.tree"))
        self.assertEqual(self.signups(), 2)

    def test_what_is_counted_is_a_number_and_nothing_of_the_person(self):
        self.login((base.BOSS, "maple.leaf"))
        data = file_bytes(os.path.join(os.path.dirname(self.rig.db), "analytics.db"))
        for secret in (b"maple.leaf", base.BOSS.encode()):
            self.assertNotIn(secret, data)


class TheSwitch(unittest.TestCase):
    def test_off_the_door_is_a_503_and_nothing_is_kept_and_no_file_is_made(self):
        with hub_rig.Rig(env={"ANTS_HUB_ANALYTICS": "off"}) as rig:
            r = hc.request(rig.port, "POST", "/hub/a", [("X-Client-Address", IP_US), ("Origin", ORIGIN), ("Sec-Fetch-Site", "same-origin"), ("Content-Type", "text/plain")], body=json.dumps(view()).encode())
            self.assertEqual((r.status, r.json()), (503, {"error": "off"}))
            self.assertFalse(os.path.exists(os.path.join(os.path.dirname(rig.db), "analytics.db")))

    def test_the_setting_is_on_or_off_and_nothing_else(self):
        self.assertTrue(configmod.load({}).analytics)
        self.assertTrue(configmod.load({"ANTS_HUB_ANALYTICS": " ON "}).analytics)
        self.assertFalse(configmod.load({"ANTS_HUB_ANALYTICS": "off"}).analytics)
        with self.assertRaises(configmod.ConfigError):
            configmod.load({"ANTS_HUB_ANALYTICS": "yes"})

    def test_a_file_that_cannot_be_opened_does_not_stop_the_hub(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        with hub_rig.Rig(env={"ANTS_HUB_ANALYTICS_DB": os.path.join(folder.name, "missing", "a.db")}) as rig:
            r = hc.request(rig.port, "POST", "/hub/a", [("X-Client-Address", IP_US), ("Origin", ORIGIN), ("Sec-Fetch-Site", "same-origin"), ("Content-Type", "text/plain")], body=json.dumps(view()).encode())
            self.assertEqual(r.status, 503)
            self.assertIn("analytics_off_its_file_cannot_be_opened", rig.log_text.getvalue())
            self.assertEqual(hc.request(rig.port, "GET", "/hub/healthz").status, 200)

    def test_the_files_are_named_by_the_settings_and_by_default_next_to_the_chats_file(self):
        self.assertEqual(configmod.load({"ANTS_HUB_DB": "/data/hub.db"}).analytics_db, "/data/analytics.db")
        self.assertEqual(configmod.load({"ANTS_HUB_ANALYTICS_DB": "/x/y.db"}).analytics_db, "/x/y.db")
        self.assertEqual(configmod.load({"ANTS_HUB_GEO_FILE": "/g.bin"}).geo_file, "/g.bin")
        self.assertTrue(configmod.load({}).geo_file.endswith(os.path.join("geo", "country.bin")))


if __name__ == "__main__":
    unittest.main()
