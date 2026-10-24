#!/usr/bin/env python3
"""The map "Where ants died" (run by ./run_tests.sh --fast and by the CI): web/death_map.js and web/death_map.css, and how the match page and the replay page use them. docs/REPLAYS.md "The map of deaths" says what
it shows and where the field comes from (the replay pass, ants_replay/match_stats.hpp, ants_server/history_deaths.hpp).

  - the code is RUN (node, when it is installed): the field as the server writes it (one text is made by the C++ program), a field that is damaged, the glow, the dots, the squares of a big match, the chips, the
    switch of the ants that drowned, the marks, the zoom, the tooltip, the sum of all the matches of a map, a name that is text, and a match with no map, on a STRICT fake of the DOM
    (tests/scripts/web_death_map_check.js), and again on broken copies of the file, which the check must refuse;
  - what needs no browser is read from the files: that nothing a record carries becomes markup, that the card is one entry of the match page's list right after "Over time", that the replay page's panel gets the small
    card, that the stylesheet and the script are linked, copied into the image and part of the build, and that the colors of the teams are the page's;
  - tests/scripts/test_web_death_map.sh (opt-in) shows the same in a real browser against a fake door.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_death_map_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_check(map_path, history_path=None):
    return subprocess.run([NODE, CHECK_JS, map_path, history_path or os.path.join(REPO, "web", "history_page.js")], capture_output=True, text=True, timeout=120)


@unittest.skipUnless(NODE, "node is not installed: the map of deaths was NOT run (tests/scripts/web_death_map_check.js)")
class TheCards(unittest.TestCase):
    def test_the_cards_are_built_from_the_field_of_a_record(self):
        done = run_check(os.path.join(REPO, "web", "death_map.js"))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"^ok: \d+ checks")

    MUTANTS = {
        # reading the field
        "a letter that is not one of the 64 is a letter": ("if (k < 0 || text.charAt(at + i) === '') return -1;", "if (text.charAt(at + i) === '') return -1;"),
        "a type of 6 is an ant": ("if (type > 5 || (killer > 3 && killer !== 7)) return null;", "if (killer > 3 && killer !== 7) return null;"),
        "a team to blame of 5 is one": ("if (type > 5 || (killer > 3 && killer !== 7)) return null;", "if (type > 5) return null;"),
        "an ant is not put on the map in x": ("return { x: Math.min(x, w - 1), y: Math.min(y, h - 1),", "return { x: x, y: Math.min(y, h - 1),"),
        "an ant is not put on the map in y": ("return { x: Math.min(x, w - 1), y: Math.min(y, h - 1),", "return { x: Math.min(x, w - 1), y: y,"),
        "a square is not put on the map": ("m.cells.push({ x: Math.min(x, cx), y: Math.min(y, cy),", "m.cells.push({ x: x, y: y,"),
        "another version is read": ("if (d.v !== 1 || !whole(d.w, 1, MAX_SIDE)", "if (!whole(d.w, 1, MAX_SIDE)"),
        "a map of any size is read": ("!whole(d.w, 1, MAX_SIDE)", "!whole(d.w, 1, 1e9)"),
        "a count of 0 is a square": ("if (x < 0 || y < 0 || f < 0 || f > 7 || c < 1) return null;", "if (x < 0 || y < 0 || f < 0 || f > 7 || c < 0) return null;"),
        "a flag above 7 is a square": ("if (x < 0 || y < 0 || f < 0 || f > 7 || c < 1) return null;", "if (x < 0 || y < 0 || f < 0 || c < 1) return null;"),
        "any number of ants is read": ("if (text.length % 9 !== 0 || text.length > 9 * MAX_ANTS) return null;", "if (text.length % 9 !== 0) return null;"),
        "an unknown kind is read": ("} else return null;\n        if (m.total < 1) return null;", "} else m.total = 1;\n        if (m.total < 1) return null;"),
        "the squares are not summed": ("m.total += c;", "m.total += 1;"),
        "the first blood is the first ant that died": ("if (a.killer >= 0) { if (!m.first) m.first = a; m.last = a; }", "{ if (!m.first) m.first = a; m.last = a; }"),
        "the first blood of squares may have nobody to blame": ("if (m.first && m.first.killer < 0) m.first = null;", ""),
        "a header that says less than the text holds is believed": ("if (m.of < m.total) m.of = m.total;", ""),
        "a name of a map is not cleaned": ("map: clean(d.map, 64) }", "map: String(d.map || '') }"),
        "a text is not cut": (".slice(0, max || 40) : ''; }", ": ''; }"),
        "a right-to-left mark is kept": ("\\u2028-\\u202e", "\\u2028-\\u202d"),
        "a zero-width space is kept": ("\\u200b-\\u200f", "\\u200c-\\u200f"),
        # what is shown
        "the ants that drowned are counted when hidden": ("if (a.drowned && st.drowned === false) return; by[a.team]++;", "by[a.team]++; if (a.drowned && st.drowned === false) return;"),
        "the squares that drowned are shown when hidden": ("m.cells.forEach(function (c) { if (c.drowned && st.drowned === false) return;", "m.cells.forEach(function (c) { if (false) return;"),
        "one team shows every ant": ("if (st.team < 0 || a.team === st.team) { ants.push(a); total++; }", "{ ants.push(a); total++; }"),
        "one team shows every square": ("if (st.team < 0 || c.team === st.team) { cells.push(c); total += c.count; }", "{ cells.push(c); total += c.count; }"),
        "a square weighs one ant": ("return sel.cells.map(function (c) { return [c.x * m.side + (m.side - 1) / 2, c.y * m.side + (m.side - 1) / 2, c.count]; });", "return sel.cells.map(function (c) { return [c.x * m.side + (m.side - 1) / 2, c.y * m.side + (m.side - 1) / 2, 1]; });"),
        "the busiest place looks at one bucket": ("for (var dx = -1; dx <= 1; dx++)", "for (var dx = 0; dx <= 0; dx++)"),
        "the busiest place counts places, not ants": ("n += q[2];", "n += 1;"),
        "the busiest place has no radius": ("return Math.max(2, Math.round(m.w / 30), m.squares ? m.side : 0);", "return 2;"),
        "a huge map is not huge": ("Math.max(m.w, m.h) > HUGE", "Math.max(m.w, m.h) > 1e9"),
        "a huge match starts as Heat": ("return isHuge(m) ? 'dots' : saved === 'dots' ? 'dots' : 'heat';", "return saved === 'dots' ? 'dots' : 'heat';"),
        "every match starts as Dots": ("return isHuge(m) ? 'dots' : saved === 'dots' ? 'dots' : 'heat';", "return 'dots';"),
        # the glow
        "the glow falls off too slowly": ("kern[i] = Math.exp(-(i * i) / (2 * s * s));", "kern[i] = Math.exp(-(i * i) / (4 * s * s));"),
        "a count weighs one ant in the glow": ("var ky = p[2] * kern[Math.abs(y - py)]", "var ky = kern[Math.abs(y - py)]"),
        "the glow canvas has no limit": ("Math.min(2400, Math.round(w * ppt))", "Math.round(w * ppt)"),
        "the glow is everywhere": ("a = Math.max(0, Math.min(1, (v - 0.035) / 0.22)) * 0.92", "a = 0.92"),
        "the glow starts bright": ("[0, [157, 13, 23]]", "[0, [257, 13, 23]]"),
        "one team has the glow of all": ("var c = rgb[team] || FALLBACK[team];", "var c = FALLBACK[team];"),
        "the rulers step by two": ("span <= 34 ? 5", "span <= 34 ? 2"),
        "the original has no picture of its own": ("return 'front/preview_' + key + '.png';", "return '';"),
        # the card
        "a card of a match with no map is a card of nothing": ("if (!model) return compact ? null : quiet(false);", "if (!model) return null;"),
        "the small card of a match with no map is an empty box": ("if (!model) return compact ? null : quiet(false);", "if (!model) return quiet(compact);"),
        "the quiet line says something else": ("p.appendChild(doc.createTextNode('No map of deaths for this match.'));", "p.appendChild(doc.createTextNode('Nothing to show.'));"),
        "the choice of a mode is not kept": ("b.addEventListener('click', function () { st.mode = x[0]; keep(x[0]); st.pick = null; render(); });", "b.addEventListener('click', function () { st.mode = x[0]; st.pick = null; render(); });"),
        "a storage that throws is not caught": ("function saved() { try { return env.storage ? env.storage.getItem(MODE_KEY) : null; } catch (e) { return null; } }", "function saved() { return env.storage ? env.storage.getItem(MODE_KEY) : null; }"),
        "a storage that throws on write is not caught": ("try { if (env.storage) env.storage.setItem(MODE_KEY, mode); } catch (e) { /* the choice is not remembered */ }", "if (env.storage) env.storage.setItem(MODE_KEY, mode);"),
        "the switch of the drowned only draws again": ("d.addEventListener('click', function () { st.drowned = !st.drowned; st.pick = null; render(); });", "d.addEventListener('click', function () { st.drowned = !st.drowned; st.pick = null; draw(); });"),
        "one team in the sum is not named in the title": ("(st.team < 0 ? plural(sel.total, 'ant', 'ants') + ' died' : n0(sel.total) + ' of ' + chipName(st.team) + '\\u2019s ants died') + ' in '", "plural(sel.total, 'ant', 'ants') + ' died in '"),
        "a chip changes nothing": ("b.addEventListener('click', function () { st.team = team; st.pick = null; render(); }); chips.appendChild(b);", "b.addEventListener('click', function () { st.pick = null; render(); }); chips.appendChild(b);"),
        "a ring is shown for a team that is not shown": ("var visible = function (a) { return a && (st.team < 0 || a.team === st.team) && !(a.drowned && st.drowned === false); };", "var visible = function (a) { return a && !(a.drowned && st.drowned === false); };"),
        "a ring is shown for an ant that drowned and is hidden": ("var visible = function (a) { return a && (st.team < 0 || a.team === st.team) && !(a.drowned && st.drowned === false); };", "var visible = function (a) { return a && (st.team < 0 || a.team === st.team); };"),
        "the small card has marks": ("var showMarks = !compact && !isAll && (m.first || m.last);", "var showMarks = !isAll && (m.first || m.last);"),
        "the sum has marks": ("var showMarks = !compact && !isAll && (m.first || m.last);", "var showMarks = !compact && (m.first || m.last);"),
        "the squares have a number at any size": ("if (e.n > 1 && sd * ppx > 17)", "if (e.n > 1 && sd * ppx > 0)"),
        "a mark can be smaller than four pixels": ("if (ppx < 2.2) {", "if (false) {"),
        "the grid of tiles never shows": ("if (ppx >= 20 && m.side === 1) {\n                        var grid", "if (ppx >= 2000 && m.side === 1) {\n                        var grid"),
        "the zoom has no end": ("return Math.max(8, Math.ceil(26 / g.ppt));", "return 1e9;"),
        "Fit keeps the zoom": ("st.zoom = null; st.pick = null; draw(); }]].forEach", "st.pick = null; draw(); }]].forEach"),
        "the wheel zooms the other way": ("zoomAt(ev.deltaY < 0 ? 1.25 : 0.8, ev.clientX, ev.clientY);", "zoomAt(ev.deltaY < 0 ? 0.8 : 1.25, ev.clientX, ev.clientY);"),
        "a drag moves the map the other way": ("cx: g.vx + g.vw / 2 - dx / (g.ppt * g.s), cy: g.vy + g.vh / 2 - dy / (g.ppt * g.s)", "cx: g.vx + g.vw / 2 + dx / (g.ppt * g.s), cy: g.vy + g.vh / 2 + dy / (g.ppt * g.s)"),
        "a drag is a tap": ("if (!was || dragged || ev.type === 'pointercancel') {", "if (!was || ev.type === 'pointercancel') {"),
        "a tap finds a mark however far": ("return best && bd < 22 ? { kind: 'ant', ant: best } : null;", "return best ? { kind: 'ant', ant: best } : null;"),
        "the tooltip counts the ant itself": ("var p = st.pick.ant, same = (tiles[p.x + ',' + p.y] || [p]).length - 1;", "var p = st.pick.ant, same = (tiles[p.x + ',' + p.y] || [p]).length;"),
        "a drowned ant does not say who hit it": ("'Drowned' + (p.killer >= 0 ? '; last hit by ' + shortName(seatName(ctx, p.killer)) : '')", "'Drowned'"),
        "an ant that nobody killed has a killer": ("(p.killer >= 0 ? shortName(seatName(ctx, p.killer)) : 'not known')", "(p.killer >= 0 ? shortName(seatName(ctx, p.killer)) : shortName(seatName(ctx, p.team)))"),
        "the tooltip is over the map on a phone": ("<= 720 && !compact;", "<= 0 && !compact;"),
        "a sum of another map size is shown": ("if (!sum || sum.w !== model.w || sum.h !== model.h) {", "if (!sum) {"),
        "the sum is asked for every time": ("if (st.sum) { st.scope = 'all'; st.sumState = ''; render(); sec.dmDraw(); return; }", ""),
        "back to this match keeps the sum": ("if (scope === 'match') { st.scope = 'match'; st.sumState = ''; render(); if (sec.dmDraw) sec.dmDraw(); return; }", "if (scope === 'match') { st.sumState = ''; render(); if (sec.dmDraw) sec.dmDraw(); return; }"),
        "the map name of the sum is not cleaned": ("map = typeof obj(rec).map === 'string' ? clean(rec.map, 64) : '';", "map = typeof obj(rec).map === 'string' ? rec.map : '';"),
        "a name is not cleaned": ("return clean(ctx.by[seat] ? H.seatLabel(ctx.by[seat]) : SEATS[seat], 40) || SEATS[seat];", "return (ctx.by[seat] ? H.seatLabel(ctx.by[seat]) : SEATS[seat]) || SEATS[seat];"),
        "a chip is markup": ("b.appendChild(doc.createTextNode(label + ' ')); b.appendChild(el('b', '', n0(n)));", "b.innerHTML = label + ' '; b.appendChild(el('b', '', n0(n)));"),
        "a card with no size is drawn": ("if (!(sw > 0) || !(sh > 0)) return null;", ""),
        "a page is drawn again without its cards": ("for (var i = 0; i < boxes.length; i++) if (boxes[i].dmDraw) boxes[i].dmDraw();", ""),
        "the card has no id": ("if (!compact) { sec.id = 'deaths'; sec.setAttribute('aria-labelledby', 'deaths-h'); }\n            var draw", "var draw"),
    }

    def test_the_check_can_fail(self):
        text = read("web", "death_map.js")
        with tempfile.TemporaryDirectory() as folder:
            shutil.copy(os.path.join(REPO, "web", "history_page.js"), os.path.join(folder, "history_page.js"))        # (the script finds its helpers beside itself: a broken copy must not fail for the lack of them)
            for why, (old, new) in self.MUTANTS.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "death_map.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                done = run_check(path)
                self.assertNotIn("Cannot find module", done.stdout + done.stderr, why)
                self.assertNotEqual(done.returncode, 0, "the check accepted a script where " + why + "\n" + done.stdout[-400:])


@unittest.skipUnless(NODE, "node is not installed: the Stats panel was NOT run (tests/scripts/web_death_map_check.js)")
class TheStatsPanel(unittest.TestCase):
    """AntsHistory.replayStats (web/history_page.js) puts the small card in the panel and asks for it to be drawn when the panel is opened"""
    MUTANTS = {
        "the card is not added to the panel": ("if (dm) box.appendChild(dm);", ""),
        "the card is not asked to be small": ("env.deathMap.card(rec, { compact: true })", "env.deathMap.card(rec, {})"),
        "opening the panel does not draw the card": ("redraw(pop); }\n            }", "}\n            }"),
        "opening the row does not draw the card": ("if (!closed) redraw(flow); });", "});"),
        "a card that throws takes the panel with it": ("catch (e) { dm = null; }", "catch (e) { throw e; }"),
        "a redraw that throws is heard": ("catch (e) { /* the card is drawn when it is next opened */ }", "catch (e) { throw e; }"),
        "a window that changes size draws a closed panel": ("if (!pop.hidden) redraw(pop); if (!flow.classList.contains('closed')) redraw(flow);", "redraw(pop); redraw(flow);"),
        "a window that changes size draws nothing": ("if (!pop.hidden) redraw(pop); if (!flow.classList.contains('closed')) redraw(flow);", ""),
    }

    def test_the_check_can_fail(self):
        text = read("web", "history_page.js")
        with tempfile.TemporaryDirectory() as folder:
            for why, (old, new) in self.MUTANTS.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "history_page.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                done = run_check(os.path.join(REPO, "web", "death_map.js"), path)
                self.assertNotIn("Cannot find module", done.stdout + done.stderr, why)
                self.assertNotEqual(done.returncode, 0, "the check accepted a page script where " + why + "\n" + done.stdout[-400:])


class TheFiles(unittest.TestCase):
    page = read("web", "match.html")
    script = read("web", "death_map.js")
    css = read("web", "death_map.css")
    shell = read("web", "shell.html")

    def test_the_stylesheet_and_the_script_are_linked_after_the_shared_ones_and_load_nothing_from_elsewhere(self):
        self.assertIn('<link rel="stylesheet" href="match_stats.css">\n    <link rel="stylesheet" href="death_map.css">', self.page)
        self.assertIn('<script src="history_page.js"></script>\n<script src="match_stats.js"></script>\n<script src="death_map.js"></script>\n<script>', self.page)
        self.assertIn('<link rel="stylesheet" href="death_map.css">', self.shell)
        self.assertRegex(self.shell, r'<script src="history_page\.js"></script><script src="death_map\.js"></script>')
        self.assertNotRegex(self.css, r"url\((?:https?:)?//")
        self.assertNotIn("@import", self.css)
        self.assertEqual(set(re.findall(r"https?://[^'\" ]+", self.script)) - {"http://www.w3.org/2000/svg"}, set())

    def test_nothing_that_a_record_carries_becomes_markup(self):
        for bad in (".innerHTML", "document.write", "insertAdjacentHTML", "outerHTML", "eval(", "new Function", "setAttribute('style'", "setAttribute('href'", "setAttribute('src'", "location."):
            self.assertNotIn(bad, self.script, bad)
        for fetch in re.findall(r"fetch\(", self.script):
            self.fail("the script asks for nothing itself: the page gives it the one question (env.sum)")

    def test_the_card_is_the_entry_right_after_over_time_and_a_match_with_no_stats_gets_one_box_not_two(self):
        self.assertIn("var SECTIONS = [resultSection, replaySection, stats.summary, boardSection, stats.overtime, deathsSection, stats.activity, moreSection, aboutSection];", self.page)
        self.assertIn("if (window.AntsMatchStats.noStatsNote(rec)) return null;\n        try { return deathMap.card(rec, {}); } catch (e) { return null; }", self.page)      # (a card that throws costs the page nothing)
        self.assertIn("window.AntsDeathMap.redraw(document)", self.page)
        self.assertEqual(self.page.count("window.AntsDeathMap.redraw(document)"), 2)               # (after the page is built, and when the window changes size)
        self.assertEqual(self.page.count("addEventListener('resize'"), 1)

    def test_the_sum_of_a_map_is_asked_for_with_the_name_percent_encoded_by_the_pickers_rule(self):
        self.assertIn("fetch('/history/deaths?map=' + name, { cache: 'no-store', credentials: 'omit' })", self.page)
        self.assertIn("window.AntsMapPicker.percentEncode(String(map))", self.page)

    def test_the_replay_panel_gets_the_small_card_and_the_game_page_does_not_grow(self):
        helpers = read("web", "history_page.js")
        self.assertIn("env.deathMap.card(rec, { compact: true })", helpers)
        self.assertIn("redraw(pop)", helpers)
        self.assertIn("redraw(flow)", helpers)
        self.assertIn("deathMap: window.AntsDeathMap && window.AntsDeathMap.create(", self.shell)
        self.assertEqual(len(self.shell.splitlines()), 3966)                                    # (tools/check_hygiene.py: the game page may not grow)

    def test_the_seat_colors_are_the_pages(self):
        for token in ("--c-green", "--c-red", "--c-blue", "--c-black"):
            self.assertIn(token, self.css)
        self.assertEqual(len(re.findall(r"#79bf93|#d4707f|#8f8fd6|#b3afb9", self.css)), 0)           # (one home for the colors: matches.css)

    def test_a_card_on_a_phone_lies_inside_the_screen_and_the_stage_lets_the_page_scroll_until_it_is_zoomed(self):
        self.assertRegex(self.css, r"\.dm-stage \{[^}]*touch-action: pan-y")
        self.assertRegex(self.css, r"\.dm-stage\.zoomed \{[^}]*touch-action: none")
        self.assertIn("@media (max-width: 720px)", self.css)
        self.assertRegex(self.css, r"\.dm-body \{[^}]*grid-template-columns: minmax\(0, 560px\) minmax\(0, 1fr\)")

    def test_the_image_and_the_local_build_carry_the_two_files(self):
        dockerfile = read("Dockerfile")
        for name in ("death_map.css", "death_map.js"):
            self.assertIn("COPY web/%s /usr/share/nginx/html/%s" % (name, name), dockerfile)
            self.assertIn('cp -f "${SCRIPT_DIR}/web/%s" "${DIST_DIR}/"' % name, read("build_web.sh"))
        self.assertIn("death_map.js death_map.css", read(".github", "workflows", "ci.yml"))


class TheDocument(unittest.TestCase):
    def test_the_replays_page_describes_the_map_the_field_and_the_pass(self):
        text = read("docs", "REPLAYS.md")
        for needle in ("The map of deaths", "web/death_map.js", "ants_server/history_deaths", "kDeathsMaxBytes", "history_deaths", "/history/deaths", "No map of deaths for this match.", "tile_x"):
            self.assertIn(needle, text, needle)
        server = read("docs", "SERVER.md")
        for needle in ("history_deaths", "/history/deaths", "schema version 7"):
            self.assertIn(needle, server, needle)


if __name__ == "__main__":
    unittest.main()
