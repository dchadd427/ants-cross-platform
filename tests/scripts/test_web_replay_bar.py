#!/usr/bin/env python3
"""Hiding the bar of a fullscreen replay (docs/REPLAYS.md "Hiding the bar"): the page code in web/shell.html (the handle, the three states, the B key, the feed of the mouse) and the state machine in
web/replay_page.js (barZone, run on a fake clock by tests/scripts/web_replay_bar_check.js). The list tests/data/replay_bar_hide_mutants.json (tools/mutate.py runs it with `--keep-going --jobs 1`) is run
here on a copy of the files, one change at a time: each must make a check fail. The opt-in `tests/scripts/test_web_live.sh --only hide` shows it in a real browser."""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NODE = shutil.which("node")
CHECK = os.path.join("tests", "scripts", "web_replay_bar_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


class TheStyleAndTheMarkup(unittest.TestCase):
    page = read("web", "shell.html")
    glue = page[page.index("<!-- BEGIN replay glue"):page.index("<!-- END replay glue")]

    def test_the_handle_is_a_focusable_button_with_the_state_in_aria_and_the_chevron_says_it_too(self):
        self.assertRegex(self.page, r'<button class="rhandle rep-only" type="button" id="b-handle" aria-label="Show the bar" aria-controls="rbar" aria-expanded="false"[^>]*hidden>')
        self.assertRegex(self.page, r'<button class="rtab" type="button" id="b-tab" aria-label="Hide the bar" aria-expanded="true"')
        self.assertIn("$('b-tab').setAttribute('aria-expanded', m === 'hidden' ? 'false' : 'true'); if (m !== 'hidden' && fromHandle) $('b-tab').focus();", self.glue)
        self.assertNotIn("Keep the bar open", self.page)                                                     # (no handle over a shown bar: nothing to keep open with it)

    def test_the_handle_is_thin_big_enough_to_hit_and_leaves_the_picture_to_the_game(self):
        rule = re.search(r"\.rhandle \{([^}]*)\}", self.page).group(1)
        for text in ("width: 44px;", "height: 14px;", "bottom: env(safe-area-inset-bottom, 0px);", "touch-action: none;"):
            self.assertIn(text, rule)
        self.assertRegex(self.page, r'\.rhandle::before \{ content: ""; position: absolute; left: -12px; right: -12px; top: -30px; bottom: 0; \}')       # (a hit area of 44 px high, invisible)
        self.assertIn("$('b-handle').hidden = !(bar.classList.contains('over') && zone.mode() === 'hidden');", self.glue)       # (the handle shows only while the bar is hidden: never a second arrow over a shown bar, and no tall hit area over its controls)
        self.assertIn(".rhandle:focus-visible { outline: 3px solid var(--gold); outline-offset: 2px; }", self.page)
        self.assertIn(".rhandle[hidden]", self.page)                                                         # (no markup of the handle shows while the attribute is there)
        self.assertNotIn("rzone", self.page)                                                                 # (the hot zone is a rectangle that the script reads: no element lies over the picture)

    def test_a_hidden_bar_is_not_there_at_all(self):
        rule = re.search(r"\.rbar\.over\.gone \{([^}]*)\}", self.page).group(1)
        for text in ("opacity: 0;", "visibility: hidden;", "pointer-events: none;", "transform: translateX(-50%) translateY(14px);"):
            self.assertIn(text, rule)                                                                        # (visibility: no focus and no screen reader reads it)

    def test_reduced_motion_and_forced_colors_have_their_rules(self):
        self.assertIn("@media (prefers-reduced-motion: reduce) { .rbar.over, .rbar.over.gone { transition: none; } }", self.page)
        self.assertRegex(self.page, r"@media \(forced-colors: active\) \{ \.rhandle \{ color: ButtonText; background: ButtonFace; border-color: ButtonText; \}")


class TheGlue(unittest.TestCase):
    page = read("web", "shell.html")
    glue = page[page.index("<!-- BEGIN replay glue"):page.index("<!-- END replay glue")]

    def test_the_states_are_the_state_machines_and_the_zone_reads_the_picture_not_an_element(self):
        self.assertIn("var zone = R.barZone({", self.glue)
        self.assertIn("zone: function () { var c = $('canvas'), r = c.getBoundingClientRect(); return r.width ? { left: r.left, right: r.right, bottom: r.bottom, height: r.height, rows: c.height } : null; }", self.glue)
        self.assertIn("busy: function () { return dragging || !$('picpop').hidden; }", self.glue)

    def test_a_hidden_bar_is_not_woken_by_the_mouse_a_key_or_a_click(self):
        self.assertIn("if (bar.classList.contains('over') && zone.mode() === 'hidden') { syncEdge(); return; }", self.glue)           # (wake is what every move, key and press calls)
        self.assertIn("zone.mode() === 'open' && get(VALUE.state) === STATE.playing) idleTimer = setTimeout(hideBar, 3000);", self.glue)   # (a peek has no idle fade of its own: the leave time governs)
        self.assertIn("if (!bar.classList.contains('over') || zone.mode() !== 'open') return;", self.glue)

    def test_the_chevron_the_swipe_and_the_b_key_hide_and_the_handle_and_the_key_show(self):
        for line in ("$('b-tab').addEventListener('click', function () { zone.hide(); });",
                     "$('b-handle').addEventListener('click', function () { showBar(false); });",
                     "{ barFrom = null; zone.hide(); swiped = ev.pointerId; }",
                     "function toggleBar() { if (zone.mode() === 'hidden') showBar(false); else zone.hide(); }",
                     "if ((ev.key === 'b' || ev.key === 'B' || ev.code === 'KeyB') && $('rbar').classList.contains('over') && !ev.ctrlKey && !ev.altKey && !ev.metaKey && !/^(INPUT|TEXTAREA|SELECT)$/.test((ev.target && ev.target.tagName) || '') && !document.querySelector('[role=\"dialog\"]:not([hidden]), [role=\"alertdialog\"]:not([hidden])') && $('picpop').hidden) {",
                     "ev.preventDefault(); ev.stopImmediatePropagation(); if (!ev.repeat) toggleBar(); return;"):
            self.assertIn(line, self.glue, line)

    def test_the_chevron_and_the_handle_are_part_of_the_bar_and_a_finger_swipes_up_on_the_handle(self):
        for line in ("bar: function () { var a = $('rbar').getBoundingClientRect(), t = $('b-tab').getBoundingClientRect(), h = $('b-handle').hidden ? null : $('b-handle').getBoundingClientRect(); return { left: Math.min(a.left, t.left, h ? h.left : a.left), right: Math.max(a.right, t.right, h ? h.right : a.right), top: Math.min(a.top, t.top, h ? h.top : a.top), bottom: Math.max(a.bottom, h ? h.bottom : a.bottom) }; },",
                     "if (handleFrom && ev.pointerId === handleFrom.id && handleFrom.y - ev.clientY >= EDGE_SWIPE) { handleFrom = null; showBar(false); }",
                     "if (touchSeen && !noteShown) { noteShown = true; $('rnote').textContent = 'Tap the handle, or swipe it up, to bring the bar back'; $('rnote').hidden = false;"):
            self.assertIn(line, self.glue, line)

    def test_focus_follows_the_state(self):
        self.assertIn("function showBar(focus) { var from = document.activeElement === $('b-handle'); zone.open(); if (focus || from) $('b-tab').focus(); }", self.glue)
        self.assertIn("if (bar.contains(document.activeElement)) $('b-handle').focus();", self.glue)

    def test_a_finger_never_peeks_and_the_strip_is_not_for_a_hidden_bar(self):
        self.assertIn("if (ev.pointerType !== 'touch' && !isLocked() && $('rbar').classList.contains('over')) zone.pointer(ev.clientX, ev.clientY, ev.buttons);", self.glue)
        self.assertIn("bar.classList.contains('idle') && !gone);", self.glue)
        self.assertIn("zone.settle(); wake(); }", self.glue)                                                  # (fullscreen ended: a peek goes back)

    def test_the_locked_cursor_feeds_the_zone_before_the_bar_takes_it(self):
        line = [l for l in self.page.split("\n") if "window.antsReplayZone(" in l and "barLock.move(e.buttons)" in l]
        self.assertEqual(len(line), 1)
        self.assertLess(line[0].index("window.antsReplayZone("), line[0].index("barLock.move(e.buttons)"))
        self.assertIn("window.antsReplayZone = function (x, y, buttons) {", self.glue)

    def test_a_hidden_bar_is_no_target_for_the_locked_cursor(self):
        self.assertIn("bar.classList.contains('idle') || bar.classList.contains('gone')) return null;", read("web", "replay_page.js"))

    def test_the_game_page_line_ceiling_is_the_pages_length(self):
        lines = read("web", "shell.html").count("\n")
        ceiling = int(re.search(r'"web/shell.html": (\d+)', read("tools", "check_hygiene.py")).group(1))
        self.assertLessEqual(lines, ceiling)
        self.assertGreater(lines, ceiling * 0.99)                                                            # (a ceiling that is far above the page is no ceiling)


class TheDocs(unittest.TestCase):
    def test_the_docs_say_how_to_hide_and_call_the_bar(self):
        text = read("docs", "REPLAYS.md")
        for words in ("Hiding the bar", "or the **B** key: the bar is not there at all", "handle", "150 ms", "700 ms", "48 px"):
            self.assertIn(words, text)


@unittest.skipUnless(NODE, "node is not installed: the state machine of the bar's hiding was NOT run (tests/scripts/web_replay_bar_check.js)")
class TheStateMachine(unittest.TestCase):
    def test_the_check_passes(self):
        done = subprocess.run([NODE, os.path.join(REPO, CHECK), os.path.join(REPO, "web", "replay_page.js")], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)


@unittest.skipUnless(NODE, "node is not installed: the mutants of the bar's hiding were NOT run (tests/data/replay_bar_hide_mutants.json)")
class TheChecksAreNotVacuous(unittest.TestCase):
    """the mutants of tests/data/replay_bar_hide_mutants.json on a COPY of the files, one at a time: each must make its test fail, and the copy with nothing changed must pass"""
    FILES = ("web/replay_page.js", "web/shell.html", "docs/REPLAYS.md", "tools/check_hygiene.py", "tests/scripts/web_replay_bar_check.js", "tests/scripts/test_web_replay_bar.py")
    COMMAND = "python3 -m unittest tests.scripts.test_web_replay_bar.TheStyleAndTheMarkup tests.scripts.test_web_replay_bar.TheGlue tests.scripts.test_web_replay_bar.TheStateMachine tests.scripts.test_web_replay_bar.TheDocs"

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, "tests", "data", "replay_bar_hide_mutants.json"), encoding="utf-8") as f:
            cls.spec = json.load(f)
        cls.folder = tempfile.mkdtemp(prefix="ants_bar_mutants.")
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
        self.assertGreaterEqual(len(ids), 20)
        for m in self.spec["mutants"]:
            self.assertTrue(m.get("why"), m["id"] + ": no reason")

    def test_the_copy_with_nothing_changed_passes(self):
        status, out = self.run_command(self.COMMAND)
        self.assertEqual(status, 0, out[-2000:])

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
