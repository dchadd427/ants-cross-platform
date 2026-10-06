#!/usr/bin/env python3
"""A match played in a REAL browser ends with a "Download replay" button that saves the match (opt-in; see tests/scripts/test_web_replay_game.sh and docs/REPLAYS.md).

The script block of web/shell.html is checked with a fake page (tests/scripts/test_web_replay.py) and the recording with the application's own tests, but only the real page with the real WebAssembly game
shows that the three meet: the game hands the file to the page (a call out of the WebAssembly, EM_JS), the button appears in the bar under the game with the size of the file, a click on it downloads the
file, and `replay_tool` of this tree, a native program, plays the file that the browser saved to the state in which the web game's match ended: every hash that the web engine wrote is the native
engine's too. Needs a running web page (the web image of this tree, or tools/web_without_docker.py), a Chromium-based browser and Python 3; the tool is optional (build the target replay_tool: without it
the checks of the saved file are left out, and the script says so). The DevTools protocol is spoken with the client of web_hidden_check.py, standard library only. What it does, in a throwaway headless
browser (its own profile, port and download folder; nothing of yours is touched):

  * the game page of a game on this computer against one easy bot (play.html?map=tiny&bots=easy,none,none) is loaded and its quick help closed: the match runs, and the button is not there yet;
  * the match runs for a while, is quit (Ctrl+Q, Y) and ends: the bar under the game shows the button, "Download replay (N KB)", with the size of the file that the game made;
  * a click on the button saves a file named after the map and the date, as big as the button said;
  * the file is a replay of a game on this computer on TINY.LVL with the player's name and the bot, `replay_tool verify` plays it to the same state (every hash and the end), and `replay_tool orders` lists
    the bot's orders and the player's quit;
  * in a window the size of a phone's the bar wraps, and the button is still whole and inside the page, with no sideways scroll.
Exit status 0: every check passed; 1: a check failed; 3: the check could not be made because the environment is not there (no browser, nothing answers at the page's address).
"""
import argparse
import base64
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402
from web_aspect_check import Browser, NotReachable, Tab                    # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
BUTTON = "document.getElementById('replay-download')"


