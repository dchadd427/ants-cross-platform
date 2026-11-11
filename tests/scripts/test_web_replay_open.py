#!/usr/bin/env python3
"""Opening a replay FILE of your own on the website (run by ./run_tests.sh --fast and by the CI): the "Got a replay file?" strip of the Matches page (web/matches.html, web/matches.css), the player's own
mode (web/shell.html at play.html?local=<id>) and the code that they share (web/replay_page.js). docs/REPLAYS.md "A file of your own" says what it does and why nothing leaves the device.

  - the shared code is RUN (node, when it is installed: tests/scripts/web_replay_open_check.js): the cheap questions that are asked before the game is loaded for a file (the size from File.size before a
    byte is read, and again on the bytes; the 8 bytes that open a replay; a HEAD chunk after them, at most 4096 bytes long, with the right checksum) on the real file of tests/data/ and on every cut and every
    flipped bit of its head; the name of the file as text; the size in words;
  - the map that a file carries is counted before the game starts (web/map_files.js startMarkersOfMap, run on all the maps of the library by tests/scripts/web_map_files_check.js) and the limit is the game's:
    kMaxEmbeddedStartAnts and kMaxEmbeddedPlantWaypointPairs (include/ants_replay/replay.hpp, checked by fits_a_replay in src/ants_replay/player.cpp for the map that a file carries, whichever copy is read; the C++
    tests RP9.11 and RA7.10 - RA7.13) are pinned equal to the page's MAX_EMBEDDED_ANTS and MAX_EMBEDDED_PAIRS, and the plants that the page counts are the tiles of the engine's table; the card of a map that the game
    refuses says the game's reason; text that cannot wrap (a file name, a map name, player names) is wrapped by the rules of the game page, and a file name is one unit for the bidirectional algorithm;
  - tests/data/replay_open_mutants.json is the list of changes to the page and the code that the checks must notice (tools/mutate.py runs it with `--keep-going --jobs 1`; the class at the end of this file
    runs the same list in the quick tier on a copy of the files): every one is a way to lose a promise (the size is not checked, the checksum is not, a drop that is not taken over, a file that is sent to the
    server, a record that is kept for ever ...).
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NODE = shutil.which("node")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_replay_open_check.js")
FIXTURE = os.path.join(REPO, "tests", "data", "replay_bots_tiny.antsrep")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def has(case, needle, text, label=None):
    """needle is in text (a failure names the needle and does not print the page)"""
    case.assertTrue(needle in text, "missing: %r" % (label or needle[:90]))


def lacks(case, needle, text):
    case.assertFalse(needle in text, "found: %r" % needle[:90])


def run_node(script, *args):
    return subprocess.run([NODE, CHECK_JS, script] + list(args), capture_output=True, text=True, timeout=120)


@unittest.skipUnless(NODE, "node is not installed: the code that opens a file of your own was NOT run (tests/scripts/web_replay_open_check.js)")
class TheCodeRuns(unittest.TestCase):
    def test_the_size_the_magic_the_head_and_the_checksum_of_a_file_the_name_and_the_size_in_words(self):
        done = run_node(os.path.join(REPO, "web", "replay_page.js"), FIXTURE)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r" 0 failures")


class TheStrip(unittest.TestCase):
    """The strip of the Matches page (approved pictures, docs/REPLAYS.md "A file of your own"): its words, the chooser that has no `accept` (a phone's chooser greys out a file of an ending that it does not know:
    the bytes are checked instead), where it stands, and that the page asks the site for nothing more because of it."""
    page = read("web", "matches.html")
    css = read("web", "matches.css")

    def test_it_stands_between_the_header_and_the_tools_with_the_approved_words(self):
        order = [self.page.index(x) for x in ('<header class="mast"', 'id="opener"', 'id="open-note"', 'id="tools"', 'id="list-panel"')]
        self.assertEqual(order, sorted(order))
        strip = self.page[self.page.index('<section class="panel opener"'):self.page.index('<div id="open-note"')]
        has(self, 'aria-labelledby="opener-h" hidden>', strip)                                         # (hidden until the script knows that the browser can read a file)
        for words in ('<h2 id="opener-h">Got a replay file?</h2>', "Watch a downloaded .antsrep here. It stays on this device: nothing is uploaded.", "Open a replay file</button>"):
            has(self, words, strip)
        has(self, '<div id="open-note" aria-live="polite" hidden></div>', self.page)
        veil = self.page[self.page.index('<div class="dropveil"'):self.page.index('<footer')]
        for words in ("Drop the replay file to watch it", "It stays on this device. Nothing is uploaded.", 'hidden aria-hidden="true"'):
            has(self, words, veil)

    def test_the_chooser_takes_any_file_and_is_out_of_the_way(self):
        tag = re.search(r'<input type="file"[^>]*>', self.page).group(0)
        self.assertNotIn("accept", tag)                                                                    # (the first line of defence is the bytes: web/replay_page.js localCheck)
        self.assertNotIn("multiple", tag)
        self.assertIn('id="open-file"', tag)
        self.assertIn('class="sr"', tag)                                                                   # (visually hidden, not display: none: a phone opens the chooser from a hidden input only when it is in the layout)
        self.assertIn('tabindex="-1" aria-hidden="true"', tag)
        self.assertEqual(len(re.findall(r'type="file"', self.page)), 1)
        self.assertNotRegex(read("web", "replay_page.js"), r"\.accept\s*=|setAttribute\('accept'")

    def test_the_page_wires_the_strip_in_one_call_and_goes_to_the_player_in_this_tab(self):
        self.assertEqual(self.page.count("R.opener({"), 1)
        call = self.page[self.page.index("R.opener({"):]
        call = call[:call.index(".wire();")]
        has(self, "store: R.ownStoreOf(window)", call)
        has(self, "go: function (url) { window.location.assign(url); }", call)                          # (this tab: the way back is the browser's Back, and no popup to be blocked)
        lacks(self, "window.open", self.page)
        self.assertNotIn("target=", self.page[self.page.index('id="opener"'):self.page.index('id="tools"')])

    def test_the_page_asks_the_site_for_nothing_because_of_the_strip(self):
        script = self.page[self.page.index("<script>\n(function"):]
        self.assertEqual(sorted(re.findall(r"fetch\('([^']*)'", script)), ["/history", "/live"])
        for bad in ("XMLHttpRequest", "sendBeacon", "FormData", "method:", "WebSocket", "navigator.serviceWorker"):
            lacks(self, bad, script)

    def test_nothing_that_the_script_shares_ever_touches_the_network(self):
        text = read("web", "replay_page.js")
        for bad in ("fetch(", "XMLHttpRequest", "sendBeacon", "FormData", "WebSocket", "importScripts", "postMessage", "window.open", "innerHTML", "insertAdjacentHTML", "outerHTML", "document.write", "eval(", "new Function"):
            lacks(self, bad, text)

    def test_the_style_has_the_veil_that_never_catches_the_pointer_the_phone_button_and_the_light_of_the_strip(self):
        for rule in (".dropveil { position: fixed; inset: 0; z-index: 60;", "pointer-events: none; }", ".opener.drag { border-color: var(--gold); }", ".opener.drag .drop { border-color: var(--gold);",
                     "@media (hover: none) and (pointer: coarse) { .dropveil { display: none; } }", "    .opener .btn { width: 100%; margin-bottom: 0; }", "#open-note .btn .ic, .opener .btn .ic { margin-left: 2px; }", ".opener .btn { margin-bottom: 4px; }", "#open-note:not([hidden]) + .panel { margin-top: 14px; }", "#open-note + .panel { margin-top: 22px; }"):
            has(self, rule, self.css)
        veil = self.css[self.css.index(".dropveil {"):]
        self.assertIn("pointer-events: none", veil[:veil.index("}")])
        phone = self.css[self.css.rindex("@media (max-width: 720px) {"):]                                  # (the phone's block of the list's own parts, the last one)
        has(self, ".opener .drop { flex-direction: column; align-items: stretch;", phone[:700])
        self.assertEqual(len(re.findall(r"<style>", self.page)), 1)


class ThePlayer(unittest.TestCase):
    """The game page for a file of your own (play.html?local=<id>): the address is read in the places that read the other two, the file comes from the record and never from the site, nothing starts without
    one, and the pieces that need the site (Copy link, Stats, Download) are not there."""
    page = read("web", "shell.html")
    glue = page[page.index("<!-- BEGIN replay glue"):page.index("<!-- END replay glue")]

    def test_the_address_is_read_where_the_other_two_are_and_a_replay_wins(self):
        has(self, "(replay|live|local)=", self.page)
        self.assertIn("if (/(^|[?&])local=/.test(window.location.search) && !/(^|[?&])replay=/.test(window.location.search)) document.body.className += ' ownfile';", self.page)
        self.assertEqual(self.page.count("ANTS_OWN_MODE = ANTS_REPLAY_MODE && /(^|[?&])local=/.test(window.location.search) && !/(^|[?&])replay=/.test(window.location.search)"), 1)
        has(self, "var ANTS_LIVE_MODE = ANTS_REPLAY_MODE && !ANTS_OWN_MODE &&", self.page)
        has(self, "&& !ANTS_OWN_MODE && /(^|[?&])embed=1(&|$)/.test(window.location.search);", self.page)       # (a file of your own is never the frame of a match page)

    def test_the_file_is_the_record_and_nothing_is_fetched_for_it(self):
        has(self, "fetched = OWN ? OWN.read() : Promise.resolve({ error: 'gone' });", self.glue)
        self.assertEqual(re.findall(r"fetch\(([^,)]*)", self.glue), ["'/replays'", "'/live/' + LIVE_ID", "'/replays/' + file", "'/replays/' + FILE"])      # (the list of the pages' requests: none of them is made for a file of your own)
        has(self, "if (got.error) return;                    // (its card is up already)", self.glue)                    # (no record, no game)
        has(self, "if (got.error === 'gone' || got.error === 'again') card(got.error);", self.glue)
        has(self, "window.AntsHistory.replayStats", self.page)
        self.assertIn("ANTS_REPLAY_FILE && typeof fetch === 'function') window.AntsHistory.replayStats", self.page)           # (the stats are asked for by the server's own file name, which a local address has none of)
        has(self, "var OWN = ", self.glue.replace("var LIVE_ID = ANTS_LIVE_ID, OWN = ", "var OWN = "))
        self.assertIn("ANTS_OWN_MODE ? R.ownPageOf(window, ", self.glue)

    def test_a_file_of_your_own_has_no_copy_link_no_stats_and_no_download(self):
        has(self, "body.ownfile [data-dl], body.ownfile #copy-btn, body.ownfile #copy-more, body.ownfile #stats-btn { display: none !important; }", self.page)
        for ident in ("copy-btn", "copy-more", "stats-btn"):
            self.assertEqual(len(re.findall(r'\bid="%s"' % ident, self.page)), 1, ident)
        self.assertEqual(len(re.findall(r'data-dl=""', self.page)), 2)                                    # (the two Downloads: the header's and More's)
        has(self, ".pill.own {", self.page)

    def test_the_words_of_the_cards_come_from_one_place_and_the_glue_takes_them_whole(self):
        self.assertIn("plan = OWN ? OWN.card(kind, { made: made, version: pageVersion(), map: info && info.map ? info.map : OWN.map(), why: why }) : null;", self.glue)
        self.assertIn("if (plan) { title = plan.title; plan.body.forEach(function (node) { body.appendChild(node); }); plan.buttons.forEach(function (node) { buttons.appendChild(node); }); } else if (kind === 'older') {", self.glue)
        has(self, "card(OWN && map.mismatch ? 'changed' : OWN && map.gone ? 'nomap' : 'cant', map.text);", self.glue)
        has(self, "else card(OWN && failure === FAIL.nomap && OWN.lacks(text) ? 'nomap' : 'cant', text);", self.glue)       # (the missing-map card only for the game's words for a folder that lacks the map: any other reason is said as the game says it)
        self.assertIn("if (OWN) { OWN.header(info, headLength(), !!(info && info.turns && !info.over)); return; }", self.glue)
        has(self, "else if (OWN) setHeader();", self.glue)
        has(self, "if (OWN) OWN.chrome();", self.glue)

    def test_the_game_page_has_not_grown(self):
        with open(os.path.join(REPO, "web", "shell.html"), "rb") as f:
            lines = f.read().count(b"\n")
        with open(os.path.join(REPO, "tools", "check_hygiene.py"), encoding="utf-8") as f:
            ceiling = int(re.search(r'"web/shell.html": (\d+)', f.read()).group(1))
        self.assertLessEqual(lines, ceiling)                                                              # (the logic is in web/replay_page.js: the page only calls it)

    def test_a_map_that_a_file_carries_is_counted_before_the_game_starts_and_the_limits_are_the_games(self):
        has(self, "(carried ? Promise.resolve(window.AntsMapFiles.carriedVerdict(carried)) : need ?", self.glue)
        has(self, "if (!map.ok) { card(OWN && map.mismatch ? 'changed' : OWN && map.gone ? 'nomap' : 'cant', map.text); return; }", self.glue)        # (a refused count has neither flag: the card with the reason)
        header, player, script = read("include", "ants_replay", "replay.hpp"), read("src", "ants_replay", "player.cpp"), read("web", "map_files.js")
        native = [int(re.search(r"kMaxEmbeddedStartAnts = (\d+);", header).group(1)), int(re.search(r"kMaxEmbeddedPlantWaypointPairs = (\d+);", header).group(1))]
        page = [int(re.search(r"var MAX_EMBEDDED_ANTS = (\d+);", script).group(1)), int(re.search(r"var MAX_EMBEDDED_PAIRS = (\d+);", script).group(1))]
        self.assertEqual((native, page), ([8000, 1000000], [8000, 1000000]))                                # (the page counts what the game counts, against the same numbers)
        for line in ("if (ants > kMaxEmbeddedStartAnts) {", "if (spawn.team_id < 4) ++ants;", "if (sim::movement::is_dropper_plant_tile(spawn.tile_id)) ++plants;", "const uint64_t waypoints = level.waypoints.size();",
                     "if (plants * waypoints > kMaxEmbeddedPlantWaypointPairs) {", "    return fits_a_replay(level, error);\n}"):
            has(self, line, player)                                                                          # (fits_a_replay: the one place that counts, for every reader of a carried map)
        has(self, "net::hash_bytes(carried.data(), carried.size()) == replay.head.map_hash) return fits_a_replay(level, error);", player)       # (the folder's copy of a carried map is counted too: the page writes it there)
        self.assertEqual(player.count("fits_a_replay(level, error)"), 2)
        has(self, "var MARKER_FIRST = 152, MARKER_LAST = 155;", script)                                       # (the tiles of the start markers: include/ants_assets/lvl_parser.hpp)

    def test_the_plants_that_the_page_counts_are_the_tiles_that_the_engine_looks_a_waypoint_up_for(self):
        table = read("src", "ants_sim", "movement_tables_data.inc")
        start = table.index("kTileFlags[1344] = {")
        body = re.sub(r"/\*.*?\*/", "", re.sub(r"//[^\n]*", "", table[start:table.index("};", start)]), flags=re.S).split("{", 1)[1]
        flags = [int(x, 0) for x in re.findall(r"0[xX][0-9a-fA-F]+|\b\d+\b", body)]
        self.assertEqual(len(flags), 1344)
        ids = [i for i, f in enumerate(flags) if f & 0x10]                                                   # (kTileFlagPlant: sim::movement::is_dropper_plant_tile)
        self.assertEqual(ids, list(range(404, 409)) + list(range(410, 417)) + [420, 421])
        has(self, "function isPlantTile(id) { return (id >= 404 && id <= 408) || (id >= 410 && id <= 416) || id === 420 || id === 421; }", read("web", "map_files.js"))

    def test_nothing_in_the_game_page_can_make_it_scroll_sideways_for_a_name_that_cannot_wrap_and_a_file_name_is_a_unit(self):
        has(self, ".rhead { flex: 1 1 0; min-width: 0; display: grid; grid-template-columns: minmax(0, 1fr); gap: 5px;", self.page)           # (the chips: the track may be as narrow as the window)
        has(self, "gap: 0 14px; color: var(--ink); overflow-wrap: anywhere; }", self.page)                                                    # (the title: the map's name and the file's)
        has(self, ".rm-card p { margin: 0 0 10px; font-size: 16px; line-height: 1.4; color: var(--ink); overflow-wrap: anywhere; }", self.page)  # (the cards: the map's name, the reason)
        script = read("web", "replay_page.js")
        has(self, "if (name) small.appendChild(mk(doc, 'bdi', '', name));", script)                      # (a name that reads right to left is a unit: the length after it stays after it)
        lacks(self, "' ' + [name, seconds", script)

    def test_only_these_places_of_a_page_of_the_site_know_the_address_local(self):
        for name in ("lobby.html", "match.html", "live.html"):
            self.assertNotIn("local=", read("web", name), name)


class TheDocument(unittest.TestCase):
    def test_the_replays_page_describes_a_file_of_your_own(self):
        text = read("docs", "REPLAYS.md")
        has(self, "## A file of your own", text)
        part = text[text.index("## A file of your own"):]
        part = part[:part.index("\n## ", 5)] if "\n## " in part[5:] else part
        for needle in ("play?local=", "IndexedDB", "Nothing is uploaded", "2,097,164", "web/replay_page.js", "tests/data/replay_bots_tiny.antsrep", "ownfile", "test_web_replay_open.sh", "8,000", "kMaxEmbeddedStartAnts", "kMaxEmbeddedPlantWaypointPairs", "1,000,000", "fits_a_replay", "WHICHEVER copy"):
            has(self, needle, part)

    def test_the_tree_of_the_sources_lists_the_fuzz_script(self):
        has(self, "web_replay_open_fuzz.sh", read("docs", "ARCHITECTURE.md"))


class TheFixture(unittest.TestCase):
    """tests/data/replay_bots_tiny.antsrep: a real recording that bot_arena --save-replays wrote (a match on Tiny between four computer players, 6 minutes), 3 KB. It holds nothing of a person: the
    names are "Bot (Medium)", the build is a commit's short name. A byte of it must never be changed by a line-end conversion (.gitattributes)."""

    def test_it_is_a_whole_replay_of_the_size_that_the_cases_assume(self):
        with open(FIXTURE, "rb") as f:
            data = f.read()
        self.assertEqual(data[:8], bytes([0x89, 0x41, 0x52, 0x50, 0x4C, 0x0D, 0x0A, 0x1A]))
        self.assertEqual(data[8:12], b"HEAD")
        self.assertTrue(2000 < len(data) < 6000, len(data))
        self.assertIn(b"ENDS", data[-40:])                                                              # (a whole match: its last chunk)
        self.assertNotIn(b"tmap", data)                                                                 # (it plays on a map that the game has)

    def test_git_leaves_its_bytes_alone(self):
        self.assertRegex(read(".gitattributes"), r"(?m)^tests/data/\*\.antsrep\s+-text$")

    def test_it_names_nobody(self):
        with open(FIXTURE, "rb") as f:
            data = f.read()
        head = data[16:16 + int.from_bytes(data[12:16], "little")].decode("latin-1")             # (every text of the file is a field of its head: the other chunks are numbers)
        self.assertIn("Bot (Medium)", head)
        for word in ("/home", "/root", "/Users", "@", "password", "token", "C:\\"):
            self.assertFalse(word in head, "the head holds %r" % word)


