#!/usr/bin/env python3
"""The stats sections of the match page (run by ./run_tests.sh --fast and by the CI): web/match_stats.js and web/match_stats.css, and how web/match.html uses them. docs/REPLAYS.md "The match pages" says what
they show and where `details` comes from (the replay pass, ants_replay/match_stats.hpp and ants_server/history_details.hpp).

  - the code is RUN (node, when it is installed): the band of highlights, "Over time" and "Activity" built from a record's `details` on a small fake of the DOM, the plain functions on tables, a record
    with no stats, with the marker of a pass that failed and with damaged details (tests/scripts/web_match_stats_check.js), and again on broken copies of the file, which the check must refuse;
  - what needs no browser is read from the files: that nothing a record carries becomes markup, that a section is one entry of the page's list, that the stylesheet and the script are linked after the
    shared ones, and that a section that is not on the page is not linked from the band;
  - tests/scripts/test_web_matches.sh (opt-in) shows the same in a real browser against a fake door.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_match_stats_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_check(stats_path):
    return subprocess.run([NODE, CHECK_JS, stats_path, os.path.join(REPO, "web", "history_page.js")], capture_output=True, text=True, timeout=120)


@unittest.skipUnless(NODE, "node is not installed: the stats sections were NOT run (tests/scripts/web_match_stats_check.js)")
class TheSections(unittest.TestCase):
    def test_the_band_over_time_and_activity_are_built_from_the_details_of_a_record(self):
        done = run_check(os.path.join(REPO, "web", "match_stats.js"))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"^ok: \d+ checks")

    MUTANTS = {
        "the clock counts minutes of 30 seconds": ("Math.floor(s / 60) + ':'", "Math.floor(s / 30) + ':'"),
        "the clock has no zero before the seconds": ("(s % 60 < 10 ? '0' : '')", "''"),
        "a seat can be number 4": ("x >= 0 && x <= 3 && x === Math.floor(x)", "x >= 0 && x <= 4 && x === Math.floor(x)"),
        "a seat can be a fraction": ("x >= 0 && x <= 3 && x === Math.floor(x)", "x >= 0 && x <= 3"),
        "a number that is not finite is a number": ("typeof x === 'number' && isFinite(x) ? x : 0", "typeof x === 'number' ? x : 0"),
        "a text is a list": ("function list(x) { return Array.isArray(x) ? x : []; }", "function list(x) { return x || []; }"),
        "an array is an object": ("function obj(x) { return x && typeof x === 'object' && !Array.isArray(x) ? x : {}; }", "function obj(x) { return x || {}; }"),
        "a small comeback is a chip": ("if (num(cb.behind) >= 100 && seatOf(cb.seat) >= 0) chips.push", "if (num(cb.behind) >= 0 && seatOf(cb.seat) >= 0) chips.push"),
        "the band has no chip for a quiet match": ("else if (noAntLost(ctx, d.combat)) chips.push", "else if (false) chips.push"),
        "ants lost on the scoreboard do not matter to the quiet chip": ("return typeof l === 'number' && l === 0;", "return true;"),
        "a scoreboard that does not say how many were lost is quiet": ("return typeof l === 'number' && l === 0;", "return !(l > 0);"),
        "a list of kills does not matter to the quiet chip": (" || list(c.ev).length > 0) return false;", ") return false;"),
        "the strip of who led has no end": ("list(obj(d.highlights).lead).slice(0, MAX_LEAD).forEach(", "list(obj(d.highlights).lead).forEach("),
        "the rows of wiped out have no end": ("list(hl.wiped_out).slice(0, MAX_ROWS).forEach(", "list(hl.wiped_out).forEach("),
        "a coordinate of a chart is not bounded": ("function px(v) { return v < -100000 ? -100000 : v > 100000 ? 100000 : v; }", "function px(v) { return v; }"),
        "a length of a match is not bounded": ("return Math.min(MAX_TURNS, Math.max(1, n > 0 ? n : fallback));", "return Math.max(1, n > 0 ? n : fallback);"),
        "the lines of the minutes of a chart have no end": ("Math.max(total / 1200 > 18 ? 5 : total / 1200 > 9 ? 2 : 1, Math.ceil(total / 1200 / MAX_GRID))", "total / 1200 > 18 ? 5 : total / 1200 > 9 ? 2 : 1"),
        "the note of the orders is gone": (" Orders that name no ant of the seat are counted too.", ""),
        "the chips link to the wrong section": ("chips.push({ href: '#activity', label: 'Busiest'", "chips.push({ href: '#overtime', label: 'Busiest'"),
        "the lead counts the first leader as a change": ("var parts = [lead.length > 1 ? 'Changed hands ' + plural(lead.length - 1, 'time', 'times')", "var parts = [lead.length > 1 ? 'Changed hands ' + plural(lead.length, 'time', 'times')"),
        "the lead is not followed by its end": ("parts.push(i + 1 < lead.length ? ' until ' + clock(lead[i + 1].t) : ' to the end');", "parts.push(' to the end');"),
        "a one-sentence lead does not name the ally": ("parts: [side(lead[0].seat, lead[0].ally), ' led from '", "parts: [side(lead[0].seat, -1), ' led from '"),
        "the ink of a cell is always light": ("color: lum(m) > 0.3 ? '#06120d' : '#f4ece2'", "color: '#f4ece2'"),
        "a cell of the strongest colour is not the seat colour": ("BASE[i] + (v - BASE[i]) * a", "BASE[i] + (v - BASE[i]) * a * 0.5"),
        "a step of a chart is not round": ("return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * p;", "return m * p;"),
        "the top of a chart is below its largest value": ("Math.ceil(Math.max(maxV, o.ymax || 0) / tstep) * tstep", "Math.floor(Math.max(maxV, o.ymax || 0) / tstep) * tstep"),
        "a point of a line is at its start": ("var px = X((k + 1) * step).toFixed(1)", "var px = X(k * step).toFixed(1)"),
        "first blood is not marked": ("list(o.flags).forEach(function (f) {", "[].forEach(function (f) {"),
        "the seats have the same shape": ("SHAPES = ['circle', 'square', 'triangle', 'diamond']", "SHAPES = ['circle', 'circle', 'circle', 'circle']"),
        "a chart is drawn again at the same width": ("if (w === last) return;", ""),
        "a chart can be narrower than 280": ("Math.max(280, Math.floor(box.clientWidth || 600))", "Math.max(1, Math.floor(box.clientWidth || 600))"),
        "the strip of who led ends nowhere": ("t1 = i + 1 < lead.length ? lead[i + 1].t : turns", "t1 = i + 1 < lead.length ? lead[i + 1].t : 0"),
        "a side of two is one tone": ("seg = el('i', e.ally >= 0 ? 'two' : '')", "seg = el('i', '')"),
        "mode 187 shows a score": ("var series = ot && (ctx.mode187 ? list(ot.kills) : list(ot.score));", "var series = ot && list(ot.score);"),
        "a match with no kill has a chart of kills": ("if (!ctx.played.some(function (p) { return sum(kills[p]) > 0; })) s.appendChild(el('p', 'quiet'", "if (false) s.appendChild(el('p', 'quiet'"),
        "the minutes of a strip are not bounded": ("Math.min(MAX_MINUTES, list(pm[p]).length)", "list(pm[p]).length"),
        "every seat's busiest minute is not ringed": ("(v === rowMax && v > 0 ? ' top' : '')", "''"),
        "a minute with no order is painted": ("if (v) tint(c, p, 0.16 + 0.84 * v / gmax);", "tint(c, p, 0.16 + 0.84 * v / gmax);"),
        "the heat is not the same scale for every seat": ("tint(c, p, 0.16 + 0.84 * v / gmax)", "tint(c, p, 0.16 + 0.84 * v / rowMax)"),
        "the table has no column for raids": ("['raid', 'Raids']];", "];"),
        "the busiest seat is the last one": ("if (!best || n > best.n) best = { seat: p, n: n };", "best = { seat: p, n: n };"),
        "the busiest minute is the first": ("if (all[m] > all[best]) best = m;", ""),
        "a record with no keys has stats": ("return !!d && (obj(d.over_time).kills !== undefined || obj(d.activity).per_minute !== undefined);", "return !!d;"),
        "a marker is a record that was not counted yet": ("if (H.detailsOf(rec)) return what + 'the recording, and this one could not be counted. Only the totals above were kept.';", ""),
        "an older version is not named": ("(rec && typeof rec.game === 'string' && rec.game ? rec.game : 'unknown')", "'unknown'"),
        "a removed recording is told as not ready": ("if (st.kind === 'removed') return what + 'the recording. This one was removed before they were, so only the totals above were kept.';", ""),
        "a name is markup": ("w.appendChild(doc.createTextNode(nameText(ctx, seat))); return w;", "w.innerHTML = nameText(ctx, seat); return w;"),
    }

    def test_the_check_can_fail(self):
        text = read("web", "match_stats.js")
        with tempfile.TemporaryDirectory() as folder:
            for why, (old, new) in self.MUTANTS.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "match_stats.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                done = run_check(path)
                self.assertNotEqual(done.returncode, 0, "the check accepted a script where " + why + "\n" + done.stdout[-400:])


class TheFiles(unittest.TestCase):
    page = read("web", "match.html")
    script = read("web", "match_stats.js")
    css = read("web", "match_stats.css")

    def test_the_stylesheet_and_the_script_come_after_the_shared_ones_and_load_nothing_from_elsewhere(self):
        self.assertIn('<link rel="stylesheet" href="matches.css">\n    <link rel="stylesheet" href="match_stats.css">', self.page)
        self.assertIn('<script src="history_page.js"></script>\n<script src="match_stats.js"></script>\n<script>', self.page)       # (the shared helpers, then the sections, then the page's own script)
        self.assertNotRegex(self.css, r"url\((?:https?:)?//")
        self.assertNotIn("@import", self.css)
        self.assertEqual(set(re.findall(r"https?://[^'\" ]+", self.script)) - {"http://www.w3.org/2000/svg"}, set())          # (the SVG namespace is a name, not an address that is asked)

    def test_nothing_that_a_record_carries_becomes_markup(self):
        for bad in (".innerHTML", "document.write", "insertAdjacentHTML", "outerHTML", "eval(", "new Function"):
            self.assertNotIn(bad, self.script, bad)
        self.assertNotIn("setAttribute('style'", self.script)

    def test_the_sections_are_entries_of_the_pages_list_and_a_match_without_stats_says_why(self):
        self.assertIn("var SECTIONS = [resultSection, replaySection, stats.summary, boardSection, stats.overtime, stats.activity, moreSection, aboutSection];", self.page)
        self.assertIn("window.AntsMatchStats.noStatsNote(rec)", self.page)
        self.assertIn("window.AntsMatchStats.redraw(document)", self.page)
        self.assertEqual(self.page.count("addEventListener('resize'"), 1)                                       # (the charts are drawn again for a new width)
        self.assertIn("panel('more', 'more-h', 'More stats')", self.page)

    def test_the_band_links_only_to_sections_that_are_on_the_page(self):
        for target in set(re.findall(r"href: '#([a-z]+)'", self.script)):
            self.assertIn("panel('%s'," % target, self.script, "a chip links to #" + target + ", which no section makes")

    def test_the_older_version_note_says_totals_not_stats(self):
        helpers = read("web", "history_page.js")
        self.assertIn("The totals below are complete.", helpers)
        self.assertNotIn("The stats below are complete.", helpers)

    def test_the_seat_colours_are_the_scoreboards(self):
        css = read("web", "matches.css")
        for token in ("--c-green", "--c-red", "--c-blue", "--c-black"):
            self.assertIn(token, css)
        self.assertEqual(len(re.findall(r"#79bf93|#d4707f|#8f8fd6|#b3afb9", self.css)), 0)                       # (one home for the colours: matches.css)


class TheDocument(unittest.TestCase):
    def test_the_replays_page_describes_the_stats_the_details_and_the_pass(self):
        text = read("docs", "REPLAYS.md")
        for needle in ("web/match_stats.js", "details", "ants_replay/match_stats", "ants_server/history_details", "Over time", "Activity", "kDetailsMaxBytes", "after_tick"):
            self.assertIn(needle, text, needle)
        server = read("docs", "SERVER.md")
        for needle in ("details", "backfill", "the stats"):
            self.assertIn(needle, server, needle)


if __name__ == "__main__":
    unittest.main()
