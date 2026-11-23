#!/usr/bin/env python3
"""The activity curve over the timeline of the replay page (run by ./run_tests.sh --fast and by the CI): web/replay_activity.js and web/replay_activity.css, and the record handler of web/history_page.js. docs/REPLAYS.md
"The activity curve" says what it shows and where the data comes from (the replay pass, ants_replay/match_stats.hpp, ants_server/history_action.hpp).

  - the code is RUN (node, when it is installed): the field as the server writes it, damaged fields, the deaths-only curve of a record that has no action, the weights, the buckets, the curve and its label, the curve on
    the page's timeline (a computer: nothing at rest and on while the pointer is over the timeline; a touch screen: faint at rest and solid while a finger is on it), the played part, the page's call and the record handler
    of AntsHistory.replayStats, on a STRICT fake of the DOM (tests/scripts/web_activity_check.js), and again on broken copies of the two scripts, which the check must refuse;
  - what no run in the quick tier can show is read from the files, and each test says which defect it catches: a number that the page and the C++ side must agree on (the weights are the Auto camera's, the field's limits
    are the server's), and the curve being hidden at rest on a computer (that is the stylesheet's rule, which needs a real browser to run).
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_activity_check.js")
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_check(activity_path, history_path=None):
    return subprocess.run([NODE, CHECK_JS, activity_path, history_path or os.path.join(REPO, "web", "history_page.js")], capture_output=True, text=True, timeout=120)


@unittest.skipUnless(NODE, "node is not installed: the activity curve was NOT run (tests/scripts/web_activity_check.js)")
class TheCurve(unittest.TestCase):
    def test_the_curve_is_built_and_shown_from_the_data_of_a_record(self):
        done = run_check(os.path.join(REPO, "web", "replay_activity.js"))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"^ok: \d+ checks")

    MUTANTS = {
        # reading the field
        "another version is read": ("if (a.v !== 1 || !whole(a.s, BASE, MAX_SECONDS)", "if (!whole(a.s, BASE, MAX_SECONDS)"),
        "a bucket that is not a multiple of 5 seconds is read": (" || a.s % BASE !== 0", ""),
        "a bucket of any length is read": ("!whole(a.s, BASE, MAX_SECONDS)", "!whole(a.s, BASE, 1e12)"),
        "any number of buckets is read": ("!whole(a.n, 1, MAX_BUCKETS)", "!whole(a.n, 1, 1e9)"),
        "no bucket is a field": ("!whole(a.n, 1, MAX_BUCKETS)", "!whole(a.n, 0, MAX_BUCKETS)"),
        "a kind that is not as long as the buckets is read": ("if (typeof text !== 'string' || text.length !== a.n) return null;", "if (typeof text !== 'string') return null;"),
        "a kind that is not text is read": ("if (typeof text !== 'string' || text.length !== a.n) return null;", "if (text.length !== a.n) return null;"),
        "a letter that is not one of the 64 is a count": ("                if (v < 0) return null;\n                list.push(v);", "                list.push(v);"),
        # the weights and the buckets
        "a death weighs less": ("death: 14,", "death: 13,"),
        "a bomb weighs less": ("bomb: 10,", "bomb: 9,"),
        "a theft weighs less": ("theft: 6,", "theft: 5,"),
        "a fire weighs less": ("fire: 5,", "fire: 4,"),
        "a hit weighs less": ("hit: 2 }", "hit: 1 }"),
        "the hits are not counted": ("for (var k = 0; k < KINDS.length; k++) w += WEIGHTS", "for (var k = 0; k < KINDS.length - 1; k++) w += WEIGHTS"),
        "the buckets double too late": ("while (Math.ceil(base / f) > MAX_BUCKETS) f *= 2;", "while (Math.ceil(base / f) > MAX_BUCKETS + 1) f *= 2;"),
        "a bucket of 5 seconds is rounded down": ("Math.max(1, Math.ceil(total / BASE))", "Math.max(1, Math.floor(total / BASE))"),
        "the buckets never double": ("while (Math.ceil(base / f) > MAX_BUCKETS) f *= 2;", "while (false) f *= 2;"),
        # the deaths alone
        "the deaths of a record with no action are not drawn": ("if (!s) s = deathsOf(rec.deaths, total || 5);", ""),
        "the deaths win over the action": ("var s = actionOf(rec.action);\n        if (!s) s = deathsOf(rec.deaths, total || 5);", "var s = deathsOf(rec.deaths, total || 5);\n        if (!s) s = actionOf(rec.action);"),
        "deaths in squares are drawn": ("d.v !== 1 || d.k !== 'x' || typeof d.d", "d.v !== 1 || typeof d.d"),
        "any number of ants is read": (" || d.d.length > 9 * MAX_ANTS) return null;", ") return null;"),
        "a death is put in the wrong bucket": ("Math.floor((a + 64 * b + 4096 * c) / sh.seconds)", "Math.floor((a + 32 * b + 4096 * c) / sh.seconds)"),
        "a death after the end has no bucket": ("Math.min(sh.buckets - 1, Math.floor(", "(Math.floor("),
        "a bucket of deaths is not capped": ("death[bucket] = Math.min(MAX_COUNT, death[bucket] + 1);", "death[bucket] = death[bucket] + 1;"),
        "a letter that is not one of the 64 is a second": ("if (a < 0 || b < 0 || c < 0) return null;", ""),
        "a field of zeros is a curve": ("        if (!any) return null;\n        return { seconds: s.seconds", "        return { seconds: s.seconds"),
        "the match's seconds are not taken from the turns": ("Math.floor(rec.turns / 20)", "rec.turns"),
        "a match with no seconds has no length": ("if (!(total > 0)) total = s.buckets * s.seconds;", ""),
        # the curve and its label
        "a peak is a needle": ("var KERNEL = [3, 2, 1],", "var KERNEL = [1],"),
        "the height grows as fast as the weight": ("POWER = 0.8,", "POWER = 1,"),
        "the highest place touches the top": ("TOP = 12,", "TOP = 0,"),
        "the line overshoots": ("m.push(d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2);", "m.push((d[i - 1] + d[i]) / 2);"),
        "the dot is not on the line": ("(t3 - t2) * h * m[k + 1]", "(t3 - t2) * h * m[k]"),
        "the last bucket reaches past the end": ("WIDTH * Math.min((i + 0.5) * series.seconds, last) / last", "WIDTH * ((i + 0.5) * series.seconds) / last"),
        "a count of one is plural": ("NAMES[kind][n === 1 ? 0 : 1]", "NAMES[kind][n === 1 ? 1 : 0]"),
        "two hits are a fight": ("FIGHT = 3,", "FIGHT = 2,"),
        "a record of deaths alone says quiet": ("series.deathsOnly ? 'no deaths' : series.counts.hit[i]", "series.counts.hit[i]"),
        "a moment after the end is not the end": ("Math.max(0, Math.min(series.total, seconds))", "Math.max(0, seconds)"),
        "a moment after the end has no bucket": ("i = Math.min(series.buckets - 1, Math.floor(t / series.seconds))", "i = Math.floor(t / series.seconds)"),
        "the label says the fires before the deaths": ("var NAMES = { death: ['death', 'deaths'], bomb: ['bomb', 'bombs'], theft: ['theft', 'thefts'], fire: ['fire', 'fires'] };", "var NAMES = { fire: ['fire', 'fires'], death: ['death', 'deaths'], bomb: ['bomb', 'bombs'], theft: ['theft', 'thefts'] };"),
        # the page: shown on a computer only while the pointer is over the timeline, on a touch screen faint at rest and solid under a finger
        "a computer shows it at rest": ("else if (touching()) heat.classList.add('faint');", "else heat.classList.add('faint');"),
        "a touch screen shows nothing at rest": ("else if (touching()) heat.classList.add('faint');", "else if (false) heat.classList.add('faint');"),
        "a hover shows nothing": ("var live = pressed || inside;", "var live = pressed;"),
        "a finger that comes near is a hover": ("function mouseLike(ev) { return ev.pointerType !== 'touch'; }", "function mouseLike(ev) { return true; }"),
        "a pointer that leaves leaves it on": ("inside = false; paint(); });", "paint(); });"),
        "a finger that is lifted leaves it on": ("function release() { pressed = false; paint(); }", "function release() { paint(); }"),
        "a press shows nothing": ("{ pressed = true; place(ev.clientX); paint(); }", "{ pressed = false; place(ev.clientX); paint(); }"),
        "a move with no pointer on the timeline is followed": ("if (pressed || (inside && mouseLike(ev))) place(ev.clientX);", "place(ev.clientX);"),
        "the label leaves the bar": ("tip.style.left = Math.max(half - 11, Math.min(w + 11 - half, x)) + 'px';", "tip.style.left = x + 'px';"),
        "a pointer beyond the rail is beyond the match": ("x = Math.max(0, Math.min(w, clientX - r.left))", "x = clientX - r.left"),
        "the tag stays under the curve": ("if (live) env.bar.classList.add('heathov'); else", "if (false) env.bar.classList.add('heathov'); else"),
        "the bar does not say it has a curve": ("if (env.bar) env.bar.classList.add('has-activity');", ""),
        "the curve is read aloud": ("heat.setAttribute('aria-hidden', 'true');", ""),
        "a rail with no width divides by zero": ("w = r.width > 0 ? r.width : 1,", "w = r.width,"),
        "the part played is half": ("var p = Math.max(0, Math.min(100, typeof percent === 'number' && isFinite(percent) ? percent : 0)) * WIDTH / 100;", "var p = Math.max(0, Math.min(100, typeof percent === 'number' && isFinite(percent) ? percent : 0)) * WIDTH / 200;"),
        "the part played is not kept in the page": ("rp.setAttribute('width', String(p));", ""),
        "a percent beyond the end is believed": ("var p = Math.max(0, Math.min(100, typeof", "var p = Math.max(0, Math.min(1000, typeof"),
        "a text is a percent": ("typeof percent === 'number' && isFinite(percent) ? percent : 0", "isFinite(percent) ? Number(percent) : 0"),
        "a page that cannot say is a touch screen": ("function touching() { try { return !!(env.touch && env.touch()); } catch (e) { return false; } }", "function touching() { try { return !!(env.touch && env.touch()); } catch (e) { return true; } }"),
        # the page's call
        "the handle is not kept where the page's paint finds it": ("win.antsReplayCurve = handle;", ""),
        "the page is not told that the bar got taller": ("bar.offsetHeight !== before && win.Event", "false && win.Event"),
        "a finger on the page is not a touch screen": ("!!win.antsFinger || !!win.matchMedia", "!!win.matchMedia"),
        "a screen with hover is a touch screen": ("win.matchMedia('(hover: none)')", "win.matchMedia('(hover: hover)')"),
    }

    def test_the_check_can_fail(self):
        text = read("web", "replay_activity.js")
        with tempfile.TemporaryDirectory() as folder:
            shutil.copy(os.path.join(REPO, "web", "history_page.js"), os.path.join(folder, "history_page.js"))
            for why, (old, new) in self.MUTANTS.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "replay_activity.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                done = run_check(path, os.path.join(folder, "history_page.js"))
                self.assertNotIn("Cannot find module", done.stdout + done.stderr, why)
                self.assertNotEqual(done.returncode, 0, "the check accepted a script where " + why + "\n" + done.stdout[-400:])


@unittest.skipUnless(NODE, "node is not installed: the record handler was NOT run (tests/scripts/web_activity_check.js)")
class TheRecordHandler(unittest.TestCase):
    """AntsHistory.replayStats (web/history_page.js) gives the record of the match on show to the page's handler, and a handler that throws costs the panel nothing"""
    MUTANTS = {
        "the handler is not given the record": ("env.onRecord(rec);", ""),
        "a handler that throws is heard": ("catch (e) { /* the curve is one more thing: whatever it does, the panel is made */ }", "catch (e) { throw e; }"),
        "a record of another match is given": ("if (rec && typeof rec === 'object' && rec.id === id && Array.isArray(rec.seats)) {", "if (rec && typeof rec === 'object' && Array.isArray(rec.seats)) {"),
        "the panel waits for a handler": ("mount(rec);\n            }", "if (typeof env.onRecord === 'function') mount(rec);\n            }"),
    }

    def test_the_check_can_fail(self):
        text = read("web", "history_page.js")
        with tempfile.TemporaryDirectory() as folder:
            for why, (old, new) in self.MUTANTS.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "history_page.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                done = run_check(os.path.join(REPO, "web", "replay_activity.js"), path)
                self.assertNotIn("Cannot find module", done.stdout + done.stderr, why)
                self.assertNotEqual(done.returncode, 0, "the check accepted a page script where " + why + "\n" + done.stdout[-400:])