@unittest.skipUnless(NODE, "node is not installed: the mutants of the page that opens a file of your own were NOT run (tests/data/replay_open_mutants.json)")
class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/replay_open_mutants.json (the list that tools/mutate.py runs too, with `--keep-going --jobs 1`) on a COPY of the files, one at a time: each must make its checks fail, and the copy
    with nothing changed must pass"""
    FILES = ("web/replay_page.js", "web/matches.html", "web/matches.css", "web/shell.html", "web/map_files.js", "docs/REPLAYS.md", "docs/ARCHITECTURE.md", "tools/check_hygiene.py", ".gitattributes",
             "include/ants_replay/replay.hpp", "src/ants_replay/player.cpp", "src/ants_sim/movement_tables_data.inc",
             "web/live.html", "web/lobby.html", "web/match.html",
             "tests/scripts/web_replay_open_check.js", "tests/scripts/web_map_files_check.js", "tests/scripts/replay_file_tools.js", "tests/scripts/test_web_replay_open.py",
             "tests/data/replay_bots_tiny.antsrep", "tests/data/replay_head.hex", "tests/data/replay_tmap.hex", "Community-Maps/eze jungal.lvl") + tuple("Original-Ants/Maps/%s.LVL" % m for m in ("TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS")) + tuple("Community-Maps/" + n for n in sorted(os.listdir(os.path.join(REPO, "Community-Maps"))) if n.lower().endswith(".lvl") and n != "eze jungal.lvl")       # (the library, for the check that no map of it is refused)

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "replay_open_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_open_mutants.")
        for name in cls.FILES:
            target = os.path.join(cls.folder, *name.split("/"))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy(os.path.join(REPO, *name.split("/")), target)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def run_command(self, command):
        done = subprocess.run(command, shell=True, cwd=self.folder, capture_output=True, text=True, timeout=300)
        return done.returncode, done.stdout + done.stderr

    def test_there_are_enough_of_them_and_each_says_what_it_loses(self):
        ids = [m["id"] for m in self.spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        self.assertGreaterEqual(len(ids), 30)
        for m in self.spec["mutants"]:
            self.assertTrue(m.get("why"), m["id"] + ": no reason")

    def test_the_copy_with_nothing_changed_passes(self):
        for command in sorted({m["test"] for m in self.spec["mutants"]}):
            status, out = self.run_command(command)
            self.assertEqual(status, 0, command + "\n" + out[-2000:])

    def test_every_mutant_is_seen(self):
        missed = []
        for m in self.spec["mutants"]:
            path = os.path.join(self.folder, *m["file"].split("/"))
            with open(path, encoding="utf-8") as f:
                text = f.read()
            self.assertEqual(text.count(m["old"]), 1, "%s: %r is not in %s exactly once" % (m["id"], m["old"][:60], m["file"]))
            try:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(text.replace(m["old"], m["new"]))
                status, _ = self.run_command(m["test"])
            finally:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(text)
            if status == 0:
                missed.append(m["id"])
        self.assertEqual(missed, [], "the checks did not see these changes")


if __name__ == "__main__":
    unittest.main()
