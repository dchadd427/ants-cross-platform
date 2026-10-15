#!/usr/bin/env python3
"""The chat of the front page in REAL browsers (opt-in; see tests/scripts/test_web_lobby_chat.sh and docs/PLAY_IN_BROWSER.md "Chat in the lobby").

Needs the web image of this tree served by nginx (tools/web_without_docker.py builds and serves it without Docker), the native `ants_server` of this tree, a Chromium-based browser and Python 3. The
check starts the server (with the options of docker-compose.stack.yml), puts a front door before the site (/ws goes to the server, everything else to the site), and drives the front page like people:
each person is a browser of their own, a room is made by the first page and joined by the others through its link. Parts (--only NAME):

  * desktop  three people in one room on a 1280 x 900 window: the box is under the colours, with the hint to send the link while one is alone; a person who joins is a small line of the system ("Bea
             joined as Red.") and a joiner sees the hint to say hello; lines typed with real key events come back from the room to everybody, in the same order, in the colour of the seat and under
             the name; a name or a line that is markup is shown as the characters that it is (no element is made, nothing runs); a character that the game cannot show is left out as it is typed, with the
             hint; six lines at once: five go, the page keeps the sixth and says one a second, and it goes a second later, nobody is thrown out; a line of 100 characters stays inside the box; a line that
             arrives while the box is below the window is also the page's toast, one that arrives while it is in view is not; START takes every page to the game page;
  * phone    a 390 x 844 phone: the box is after the colours and before the map, the page has no sideways scroll, the field is 16 px or more and a finger tall, the START bar does not cover the field
             when the keyboard has shrunk the window (the browser scrolls the field into view), the same sideways (844 x 390), and a line that is typed there reaches the others.
--shots DIR saves the pictures that the check looks at.

Exit status 0: every check passed; 1: a check failed or something broke; 3: the environment is not there; 2: a bad command line.
"""
import argparse
import os
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from web_hidden_check import find_browser, free_port                          # noqa: E402
from web_rejoin_check import Player, Server, wait_for, REPO                    # noqa: E402
from web_map_fetch_check import Front                                           # noqa: E402

PARTS = ("desktop", "phone")
LOG = "Array.prototype.map.call(document.querySelectorAll('#chat-log > li'), function (li) { return { cls: li.className, c: li.getAttribute('data-c'), text: li.textContent }; })"


