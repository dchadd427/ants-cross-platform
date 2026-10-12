#!/usr/bin/env python3
"""The map picker of the front page (run by ./run_tests.sh --fast and by the CI): a list of the maps that the game server offers (GET /maps), a Detail list or a Thumbnails grid, a search over name and
description, four sorts (the originals first and then the most played is the default), the Original badge, the play counts, a bottom sheet on a phone, keyboard use (arrows, Enter, Esc, Tab kept inside),
and the choice of a map of the players, which sets the room's map as the selector does and warms the browser's cache for the game page (docs/PLAY_IN_BROWSER.md "Choosing a map").

  - the picker's own code is RUN (node, when it is installed): the parsing of the list, the sorts, the search, the whole picker with a small fake of the DOM (tests/scripts/web_map_picker_check.js), and
    the check is shown able to fail: broken copies of the script are refused by it;
  - the page's side is read from the source: the button, the dialog, the scripts in their order, the selector that stays (prev, next, select) and the fall back when the list cannot be had.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "web", "front", "map_picker.js")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_map_picker_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_check(script=SCRIPT):
    return subprocess.run([shutil.which("node"), CHECK_JS, script], capture_output=True, text=True, timeout=120)


class ThePage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.page = read("web", "lobby.html")
        cls.script = read("web", "front", "map_picker.js")

    def test_the_selector_stays_and_a_button_opens_the_list(self):
        for ident in ('id="prev"', 'id="next"', 'id="map"', 'id="browse"', 'id="picker-panel"'):
            self.assertTrue(ident in self.page, ident)
        self.assertTrue('aria-haspopup="dialog"' in self.page)
        self.assertTrue('role="dialog" aria-modal="true"></div></div>' in self.page)                        # (the picker names it by its heading: aria-labelledby)
        self.assertTrue("$('browse').disabled = side.mapDisabled;" in self.page)                           # the joiner's list is read-only as the selector is

    def test_the_scripts_come_before_the_pages_own_in_this_order(self):
        self.assertTrue('<script src="front/map_picker.js"></script>\n<script src="map_files.js"></script>\n<script>' in self.page)

    def test_a_choice_sets_the_rooms_map_like_the_selector_and_warms_the_cache(self):
        self.assertTrue("known ? !sendPlan({ map: known.key }) : !sendPlan({ file: entry.file })" in self.page)
        self.assertTrue("canOpen: isHost, canChoose: isHost" in self.page)
        self.assertTrue("AntsMapFiles.warm(window, file)" in self.page)
        self.assertTrue("if (isHost()) ensurePicker().open(model.mapFile);" in self.page)

    def test_the_list_is_fetched_once_and_a_failure_leaves_the_old_selector(self):
        self.assertEqual(len(re.findall(r"opts\.fetch\('/maps'", self.script)), 1)
        self.assertTrue("The six original maps can still be chosen with the arrows beside the picture." in self.script)

    def test_the_view_is_remembered_in_local_storage_inside_try(self):
        self.assertTrue("var VIEW_KEY = 'ants.mapview';" in self.script)
        self.assertTrue(re.search(r"try \{ var v = storage && storage\.getItem\(VIEW_KEY\);.*\} catch \(e\)", self.script) is not None)
        self.assertTrue(re.search(r"try \{ if \(storage\) storage\.setItem\(VIEW_KEY, v\); \} catch \(e\)", self.script) is not None)

    def test_the_pictures_have_sizes_lazy_loading_and_no_alt_text(self):
        for needle in ("'width', String(px)", "'height', String(px)", "'loading', 'lazy'", "'alt', ''", "thumbs ? 130 : 64"):
            self.assertTrue(needle in self.script, needle)

    def test_the_lobbys_own_look_is_used_and_nothing_from_another_site(self):
        for token in ("var(--gold)", "var(--frame)", "var(--cream)", "var(--muted)", "var(--edge)"):
            self.assertTrue(re.search(r"\.pk-[\w-]+[^{]*\{[^}]*" + re.escape(token), self.page) is not None, token)
        self.assertTrue(re.search(r"https?://", self.script) is None)


@unittest.skipUnless(shutil.which("node"), "node is not installed: the picker's own code was NOT run (tests/scripts/web_map_picker_check.js)")
class ThePickersOwnCode(unittest.TestCase):
    def test_the_list_the_sorts_the_search_and_the_whole_picker(self):
        done = run_check()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue("ok: " in done.stdout)

    def test_the_check_can_fail(self):
        text = read("web", "front", "map_picker.js")
        broken = {
            "the originals are not first": ("if (a.original !== b.original) return a.original ? -1 : 1;", ""),
            "ties are not by name": ("return a.original ? a.order - b.order : (b.plays - a.plays) || byName(a, b);", "return a.original ? a.order - b.order : (b.plays - a.plays);"),
            "the search leaves out the description": ("var hay = (m.name + ' ' + m.description).toLowerCase();", "var hay = m.name.toLowerCase();"),
            "the search needs one word only": ("return words.every(function (w) { return hay.indexOf(w) !== -1; });", "return words.some(function (w) { return hay.indexOf(w) !== -1; });"),
            "a picture may have a query or a slash": ("|/front/preview_[a-z]+\\\\.png)$');", "|/front/preview_[a-z]+\\\\.png)');"),
            "an escaped slash is a name": ("|%(?!2[fF]|5[cC]|00)[0-9A-Fa-f]{2})*\";", "|%[0-9A-Fa-f]{2})*\";"),
            "a name may start with a dot": ("name.charAt(0) !== '.' && /\\.(lvl|LVL)$/.test(name);", "/\\.(lvl|LVL)$/.test(name);"),
            "any case of the ending": ("/\\.(lvl|LVL)$/.test(name);", "/\\.lvl$/i.test(name);"),
            "the view is not remembered": ("function remember(storage, v) { try { if (storage) storage.setItem(VIEW_KEY, v); }", "function remember(storage, v) { try { if (false) storage.setItem(VIEW_KEY, v); }"),
            "the list is asked for every time": ("if (status === 'ready' && !again) return;", ""),
            "the list has no time limit": ("timer = later(failed, 12000);", ""),
            "a failure is retried at once by the page": ("if (!again && status === 'error' && clock() - failedAt < 15000) return;", ""),
            "reopening after an error does not retry": ("if (status === 'error' || status === 'idle') load(true); else", "if (status === 'idle') load(true); else"),
            "a search is not waited for": ("typing = later(function () { typing = 0; query = parts.q.value; paint('top'); }, 120);", "query = parts.q.value; paint('top');"),
            "a choice ignores whether the room is still the host's": ("var allowed = !opts.canChoose || opts.canChoose();", "var allowed = true;"),
            "the list opens for a joiner": ("if (opts.canOpen && !opts.canOpen()) return;", ""),
            "Escape on the body does not close": ("function onDocKey(e) { if (opened && e.key === 'Escape') { e.preventDefault(); close(); } }", "function onDocKey(e) {}"),
            "the search box takes the focus on a phone": ("(coarse ? host : parts.q).focus();", "parts.q.focus();"),
            "the room's map in another case is not found": ("function canon(file) { return byFile[file] ? file : byLower[String(file || '').toLowerCase()] ? byLower[String(file).toLowerCase()].file : file; }", "function canon(file) { return file; }"),
            "a sort does not bring the chosen row into view": ("if (cur && cur.scrollIntoView) cur.scrollIntoView({ block: 'center' }); else parts.out.scrollTop = was;", "parts.out.scrollTop = was;"),
            "a new search keeps the old place": ("if (where === 'top') parts.out.scrollTop = 0;", "if (where === 'top') parts.out.scrollTop = was;"),
            "no empty state": ("e.appendChild(el('b', '', 'No map is called \"' + query.trim() + '\".'));", "e.appendChild(el('b', '', ''));"),
            "the description is printed in a row": ("body.appendChild(el('div', 'pk-meta', metaText(m)));", "body.appendChild(el('div', 'pk-meta', metaText(m) + ' ' + m.description));"),
            "Escape does not close": ("if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); close(); return; }", ""),
            "Enter does not choose": ("else if (k === 'Enter') { e.preventDefault(); if (i >= 0) choose(); return; }", "else if (k === 'Enter') { return; }"),
            "a name is markup": ("var nm = el('div', 'pk-nm', m.name);", "var nm = el('div', 'pk-nm'); nm.innerHTML = m.name;"),
        }
        with tempfile.TemporaryDirectory() as folder:
            for why, (old, new) in broken.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "map_picker.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                self.assertNotEqual(run_check(path).returncode, 0, "the check accepted a script where " + why)


if __name__ == "__main__":
    unittest.main()
