#!/usr/bin/env python3
"""The map maker's Connect an AI (run by ./run_tests.sh --fast, with the other python tests of tests/scripts, and by the CI; docs/MAP_AI.md): the tools that a person's AI may call on the open map, and the sheet,
the strip and the socket that let it.

  - the code is RUN (node, when it is installed): the table of tools against the hub's own (hub/aitools.py), looking at a map, changing it as one step of the history at a time, taking turns with the
    person, the sheet on a fake page and the socket (tests/scripts/web_mapmaker_ai_check.js);
  - the checks are shown able to fail: broken copies of the two scripts (tests/data/mapmaker_ai_mutants.json, which tools/mutate.py runs too, with `--keep-going --jobs 1`) are each refused by them;
  - what the code must never do is read from the source, because no run can show a call that is never made: nothing here writes markup, stores, evaluates or asks any address but the hub's key pages, its
    socket and the skills of the site (a defect that a run cannot see is the one call that nobody thought of); the page's side is the button, the menu entry, the lazy loading in the order that it needs and
    the map maker's version.
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
FRONT = os.path.join(REPO, "web", "front")
CHECK = os.path.join(REPO, "tests", "scripts", "web_mapmaker_ai_check.js")
NODE = shutil.which("node")
PARTS = ("mapmaker_ai.js", "mapmaker_ai_tools.js")
WATCHDOG_S = 60


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def without_comments(text):
    return re.sub(r"(?m)^\s*//.*$|\s//\s.*$", "", text)


def run_check(front=None):
    args = [NODE] + (["--require", os.path.join(REPO, "tests", "scripts", "web_mapmaker_bail.js")] if front else []) + [CHECK] + (["--front", front] if front else [])
    env = dict(os.environ)
    if front:
        env["ANTS_BAIL_WATCHDOG_S"] = str(WATCHDOG_S)
    return subprocess.run(args, capture_output=True, text=True, timeout=300 if not front else 3 * WATCHDOG_S, env=env)


@unittest.skipUnless(NODE, "node is not installed")
class TheAiChecks(unittest.TestCase):
    def test_the_checks_pass(self):
        done = run_check()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"ai check: \d+ checks passed")
        for part in ("A the table of tools", "B looking", "C changing", "D the sheet, the strip and the socket"):
            self.assertIn("RUNNING: " + part, done.stdout)

    def test_the_tools_of_the_page_are_the_tools_of_the_hub(self):
        sys.path.insert(0, REPO)
        from hub import aitools
        done = subprocess.run([NODE, CHECK, "--tools", ",".join(sorted(aitools.TOOLS))], capture_output=True, text=True, timeout=300)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        extra = subprocess.run([NODE, CHECK, "--tools", ",".join(sorted(aitools.TOOLS) + ["one_more"])], capture_output=True, text=True, timeout=300)
        self.assertNotEqual(extra.returncode, 0, "a tool that the page does not have was not noticed")


@unittest.skipUnless(NODE, "node is not installed")
class TheChecksAreNotVacuous(unittest.TestCase):
    def test_the_checks_can_fail_on_the_broken_copies(self):
        """tests/data/mapmaker_ai_mutants.json (tools/mutate.py runs it too): each broken copy must fail the check"""
        with open(os.path.join(REPO, "tests", "data", "mapmaker_ai_mutants.json"), encoding="utf-8") as f:
            spec = json.load(f)
        ids = [m["id"] for m in spec["mutants"]]
        self.assertEqual(len(ids), len(set(ids)), "an id twice")
        self.assertGreaterEqual(len(ids), 20)
        with tempfile.TemporaryDirectory() as tmp:
            for name in os.listdir(FRONT):
                if name.endswith(".js"):
                    shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
            for m in spec["mutants"]:
                with self.subTest(m["id"]):
                    name = os.path.basename(m["file"])
                    text = read(*m["file"].split("/"))
                    self.assertEqual(text.count(m["old"]), 1, "%s: the text is not in %s exactly once" % (m["id"], name))
                    self.assertEqual(m["test"], "node tests/scripts/web_mapmaker_ai_check.js")
                    with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="") as f:
                        f.write(text.replace(m["old"], m["new"]))
                    try:
                        done = run_check(tmp)
                    finally:
                        shutil.copy(os.path.join(FRONT, name), os.path.join(tmp, name))
                    self.assertNotEqual(done.returncode, 0, "the check did not notice: " + m["id"] + "\n" + done.stdout[-600:] + done.stderr[-600:])
                    if "WATCHDOG:" in done.stderr:
                        self.assertRegex(done.stdout, r"\b(FAILED|ERROR|CRASHED): ", "the run hung with no failing line of the check for " + m["id"])


class ThePagesOwnCode(unittest.TestCase):
    def test_nothing_writes_markup_stores_evaluates_or_asks_an_address_that_it_has_no_business_with(self):
        # A defect that no run shows: the one call or write that nobody thought of. A name, a note or a sentence that comes from outside is only ever put in with textContent, a key is only ever in memory
        # while it is shown, and the only addresses are the hub's key pages and socket, the login page and the skills of the site.
        allowed = {"/hub/ai/keys", "/hub/ai/keys/revoke", "/hub/ai/ws", "/ai/skills/index.json", "/hub/login?next=mapmaker", "/ai/skills/all.zip"}
        for name in PARTS:
            text = without_comments(read("web", "front", name))
            for bad in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "localStorage", "sessionStorage", "indexedDB", "XMLHttpRequest", "sendBeacon", "importScripts", "postMessage", "window.open", "location.href", "location.assign", "location.replace", "createContextualFragment", "srcdoc"):
                self.assertNotIn(bad, text, "%s uses %s" % (name, bad))
            for literal in re.findall(r"['\"](/[A-Za-z0-9_./?=-]*)['\"]", text):
                if literal.startswith(("/hub/", "/ai/")):
                    self.assertTrue(literal in allowed or literal.startswith(("/ai/skills/", "/hub/ai/")), "%s names the address %s" % (name, literal))
            self.assertNotRegex(text, r"https?://", name + " names another site")
        tools = without_comments(read("web", "front", "mapmaker_ai_tools.js"))
        self.assertNotIn("fetch(", tools, "the tools ask the page for what they fetch (env.fetchText, env.fetchMap), never an address of their own")
        self.assertIn("'/ai/skills/' + skill + '/SKILL.md'", tools, "a guide is a file of the skills, by a name from a fixed table")

    def test_the_page_has_the_button_the_menu_entry_and_the_loading_in_the_order_it_needs(self):
        html, app, css = read("web", "mapmaker.html"), read("web", "front", "mapmaker_app.js"), read("web", "front", "mapmaker.css")
        self.assertIn('data-act="ai"', html)
        self.assertIn('class="btn sm ghost aik-btn dsk"', html, "the button is for a window with room; a phone has the File menu")
        for name in PARTS + ("mapmaker_ai.css",):
            self.assertNotIn(name, html, "a lazily loaded file has no tag in the page")
        self.assertIn("{ text: 'Connect an AI...', onClick: ACTIONS.ai }", app)
        self.assertLess(app.index("t.src = 'front/mapmaker_ai_tools.js';"), app.index("p.src = 'front/mapmaker_ai.js';"), "the tools come before the part that needs them")
        self.assertIn("if (aiPart) aiPart.touch();", app, "every change of the map is counted, whoever made it")
        self.assertIn(".aik-btn", css)
        version = read("web", "MAPMAKER_VERSION").strip()
        self.assertIn('data-maker="v%s"' % version, html)
        self.assertGreaterEqual(tuple(int(n) for n in version.split(".")), (1, 9, 0), "Connect an AI is the map maker's 1.9.0")

    def test_the_two_hooks_that_the_editor_gives_are_the_only_changes_to_its_scripts(self):
        self.assertIn("top: function () { return lastId(); }", read("web", "front", "mapmaker_edit.js"))
        self.assertIn("editor: function () { return editor; }, changed: changed,", read("web", "front", "mapmaker_tools.js"))


if __name__ == "__main__":
    unittest.main()