class Visitor(Player):
    """A person at the front page."""

    def phone(self, width=390, height=844):
        self.call("Emulation.setDeviceMetricsOverride", {"width": width, "height": height, "deviceScaleFactor": 2, "mobile": True, "screenWidth": width, "screenHeight": height})

    def desktop(self, width=1280, height=900):
        self.resize(width, height)

    def open_front(self, web, name="Ann"):
        """Opens the front page as a person who has given this name before (the page makes its room under the name that the browser remembers)."""
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": "try { window.localStorage.setItem('ants.name', %r); } catch (e) { }" % name})
        self.call("Page.navigate", {"url": web})
        deadline = time.time() + 60
        while time.time() < deadline:
            time.sleep(0.4)
            if wait_for(lambda: self.ev("!!document.getElementById('code') && document.getElementById('code').textContent.length > 0 && !document.getElementById('start').disabled"), 1.0):
                return True
        return False

    def room_code(self):
        return (self.ev("document.getElementById('code').textContent") or "").replace(" ", "")

    def join(self, web, code, name):
        """Opens the room's link and answers the name card with `name`."""
        self.call("Page.navigate", {"url": "%s?room=%s" % (web, code)})
        if not wait_for(lambda: self.ev("!document.getElementById('name-step').hidden"), 30, 0.2):
            return False
        self.ev("(function () { var i = document.getElementById('name-step-input'); i.value = %r; i.dispatchEvent(new Event('input', { bubbles: true })); document.getElementById('name-step-go').click(); })()" % name)
        return bool(wait_for(lambda: self.ev("document.getElementById('name-step').hidden && document.querySelectorAll('#slots .slot.k-person').length >= 2"), 30, 0.2))

    def people(self):
        return self.ev("document.querySelectorAll('#slots .slot.k-person').length") or 0

    def lines(self):
        return self.ev(LOG) or []

    def texts(self):
        return [l["text"] for l in self.lines()]

    def field(self):
        return self.ev("(function () { var i = document.getElementById('chat-input'); return { value: i.value, disabled: i.disabled, placeholder: i.placeholder, focus: document.activeElement === i }; })()")

    def hint(self):
        return self.ev("document.getElementById('chat-hint').textContent")

    def focus_field(self):
        self.ev("document.getElementById('chat-input').focus()")

    def type(self, text):
        self.call("Input.insertText", {"text": text})

    def enter(self):
        params = {"key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "nativeVirtualKeyCode": 13, "text": "\r"}
        self.call("Input.dispatchKeyEvent", dict(params, type="keyDown"))
        self.call("Input.dispatchKeyEvent", dict(params, type="keyUp"))

    def say(self, text):
        self.focus_field()
        self.type(text)
        self.enter()

    def rect(self, selector):
        return self.ev("(function () { var e = document.querySelector(%r); if (!e) return null; var r = e.getBoundingClientRect(); return { top: r.top, bottom: r.bottom, left: r.left, right: r.right, width: r.width, height: r.height }; })()" % selector)

    def toast(self):
        return self.ev("(function () { var t = document.getElementById('toast'); return t && !t.hidden ? t.textContent : ''; })()") or ""

    def scroll_to(self, y):
        self.ev("window.scrollTo(0, %d)" % y)
        time.sleep(0.2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site's nginx, e.g. http://127.0.0.1:19990/ (the web image of this tree; it needs no game server)")
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER_BIN", os.path.join(REPO, "build", "src", "ants_server", "ants_server")), help="the ants_server program of this tree")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (%s)" % ", ".join(PARTS))
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    args = ap.parse_args()
    site = args.web if args.web.endswith("/") else args.web + "/"
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            failures.append(what)

    def wanted(name):
        return not args.only or args.only in name

    chrome = find_browser(args.browser)
    if not chrome:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    if not os.path.isfile(args.server) or not os.access(args.server, os.X_OK):
        print("  SKIP: the game server program is not there (build the target ants_server, or give --server)")
        return 3
    try:
        urllib.request.urlopen(site, timeout=10).read(64)
    except (OSError, urllib.error.URLError) as e:
        print("  SKIP: nothing answers at %s (%s)" % (site, e))
        return 3

    work = tempfile.mkdtemp(prefix="ants_lobby_chat_check.")
    ws_port = free_port()
    server = Server(args.server, os.path.join(REPO, "Original-Ants", "Maps"), ws_port, work)
    front = None
    visitors = []
    try:
        server.start()
        site_port = int(site.split("//", 1)[1].split("/")[0].split(":")[1])
        front = Front(site_port, ws_port, site_port)
        web = "http://127.0.0.1:%d/" % front.port

        def visitor():
            v = Visitor(chrome, "x", 0, web, 60)
            visitors.append(v)
            return v

        if wanted("desktop"):
            print("part desktop", flush=True)
            ann = visitor()
            ann.desktop()
            check(ann.open_front(web), "the front page is up with its room (Ann)")
            check(bool(ann.ev("!document.getElementById('chat').hidden")), "the chat box is there for a person who is alone")
            check(ann.texts() == ["You are the only one here. Send the link above, then say hello."], "alone: the hint says to send the link (%s)" % ann.texts())
            check(ann.field()["disabled"] is False, "and the field is open")
            code = ann.room_code()
            grid = ann.ev("(function () { var c = document.getElementById('chat').getBoundingClientRect(), r = document.querySelector('#lobby > section.panel').getBoundingClientRect(), s = document.querySelector('aside.side').getBoundingClientRect(); return [c.top >= r.bottom - 1, c.top >= s.bottom - 1, c.width > 1000]; })()")
            check(grid == [True, True, True], "a wide window: the box is across the page under the colours and the map (%s)" % grid)
            ann.shot(args.shots, "desktop-alone")
            bea = visitor()
            bea.desktop()
            check(bea.join(web, code, "Bea"), "Bea opens the link and joins")
            check(bool(wait_for(lambda: ann.texts() == ["Bea joined as Red."], 5, 0.1)), "Ann sees a small line of the system: Bea joined as Red (%s)" % ann.texts())
            check(ann.lines()[0]["cls"] == "sys" and ann.lines()[0]["c"] is None, "(a line of the system: in no colour)")
            check(bea.texts() == ["Nothing said yet. Say hello: everyone in this room will see it."], "the joiner sees the hint to say hello (%s)" % bea.texts())
            ann.say("hello Bea")
            check(bool(wait_for(lambda: bea.texts()[-1:] == ["Ann:hello Bea"] and ann.texts()[-1:] == ["Ann:hello Bea"], 5, 0.1)), "Ann's line reaches Bea and comes back to Ann, under her name")
            check(ann.lines()[-1]["c"] == "green" and bea.lines()[-1]["c"] == "green", "in the colour of Ann's seat (Green)")
            check(ann.field()["value"] == "" and ann.field()["focus"] is True, "Ann's field is empty and keeps the focus")
            time.sleep(1.2)
            bea.say("hi Ann")
            check(bool(wait_for(lambda: ann.texts()[-2:] == ["Ann:hello Bea", "Bea:hi Ann"] and bea.texts()[-2:] == ["Ann:hello Bea", "Bea:hi Ann"], 5, 0.1)), "Bea answers: the same two lines in the same order on both screens")
            check(bea.lines()[-1]["c"] == "red", "Bea's line is Red")
            # a person whose name is markup, saying markup
            cy = visitor()
            cy.desktop()
            check(cy.join(web, code, "<b>Cy</b>"), "Cy joins with a name that is markup")
            check(bool(wait_for(lambda: ann.texts()[-1:] == ["<b>Cy</b> joined as Blue."], 5, 0.1)), "the line of the system shows the name as it is (%s)" % ann.texts()[-1:])
            before = ann.ev("document.title")
            cy.say("<img src=x onerror=document.title='pwned'>")
            check(bool(wait_for(lambda: ann.texts()[-1:] == ["<b>Cy</b>:<img src=x onerror=document.title='pwned'>"], 5, 0.1)), "his line is shown as the characters that it is (%s)" % ann.texts()[-1:])
            time.sleep(0.5)
            check(ann.ev("document.querySelectorAll('#chat-log img, #chat-log b, #chat-log i, #chat-log script').length") == 0 and ann.ev("document.title") == before, "no element was made of a name or a line, and nothing ran")
            # what the game cannot show is left out as it is typed
            time.sleep(1.2)
            ann.focus_field()
            ann.type("café ok ☺")
            time.sleep(0.2)
            check(ann.field()["value"] == "caf ok " and "without accents" in (ann.hint() or ""), "an accent and a smiley are left out as they are typed, and the hint says why (%r)" % ann.field()["value"])
            ann.ev("document.getElementById('chat-input').value = ''")
            # the pace: six lines at once: five go, the page keeps the sixth and says so; it goes a second later; nobody is thrown out
            time.sleep(5.5)
            for i in range(1, 7):
                ann.say("fast %d" % i)
            time.sleep(0.6)
            got = [t for t in bea.texts() if t.startswith("Ann:fast")]
            check(got == ["Ann:fast %d" % i for i in range(1, 6)], "six lines at once: five reach the others (%s)" % got)
            check(ann.field()["value"] == "fast 6", "the page keeps the line that it did not send (%r)" % ann.field()["value"])
            check("One line a second" in (ann.hint() or ""), "and says one a second (%r)" % ann.hint())
            check(bool(ann.ev("document.getElementById('banner').hidden")) and ann.people() == 3 and bea.people() == 3, "nobody was thrown out and no strip came up")
            time.sleep(1.3)
            ann.enter()
            check(bool(wait_for(lambda: len([t for t in bea.texts() if t.startswith("Ann:fast")]) == 6, 5, 0.1)), "a second later the kept line goes")
            ann.ev("document.getElementById('chat-input').value = ''")
            # a line of 100 characters stays inside the box
            time.sleep(1.3)
            hundred = ("abcdefghi " * 10)[:99] + "x"
            ann.say(hundred)
            check(bool(wait_for(lambda: bea.texts()[-1:] == ["Ann:" + hundred], 5, 0.1)), "a line of 100 characters comes whole")
            inside = ann.ev("(function () { var l = document.getElementById('chat-log'); return [l.scrollWidth <= l.clientWidth, document.documentElement.scrollWidth <= window.innerWidth]; })()")
            check(inside == [True, True], "and stays inside the box and the page (%s)" % inside)
            time.sleep(1.3)
            ann.say("x" * 99 + "y")
            check(bool(wait_for(lambda: ann.ev("(function () { var l = document.getElementById('chat-log'); return l.scrollHeight - l.scrollTop - l.clientHeight < 40; })()"), 5, 0.1)), "the box follows the newest line")
            ann.shot(args.shots, "desktop-lines")
            # the toast: the box is below the window of a 900 px high screen, so a line that arrives is also the page's toast; with the box in view it is not
            time.sleep(1.5)
            bea.scroll_to(0)
            check(bea.rect("#chat")["top"] > 900 - 140, "the box is below the window at the top of the page (%s)" % bea.rect("#chat")["top"])
            time.sleep(3.2)                                                   # (the toast of the earlier lines has gone)
            ann.say("anybody there?")
            check(bool(wait_for(lambda: bea.toast() == "Ann: anybody there?", 5, 0.1)), "a line that arrives while the box is below the window is also a toast (%r)" % bea.toast())
            bea.ev("document.getElementById('chat').scrollIntoView({ block: 'center' })")
            time.sleep(3.2)
            ann.say("and now?")
            wait_for(lambda: bea.texts()[-1:] == ["Ann:and now?"], 5, 0.1)
            time.sleep(0.3)
            check(bea.toast() == "", "with the box in view there is no toast")
            # START: every page goes to the game page
            ann.scroll_to(0)
            ann.ev("document.getElementById('start').click()")
            went = wait_for(lambda: all("join=" in (v.ev("window.location.search") or "") for v in (ann, bea, cy)), 30, 0.3)
            check(bool(went), "START takes all three pages to the game page")
            bad = [(p.name, t) for p in visitors for _, level, t in p.console if level == "exception"]
            check(not bad, "no page threw an exception (%s)" % bad[:3])
            for v in (ann, bea, cy):
                try:
                    v.close()
                except Exception:                                            # noqa: BLE001
                    pass
            visitors.clear()

        if wanted("phone"):
            print("part phone", flush=True)
            ann = visitor()
            ann.phone()
            check(ann.open_front(web), "the front page is up on a phone (Ann)")
            code = ann.room_code()
            bea = visitor()
            bea.desktop()
            check(bea.join(web, code, "Bea"), "Bea joins from a computer")
            check(bool(wait_for(lambda: ann.people() == 2, 5, 0.1)), "Ann sees her")
            places = ann.ev("(function () { var cards = document.querySelectorAll('#slots .slot'), last = cards[cards.length - 1].getBoundingClientRect(), c = document.getElementById('chat').getBoundingClientRect(), m = document.querySelector('.maprow').getBoundingClientRect(); return [c.top >= last.bottom - 1, m.top >= c.bottom - 1, document.documentElement.scrollWidth <= window.innerWidth, c.width <= window.innerWidth]; })()")
            check(places == [True, True, True, True], "the box is after the colours and before the map, and the page has no sideways scroll (%s)" % places)
            font = ann.ev("(function () { var i = document.getElementById('chat-input'), s = getComputedStyle(i), r = i.getBoundingClientRect(); return [parseFloat(s.fontSize), r.height]; })()")
            check(font[0] >= 16 and font[1] >= 44, "the field is 16 px or more (iOS does not zoom into it) and a finger tall (%s)" % font)
            bea.say("hello from the computer")
            check(bool(wait_for(lambda: ann.texts()[-1:] == ["Bea:hello from the computer"], 5, 0.1)), "a line from the computer reaches the phone")
            ann.shot(args.shots, "phone-lines")
            # the keyboard is up: a browser that resizes the window for it (the worst case) shrinks the window to 520 px; the field that was tapped is scrolled into view, and the START bar must not cover it
            ann.phone(390, 520)
            time.sleep(0.4)
            ann.ev("document.getElementById('chat-input').focus({ preventScroll: true })")
            ann.scroll_to(0)                                                 # (the field is far below the window: the browser brings it in by the nearest edge, the bottom one, which the START bar covers unless the field asks for room)
            ann.ev("document.getElementById('chat-input').scrollIntoView({ block: 'nearest' })")
            time.sleep(0.4)
            seen = ann.ev("(function () { var i = document.getElementById('chat-input').getBoundingClientRect(), g = document.querySelector('.go').getBoundingClientRect(); return { top: i.top, bottom: i.bottom, goTop: g.top, h: window.innerHeight }; })()")
            check(seen["top"] >= 0 and seen["bottom"] <= seen["goTop"] + 0.5, "keyboard up (520 px window): the field is in view and the START bar does not cover it (%s)" % seen)
            ann.type("ready when you are")
            ann.enter()
            check(bool(wait_for(lambda: bea.texts()[-1:] == ["Ann:ready when you are"], 5, 0.1)), "a line typed with the keyboard up reaches the others")
            check(ann.field()["focus"] is True, "and the field keeps the focus (the keyboard stays up)")
            ann.shot(args.shots, "phone-keyboard-up")
            # the phone held sideways with the keyboard up: 844 x 190
            ann.phone(844, 190)
            time.sleep(0.4)
            ann.ev("document.getElementById('chat-input').scrollIntoView({ block: 'nearest' })")
            time.sleep(0.3)
            seen = ann.ev("(function () { var i = document.getElementById('chat-input').getBoundingClientRect(); return { top: i.top, bottom: i.bottom, h: window.innerHeight }; })()")
            check(seen["top"] >= 0 and seen["bottom"] <= seen["h"] + 1, "sideways with the keyboard up (844 x 190): the field is in view (%s)" % seen)
            check(ann.ev("document.documentElement.scrollWidth <= window.innerWidth"), "and the page has no sideways scroll")
            bad = [(p.name, t) for p in visitors for _, level, t in p.console if level == "exception"]
            check(not bad, "no page threw an exception (%s)" % bad[:3])
    except Exception as e:                                                   # noqa: BLE001
        failures.append("the check broke: %r" % (e,))
        print("  FAIL: the check broke: %r (server: %s)" % (e, server.tail()), flush=True)
    finally:
        for v in visitors:
            try:
                v.close()
            except Exception:                                                # noqa: BLE001
                pass
        if front:
            front.stop()
        server.stop()
        shutil.rmtree(work, ignore_errors=True)
    print("%d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