class TheNumbersTheTwoSidesShare(unittest.TestCase):
    """The page and the C++ side must agree on these numbers, and no run of one can see the other: a weight that is not the Auto camera's puts the curve's hills where the camera does not go; a limit that is not the
    server's makes the page refuse a field that the server wrote (or believe one that the server never writes)."""
    script = read("web", "replay_activity.js")

    def test_the_weights_are_the_auto_cameras(self):
        camera = read("include", "ants_app", "auto_camera.hpp")
        table = dict(re.findall(r"(\w+): (\d+)", re.search(r"var WEIGHTS = \{([^}]*)\}", self.script).group(1)))
        for kind, name in (("death", "kDeath"), ("bomb", "kBomb"), ("theft", "kTheft"), ("fire", "kFire"), ("hit", "kHit")):
            weight = re.search(r"static constexpr float %s = (\d+(?:\.\d+)?)f;" % name, camera)
            self.assertIsNotNone(weight, name)
            self.assertEqual(float(table[kind]), float(weight.group(1)), kind)

    def test_the_limits_of_the_field_are_the_servers(self):
        header = read("include", "ants_server", "history_action.hpp")
        number = lambda name: int(re.search(r"%s = ([\d]+)u?;" % name, header).group(1))
        self.assertEqual(int(re.search(r"MAX_BUCKETS = (\d+)", self.script).group(1)), number("kActionMaxBuckets"))
        self.assertEqual(int(re.search(r"BASE = (\d+)", self.script).group(1)), number("kActionBaseSeconds"))
        self.assertEqual(int(re.search(r"MAX_COUNT = (\d+)", self.script).group(1)), number("kActionMaxCount"))
        self.assertIn("kActionMaxSeconds = 5u << 20;", header)
        self.assertIn("MAX_SECONDS = 5 * 1048576", self.script)
        letters = re.search(r"kAlphabet\[\] = \"([^\"]*)\"", read("src", "ants_server", "history_action.cpp")).group(1)
        self.assertEqual(re.search(r"var LETTERS = '([^']*)'", self.script).group(1), letters)
        kinds = re.findall(r"MatchStats::Action::(\w+)", re.search(r"kKinds\[[^\]]*\] = \{([^}]*)\}", read("src", "ants_server", "history_action.cpp")).group(1))
        self.assertEqual([k.lower() for k in kinds], re.findall(r"'(\w+)'", re.search(r"var KINDS = \[([^\]]*)\]", self.script).group(1)))

    def test_a_computer_shows_nothing_at_rest_and_the_curve_never_takes_the_pointer(self):
        """the curve is hidden until the script says `on` or `faint` (a computer at rest shows nothing: the pointer rule is in the script and run above, but whether the class has an effect is the stylesheet's)"""
        css = read("web", "replay_activity.css")
        base = re.search(r"\n\.heat \{([^}]*)\}", css).group(1)
        self.assertIn("opacity: 0;", base)
        self.assertIn("pointer-events: none;", base)
        self.assertRegex(css, r"\.heat\.on \{ opacity: 1; \}")
        self.assertRegex(css, r"\.heat\.faint \{ opacity: \.\d+; \}")