def wait_for(condition, timeout=10.0, step=0.1):
    """The condition's value once it is true (a page that is being replaced has no execution context for a moment: that is not true yet), else the last value."""
    def look():
        try:
            return condition()
        except (RuntimeError, TimeoutError):
            return None
    deadline = time.time() + timeout
    value = look()
    while not value and time.time() < deadline:
        time.sleep(step)
        value = look()
    return value


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--tool", default=os.environ.get("ANTS_REPLAY_TOOL", os.path.join(REPO, "build", "replay_tool")), help="the replay_tool program of this tree (default: build/replay_tool)")
    ap.add_argument("--maps", default=os.path.join(REPO, "Original-Ants", "Maps"), help="the maps folder (default: the original's)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--play-seconds", type=float, default=11.0, help="how long the match runs before it is quit (default 11: more than 200 turns, so the file holds two hashes)")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long the page may take to get its game ready (default 120)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    web = args.web if args.web.endswith("/") else args.web + "/"

    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            failures.append(what)

    def note(text):
        print("  note: %s" % text, flush=True)

    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    tool = args.tool if os.path.isfile(args.tool) and os.access(args.tool, os.X_OK) else ""
    downloads = tempfile.mkdtemp(prefix="ants_replay_downloads.")
    browser = None
    print("[web replay] a match in a real browser ends with a Download replay button that saves the match")
    try:
        try:
            urllib.request.urlopen(web, timeout=10).read(64)
        except (OSError, urllib.error.URLError) as e:
            print("  SKIP: nothing answers at %s (%s)" % (web, e))
            return 3
        browser = Browser(path)
        tab = Tab(browser)
        tab.emulate(1440, 900, 1)
        browser.devtools.call("Browser.setDownloadBehavior", {"behavior": "allow", "downloadPath": downloads})
        dialogs = []

        def on_dialog(msg):
            if msg.get("method") == "Page.javascriptDialogOpening" and msg.get("sessionId") == tab.session:
                dialogs.append(msg["params"].get("message", ""))
                try:
                    tab.call("Page.handleJavaScriptDialog", {"accept": True})
                except (RuntimeError, TimeoutError):
                    pass

        tab.dt.handlers.append(on_dialog)

        def key_event(kind, name, code, virtual, modifiers=0, text=""):
            tab.call("Input.dispatchKeyEvent", {"type": kind, "key": name, "code": code, "windowsVirtualKeyCode": virtual, "modifiers": modifiers, "text": text if kind == "keyDown" else ""})

        def key(name, code, virtual, text=""):
            key_event("keyDown", name, code, virtual, text=text)
            key_event("keyUp", name, code, virtual)

        def chord(name, code, virtual):
            """Ctrl and a key, as a keyboard sends them: Ctrl goes down first and comes up last."""
            key_event("keyDown", "Control", "ControlLeft", 17, modifiers=2)
            key_event("keyDown", name, code, virtual, modifiers=2)
            key_event("keyUp", name, code, virtual, modifiers=2)
            key_event("keyUp", "Control", "ControlLeft", 17)

        def shot(name, clip=None):
            if not args.shots:
                return
            os.makedirs(args.shots, exist_ok=True)
            params = {"format": "png"}
            if clip:
                params["clip"] = dict(clip, scale=2)
            with open(os.path.join(args.shots, name + ".png"), "wb") as f:
                f.write(base64.b64decode(tab.call("Page.captureScreenshot", params)["data"]))

        tab.open(web + "play.html?map=tiny&bots=easy,none,none&name=Bob&aspect=16:9", settle=1.0)
        check(tab.ev(BUTTON + " !== null") is True and tab.ev(BUTTON + ".hidden") is True and tab.ev("getComputedStyle(" + BUTTON + ").display") == "none",
              "the game page has the Download replay button, and it is hidden (no room in the bar) while no match has ended")
        tab.ev("document.getElementById('canvas').focus(); 1")
        started = False
        for _ in range(4):                                              # (an Enter that comes while the loading screen is still up only ends that)
            key("Enter", "Enter", 13, text="\r")
            if wait_for(lambda: tab.ev("Module._ants_match_running()") == 1, 2.5):
                started = True
                break
        check(started, "the quick help closes with Enter and the match runs")
        if not started:
            return 1
        check(bool(wait_for(lambda: tab.ev("Module._ants_probe(5)") == 0, 60, 0.5)), "... the \"Get ready\" dialog closes by itself")
        time.sleep(args.play_seconds)
        check(tab.ev("Module._ants_match_running()") == 1 and tab.ev(BUTTON + ".hidden") is True, "the match runs on, and there is still no button while it is not over")

        chord("q", "KeyQ", 81)                                           # (the quit question)
        time.sleep(0.5)
        key("y", "KeyY", 89, text="y")
        shown = wait_for(lambda: tab.ev(BUTTON + ".hidden") is False, 30)
        check(bool(shown), "Ctrl+Q and Y end the match, and the button is shown")
        if not shown:
            shot("replay_no_button")
            return 1
        label = tab.ev(BUTTON + ".textContent")
        size_match = re.match(r"^Download replay \((\d+) (bytes|KB)\)$", label)
        check(size_match is not None, "the button says what it will save, with the size of the file (%r)" % label)
        offered = tab.ev("antsReplayFile ? antsReplayFile.bytes.length : -1")
        name = tab.ev("antsReplayFile ? antsReplayFile.name : ''")
        check(isinstance(offered, int) and offered > 100, "the game handed the page a file of %s bytes" % offered)
        check(re.match(r"^ants-TINY-\d{8}-\d{6}\.antsrep$", name or "") is not None, "... named after the map and the date and time of this computer (%r)" % name)
        where = tab.ev("(function () { var r = %s.getBoundingClientRect(); var c = document.getElementById('canvas').getBoundingClientRect(); var b = document.getElementById('view-bar').getBoundingClientRect(); "
                       "return JSON.stringify({ x: r.left + r.width / 2, y: r.top + r.height / 2, below: r.top >= c.bottom - 1, inbar: r.top >= b.top - 1 && r.bottom <= b.bottom + 1, w: r.width, h: r.height, "
                       "bar: { x: b.left, y: b.top, width: b.width, height: b.height } }); })()" % BUTTON)
        where = json.loads(where)
        check(where["below"] and where["inbar"] and where["w"] > 60 and where["h"] > 10, "the button sits in the bar under the game, and has a size you can click (%dx%d)" % (where["w"], where["h"]))
        shot("replay_button_on_the_page")
        shot("replay_button_in_the_bar", clip=where["bar"])

        tab.click(where["x"], where["y"])
        saved = wait_for(lambda: [f for f in glob.glob(os.path.join(downloads, "*")) if not f.endswith(".crdownload")], 20)
        check(bool(saved) and len(saved) == 1, "a click on the button saves one file (%s)" % ([os.path.basename(f) for f in saved] if saved else "nothing"))
        if saved:
            file = saved[0]
            check(os.path.basename(file) == name, "... under the name that the page offered (%s)" % os.path.basename(file))
            check(os.path.getsize(file) == offered, "... as big as the game's file (%d bytes)" % os.path.getsize(file))
            check(not dialogs, "the page showed no dialog while this went on (%s)" % dialogs)
            if tool:
                def run(command, *more):
                    words = [tool, command, file] + list(more)
                    if command != "info":                                    # (info needs no map)
                        words += ["--maps-dir", args.maps]
                    return subprocess.run(words, capture_output=True, text=True, timeout=300)
                info = run("info")
                check(info.returncode == 0 and "TINY.LVL" in info.stdout and "local game" in info.stdout and "Bob" in info.stdout and "Bot (Easy)" in info.stdout,
                      "replay_tool info reads it: TINY.LVL, a local game, Bob and Bot (Easy) (exit %d)" % info.returncode)
                print("    " + "\n    ".join(info.stdout.strip().splitlines()[:12]))
                verify = run("verify")
                check(verify.returncode == 0, "replay_tool verify plays it here to the state in which the web game's match ended: every hash and the end agree (exit %d: %s)" %
                      (verify.returncode, (verify.stdout + verify.stderr).strip().splitlines()[-1] if (verify.stdout + verify.stderr).strip() else ""))
                orders = run("orders")
                lines = orders.stdout.strip().splitlines()
                check(orders.returncode == 0 and any("Bot (Easy)" in l and "move" in l for l in lines) and any("Bob" in l and "quit" in l for l in lines),
                      "replay_tool orders lists the bot's moves and the player's quit (exit %d, %d lines)" % (orders.returncode, len(lines)))
                print("    " + "\n    ".join(lines[:4] + ["..."] + lines[-3:]))
            else:
                note("no replay_tool program at %s (build the target replay_tool): the saved file was not played" % args.tool)

        tab.emulate(390, 844, 3, mobile=True)                           # a phone, upright: the bar wraps, and the button must still be whole and inside the page
        time.sleep(1.5)
        phone = json.loads(tab.ev("(function () { var r = %s.getBoundingClientRect(); return JSON.stringify({ left: r.left, right: r.right, width: r.width, height: r.height, hidden: %s.hidden, "
                                  "inner: window.innerWidth, scroll: document.documentElement.scrollWidth }); })()" % (BUTTON, BUTTON)))
        check(not phone["hidden"] and phone["left"] >= 0 and phone["right"] <= phone["inner"] and phone["scroll"] <= phone["inner"] + 1,
              "on a phone (%d px wide) the button is whole and inside the page, with no sideways scroll (%s)" % (phone["inner"], phone))
        shot("replay_button_on_a_phone")
    except NotReachable as e:
        print("  SKIP: %s" % e)
        return 3
    except Exception as e:                                      # noqa: BLE001  (a page that hangs or crashes is a failure, never a skip)
        print("  FAIL: the check broke down: %s: %s" % (type(e).__name__, e))
        return 1
    finally:
        if browser is not None:
            browser.close()
        shutil.rmtree(downloads, ignore_errors=True)
    print("[web replay] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