class TheFiles(unittest.TestCase):
    """Nothing that a record carries becomes markup, and the page and the build carry the two files (the generic tests of test_web_image_files.py check the Dockerfile, the CI and build_web.sh for every file that a page loads)"""
    script = read("web", "replay_activity.js")
    shell = read("web", "shell.html")

    def test_nothing_that_a_record_carries_becomes_markup(self):
        for bad in (".innerHTML", "document.write", "insertAdjacentHTML", "outerHTML", "eval(", "new Function", "setAttribute('style'", "setAttribute('href'", "setAttribute('src'", "location.", "fetch("):
            self.assertNotIn(bad, self.script, bad)
        self.assertEqual(set(re.findall(r"https?://[^'\" ]+", self.script)) - {"http://www.w3.org/2000/svg"}, set())

    def test_the_game_page_loads_the_stylesheet_and_the_script_after_the_helpers_they_use(self):
        """a page that does not load them has no curve and nothing else notices (the real page runs in a browser only in the opt-in checks)"""
        self.assertIn('<link rel="stylesheet" href="replay_activity.css">', self.shell)
        self.assertIn('<script src="replay_activity.js"></script>', self.shell)
        self.assertLess(self.shell.index('<script src="history_page.js"></script>'), self.shell.index('<script src="replay_activity.js"></script>'))


if __name__ == "__main__":
    unittest.main()
