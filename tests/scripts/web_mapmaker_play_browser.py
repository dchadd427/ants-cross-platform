"""The browser part "play" of tests/scripts/web_mapmaker_browser.py (docs/MAP_EDITOR.md, "Play this map"): a test game is started from the map maker in the real browser, against one bot at least, and
the game is really played for a while; then the way back.

  * desktop 1280 x 800, TINY opened from the disk: the gold button is the first of the top bar; the start dialog (four colors, five answers each, you at Green and a Medium bot at Red); Start opens a
             tab of its own, and the game page in it takes the open file's bytes from the map maker's tab and writes them where the game looks: they are TINY's own bytes, byte for byte; the game goes
             through its loading screen and the quick help and starts on the game's own START (the roster is Green and Red, and each has its ants), the time on the screen moves (the clock is read
             from two pictures), the page makes no call that counts a game; a reload of the game tab starts the same game again; "Back to editor" asks once ("Keep playing" stays), then the tab
             closes and the map maker says that you are back;
  * drawn    the map is drawn on (one stroke, so there is a history and the one slot of unsaved work holds the bytes), then played with you at Red, Blue a Hard bot and Black an Easy one: the bytes that the
             game gets are the bytes of the slot (the map maker's own file as it is, not a copy that a script wrote again), the roster is Red, Blue and Black; back in the editor the view, the tool,
             the check line, the unsaved work and the history are as they were: Undo takes the stroke back and the file is TINY again, byte for byte (what Download hands over);
  * control  the same page counts a game that was not started from the map maker (a normal game on this computer asks the server to count it), so the check of "not counted" can see a request;
  * refusal  a blank map (no color has a hill) is refused in plain words, a blank Extended map of 100 x 100 is refused for its colors and not for its size (an Extended map plays up to 512 x 512), with OK only; a browser
             that does not open the tab is told so, and nothing is kept;
  * phone    390 x 844, a touch screen: Menu has "Play this map" (gold), the sheet's answers and buttons are 44 pixels, inside the screen, no sideways scroll; a tap starts the game in its tab, whose
             strip has a 44 pixel way back and does not scroll sideways, and the way back closes it.
"""
import base64
import hashlib
import json
import os
import sys
import time
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_aspect_check import Tab                                           # noqa: E402

SEATS_PROBES = (20, 21, 22, 23, 24)            # the game's ants_probe: the roster mask, then the ants alive of Green, Red, Blue, Black


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pages_of(browser_dt):
    return [t for t in browser_dt.call("Target.getTargets")["targetInfos"] if t["type"] == "page"]


def attach(browser_dt, info):
    """a Tab-like object for a tab that the page opened itself (window.open): the same calls as a Tab that the check made"""
    t = Tab.__new__(Tab)
    t.dt, t.target = browser_dt, info["targetId"]
    t.session = browser_dt.call("Target.attachToTarget", {"targetId": t.target, "flatten": True})["sessionId"]
    t.call("Page.enable")
    t.call("Runtime.enable")
    t.call("Emulation.setFocusEmulationEnabled", {"enabled": True})
    return t


def game_tab(mod, browser_dt, known, size, mobile=False, timeout=20):
    """the tab that the map maker opened (a page of play.html with a testmap in its address), attached and shaped like the window of the check; None when none came"""
    found = []

    def look():
        found[:] = [t for t in pages_of(browser_dt) if "play.html?testmap=" in t["url"] and t["targetId"] not in known]
        return bool(found)
    if not mod.wait_for(look, timeout, 0.2):
        return None
    tab = attach(browser_dt, found[0])
    tab.emulate(size[0], size[1], 2 if mobile else 1, mobile=mobile, touch=mobile)
    return tab


def wait_ready(mod, tab, timeout=60):
    return mod.wait_for(lambda: tab.ev("!!window.isReadyToPlay"), timeout, 0.3)


def probes(tab, keys):
    return [tab.ev("Module._ants_probe(%d)" % k) for k in keys]


def start_the_game(mod, tab):
    """the game's own START on the quick help (the page's picture is a canvas: the button is at the lower right of the picture's box); then wait for the dialog that opens the match to be gone"""
    time.sleep(1.5)                      # (the quick help is drawn a moment after the game is ready)
    box = json.loads(tab.ev("JSON.stringify((function () { var r = document.getElementById('game-container').getBoundingClientRect(); return {x: r.left, y: r.top, w: r.width, h: r.height}; })())"))
    ok = False
    for height in (0.90, 0.94, 0.90, 0.94, 0.90):        # (where START is depends on the picture's shape and the page's size: 0.90 of the canvas's height now, 0.94 before; a click that misses it does nothing, and the next one is made only while the match does not run)
        tab.click(box["x"] + box["w"] * 0.93, box["y"] + box["h"] * height)
        ok = mod.wait_for(lambda: tab.ev("Module._ants_match_running()") == 1, 5, 0.3)
        if ok:
            break
    return ok and mod.wait_for(lambda: tab.ev("Module._ants_probe(5)") == 0, 25, 0.3), box


def clock_picture(tab, box):
    """a picture of the top band of the game's screen (the time, the names and the scores: only the time moves in a game that nobody plays), as text: it differs when the time has moved (the picture is
    not as wide as the canvas, so the clock is not in a corner of the canvas but of the picture)"""
    clip = {"x": box["x"], "y": box["y"], "width": max(box["w"], 40), "height": max(box["h"] * 0.06, 14), "scale": 1}
    return tab.call("Page.captureScreenshot", {"format": "png", "clip": clip})["data"]


def game_file(tab):
    """(digest, length) of the map file in the game's own file system, from the page of the game"""
    got = tab.ev("(function () { var d = Module.FS.readFile('Original-Ants/Maps/playtest.lvl'), s = '', i; for (i = 0; i < d.length; i += 4096) s += String.fromCharCode.apply(null, d.subarray(i, i + 4096)); return btoa(s); })()")
    data = base64.b64decode(got)
    return sha(data), len(data)


def ask_back(mod, tab, check, what):
    """the way back: the strip's button asks once, Keep playing stays, Back to editor closes the tab"""
    tab.ev("document.querySelector('#test-strip button').click()")
    check(mod.wait_for(lambda: tab.ev("!!document.getElementById('test-leave')"), 5, 0.1) and "Leave the test game?" in tab.ev("document.getElementById('test-leave').textContent"), what + ": the way back asks once")
    tab.ev("document.getElementById('test-leave-keep').click()")
    check(tab.ev("!document.getElementById('test-leave') && !!document.getElementById('test-strip')"), what + ": Keep playing stays in the game")
    tab.ev("document.querySelector('#test-strip button').click()")
    mod.wait_for(lambda: tab.ev("!!document.getElementById('test-leave')"), 5, 0.1)


def press_back(mod, tab, browser_dt, check, what):
    gone = tab.target
    tab.ev("setTimeout(function () { document.getElementById('test-leave-out').click(); }, 0); 1")
    check(mod.wait_for(lambda: gone not in [t["targetId"] for t in pages_of(browser_dt)], 10, 0.2), what + ": Back to editor closes the game's tab")


def dialog_button(page, text):
    page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent.indexOf(%s) >= 0; })[0].click()" % json.dumps(text))


def choose(page, name, value):
    """a click on an answer of a color's row, as a person does (the radio under the label is the click's target)"""
    page.click_selector(".pseat:nth-child(%d) .pc:nth-child(%d)" % (["Black", "Blue", "Green", "Red"].index(name) + 1, ["y", "e", "m", "h", "n"].index(value) + 1))


def leave_unsaved(page, mod):
    """a map that was drawn on asks before another takes its place"""
    if mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg') && document.querySelector('.dlg h2').textContent === 'Leave this map?'"), 2, 0.1):
        dialog_button(page, "Leave without saving")


def work_slot(page, mod):
    """the bytes that the browser's one slot of unsaved work holds (the open map's own bytes), as (digest, length), or None"""
    page.ev("window.__slot = undefined; (function () { var q = indexedDB.open('ants-mapmaker', 1); q.onerror = function () { window.__slot = null; }; q.onsuccess = function () { var g = q.result.transaction('work', 'readonly').objectStore('work').get('current'); "
            "g.onsuccess = function () { var r = g.result; if (!r) { window.__slot = null; return; } var s = '', b = r.bytes, i; for (i = 0; i < b.length; i += 4096) s += String.fromCharCode.apply(null, b.subarray(i, i + 4096)); window.__slot = btoa(s); }; g.onerror = function () { window.__slot = null; }; }; })()")
    mod.wait_for(lambda: page.ev("window.__slot !== undefined"), 5, 0.05)
    got = page.ev("window.__slot")
    if not got:
        return None
    data = base64.b64decode(got)
    return sha(data), len(data)


def view_state(page):
    state = page.state()
    state.pop("where")                                      # (where the pointer is: not the map's)
    return json.dumps(state, sort_keys=True) + page.ev("[].map.call(document.querySelectorAll('[data-tool]'), function (b) { return b.getAttribute('aria-pressed'); }).join(',') + '|' + document.getElementById('undo-btn').disabled + '|' + document.getElementById('redo-btn').disabled")


def run_play(page, tab, args, check, mod, shots_prefix="mapmaker-play"):
    """desktop, the drawn map, the control and the refusals (one tab of the check; the games open tabs of their own)"""
    dt = tab.dt
    print("[web map maker] Play this map, 1280 x 800")
    tiny_path = os.path.join(mod.REPO, "Original-Ants", "Maps", "TINY.LVL")
    with open(tiny_path, "rb") as f:
        tiny_bytes = f.read()
    check(page.open() and page.ready(), "the page starts")
    page.ev(mod.EDGES_JS)
    page.choose_file(tiny_path)
    check(mod.wait_for(lambda: page.state()["name"] == "TINY"), "TINY is opened")
    first = json.loads(page.ev("JSON.stringify([].map.call(document.querySelectorAll('.edmenu.dsk .btn'), function (b) { return [b.textContent, b.className, b.disabled]; }))"))
    check(first[0][0].endswith("Play this map") and "gold" in first[0][1] and not first[0][2] and len(first) == 9, "the gold button is the first of the nine in the top bar, and it is on: %s" % first[0])
    page.shot(args.shots, shots_prefix + "-desktop-button")
    # a dialog that is open for a moment: its seats and its answers
    page.press_act("play")
    check(mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg.play')"), 15), "Play this map opens the start dialog (the script is loaded the first time)")
    check(page.ev("document.querySelector('.dlg h2').textContent") == "Play this map" and "A test game on TINY (31 x 31) as it is now. Nothing is saved and no score is kept." in page.ev("document.querySelector('.play-lead').textContent"), "it says what this is")
    rows = json.loads(page.ev("JSON.stringify([].map.call(document.querySelectorAll('.pseat'), function (r) { return [r.querySelector('.pn').textContent, r.querySelector('.pf').textContent, [].map.call(r.querySelectorAll('.pc input'), function (i) { return i.checked ? i.value : ''; }).join('')]; }))"))
    check([r[0] for r in rows] == ["Black", "Blue", "Green", "Red"] and all(r[1] == "3 ants, 1 hill" for r in rows) and [r[2] for r in rows] == ["n", "n", "y", "m"], "four colors with their ants and hills, you at Green and a Medium bot at Red: %s" % rows)
    small = page.ev("[].filter.call(document.querySelectorAll('.dlg.play .pc, .dlg.play .btn'), function (n) { return n.getBoundingClientRect().height < 43.5; }).length")
    check(small == 0, "every answer and button of the dialog is at least 44 pixels high (%s are not)" % small)
    page.shot(args.shots, shots_prefix + "-desktop-dialog")
    known = {t["targetId"] for t in pages_of(dt)}
    work_slot(page, mod)
    dialog_button(page, "Start the game")
    game = game_tab(mod, dt, known, (1280, 800))
    check(game is not None, "Start opens a tab of its own at the game page")
    if game is None:
        return
    check(not page.ev("!!document.querySelector('.dlg')"), "the dialog is gone from the map maker")
    requests = []

    def seen(msg, game=game):
        if msg.get("method") == "Network.requestWillBeSent" and msg.get("sessionId") == game.session:
            requests.append((msg["params"]["request"].get("method"), msg["params"]["request"].get("url")))
    dt.handlers.append(seen)
    game.call("Network.enable")
    check(wait_ready(mod, game), "the game page loads the game")
    url = game.ev("location.href")
    check(url.endswith("&seats=ymnn") and "/play.html?testmap=" in url and "TINY" not in url and "mapmaker" not in url, "its address holds an id and four letters, nothing of the map: %s" % url)
    time.sleep(1.5)
    strip = game.ev("document.getElementById('test-strip') && document.getElementById('test-strip').textContent")
    check(strip == "◀ Back to editorTest gameTINY · You: Green · Bots: Red MediumNot saved. Not counted.", "the strip says what this is: %r" % strip)
    h = json.loads(game.ev("JSON.stringify((function () { var s = document.getElementById('test-strip'), b = s.querySelector('button').getBoundingClientRect(); return {bar: s.getBoundingClientRect().top, back: b.height, sideways: document.documentElement.scrollWidth > innerWidth}; })())"))
    check(h["bar"] <= 20 and h["back"] >= 43.5 and not h["sideways"], "the strip is on top, the way back is 44 pixels, no sideways scroll: %s" % h)
    digest, length = game_file(game)
    check(digest == sha(tiny_bytes) and length == len(tiny_bytes), "the game's file system holds TINY's own bytes (%d bytes, byte for byte)" % length)
    check(game.ev("Module.arguments.slice(1, 8).join(' ')") == "--map Original-Ants/Maps/playtest.lvl --play --player 0 --bot 1:medium" and game.ev("Module.arguments.indexOf('--alone') < 0"), "the game was told its map, to play, and its bot")
    ok, box = start_the_game(mod, game)
    check(ok, "the game goes through its quick help and START, and the match runs")
    check(probes(game, SEATS_PROBES)[0] == 3 and probes(game, SEATS_PROBES)[1:3] == [3, 3] and probes(game, SEATS_PROBES)[3:] == [0, 0], "Green and Red play, each with its ants, nobody else (roster, ants alive): %s" % probes(game, SEATS_PROBES))
    one, two = clock_picture(game, box), None
    time.sleep(3.0)
    two = clock_picture(game, box)
    check(one != two, "the time on the game's screen moves: the ticks advance")
    page_shot = args.shots
    game.save_shot(page_shot, shots_prefix + "-desktop-game")
    check(not any(m == "POST" and "/stats/local" in u for m, u in requests), "the game page did not ask the server to count this game (%d requests seen)" % len(requests))
    # a reload starts the same game again
    game.call("Page.reload")
    time.sleep(1)
    check(wait_ready(mod, game) and game.ev("!!document.getElementById('test-strip')") and game_file(game)[0] == sha(tiny_bytes), "a reload of the game's tab starts the same game on the same map again")
    # the way back
    ask_back(mod, game, check, "desktop")
    game.save_shot(page_shot, shots_prefix + "-desktop-leave")
    press_back(mod, game, dt, check, "desktop")
    check(mod.wait_for(lambda: page.ev("!!document.getElementById('play-toast')"), 5, 0.1) and page.ev("document.getElementById('play-toast').textContent") == "Back from the test game. Your map is as you left it.", "the map maker says that you are back")
    page.shot(args.shots, shots_prefix + "-desktop-back")
    time.sleep(0.3)
    # the control: a game that is not a test game asks the server to count it, and the instrument sees that
    known = {t["targetId"] for t in pages_of(dt)}
    ctl = Tab(types.SimpleNamespace(devtools=dt))
    ctl.emulate(1280, 800, 1)
    seen_control = []

    def seen2(msg, ctl=ctl):
        if msg.get("method") == "Network.requestWillBeSent" and msg.get("sessionId") == ctl.session:
            seen_control.append((msg["params"]["request"].get("method"), msg["params"]["request"].get("url")))
    dt.handlers.append(seen2)
    ctl.call("Network.enable")
    ctl.open(args.web.rstrip("/") + "/play.html?map=tiny&bots=easy", settle=1)
    ok2, box2 = start_the_game(mod, ctl)
    mod.wait_for(lambda: any(m == "POST" and "/stats/local" in u for m, u in seen_control), 8, 0.2)
    check(ok2 and any(m == "POST" and "/stats/local" in u for m, u in seen_control), "control: a normal game on this computer does ask the server to count it (started %s, %d requests seen: %s)" % (ok2, len(seen_control), [u[-40:] for m, u in seen_control][-4:]))
    dt.handlers.remove(seen2)
    dt.handlers.remove(seen)
    ctl.close()

    # a game tab that no map maker opened (a typed address, or the map maker's tab is gone): a card with a way to the map maker, not a game
    orphan = Tab(types.SimpleNamespace(devtools=dt))
    orphan.emulate(1280, 800, 1)
    orphan.call("Page.navigate", {"url": args.web.rstrip("/") + "/play.html?testmap=abcdef123456&seats=ymnn"})
    check(mod.wait_for(lambda: orphan.ev("!!document.getElementById('load-failure')"), 20, 0.3) and "This test game cannot start" in orphan.ev("document.getElementById('load-failure').textContent")
          and not orphan.ev("!!document.getElementById('test-strip')"), "a game tab that no map maker opened shows a card, with no strip and no game")
    orphan.save_shot(args.shots, shots_prefix + "-desktop-no-opener")
    orphan.ev("setTimeout(function () { document.querySelector('#load-failure button').click(); }, 0); 1")
    check(mod.wait_for(lambda: orphan.ev("location.pathname") == "/mapmaker.html", 15, 0.3), "and its button goes to the map maker")
    orphan.close()

    # ---- the drawn map: a stroke, the unsaved work, the history
    print("[web map maker] Play this map: a map that was drawn on")
    page.tool("ground")
    x, y = page.cell_point(15, 15)
    page.tab.click(x, y)
    check(mod.wait_for(lambda: not page.button_off("#undo-btn")), "one stroke is drawn: there is a history")
    check(mod.wait_for(lambda: work_slot(page, mod) is not None, 6, 0.3), "and the browser's one slot of unsaved work holds the map")
    mod.wait_for(lambda: page.state()["check"] != "Checking\u2026", 6, 0.2)
    slot_before = work_slot(page, mod)
    state_before = view_state(page)
    page.press_act("play")
    check(mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg.play')"), 10), "the dialog opens on the drawn map")
    choose(page, "Red", "y")
    choose(page, "Blue", "h")
    choose(page, "Black", "e")
    codes = page.ev("[].map.call(document.querySelectorAll('.pseat'), function (r) { return r.querySelector('.pn').textContent[0] + [].map.call(r.querySelectorAll('.pc input'), function (i) { return i.checked ? i.value : ''; }).join(''); }).join(' ')")
    check(codes == "Be Bh Gn Ry", "you at Red, a Hard bot at Blue and an Easy one at Black: %s" % codes)
    check(page.ev("document.querySelector('.play-status').textContent") == "You and 2 bots.", "the dialog says so")
    known = {t["targetId"] for t in pages_of(dt)}
    dialog_button(page, "Start the game")
    game = game_tab(mod, dt, known, (1280, 800))
    check(game is not None and game.target is not None, "the second game opens its own tab")
    if game is None:
        return
    check(wait_ready(mod, game), "the game page loads the game")
    url = game.ev("location.href")
    check(url.endswith("&seats=nyhe"), "the letters are in the game's order: Green nobody, Red you, Blue Hard, Black Easy (%s)" % url)
    digest, length = game_file(game)
    check(slot_before is not None and (digest, length) == slot_before and digest != sha(tiny_bytes), "the game got the bytes that the map maker holds for the open map (the slot's own bytes, %d), not TINY's and not a new writing" % length)
    ok, box = start_the_game(mod, game)
    check(ok, "the match runs")
    check(probes(game, SEATS_PROBES) == [14, 0, 3, 3, 3], "Red, Blue and Black play, Green has no ants: %s" % probes(game, SEATS_PROBES))
    strip = game.ev("document.getElementById('test-strip').textContent")
    check(strip == "◀ Back to editorTest gameTINY · You: Red · Bots: Black Easy, Blue HardNot saved. Not counted.", "the strip names the colors: %r" % strip)
    ask_back(mod, game, check, "drawn")
    press_back(mod, game, dt, check, "drawn")
    time.sleep(0.5)
    state_after = view_state(page)
    check(state_after == state_before, "back in the editor the view, the tool, the check line and the history buttons are as they were (%s | %s)" % (state_before, state_after))
    check(work_slot(page, mod) == slot_before, "and the unsaved work is the same bytes")
    check(not page.button_off("#undo-btn") and page.button_off("#redo-btn"), "Undo is still there")
    page.ev("document.getElementById('undo-btn').click()")
    check(mod.wait_for(lambda: page.button_off("#undo-btn")), "Undo takes the stroke back")
    got = mod.edges_bytes(page)
    check(got["sha"] == sha(tiny_bytes), "and the file is TINY again, byte for byte")
    page.ev("document.getElementById('redo-btn').click()")

    # ---- refusals
    print("[web map maker] Play this map: the refusals")
    page.ev("(function () { var t = document.getElementById('play-toast'); if (t) t.parentNode.removeChild(t); })()")
    page.press_act("new")
    mod.wait_for(lambda: page.ev("!!document.querySelector('.kinds') || !!document.querySelector('.dlg')"), 10)
    leave_unsaved(page, mod)
    mod.wait_for(lambda: page.ev("!!document.querySelector('.kinds')"), 10)
    dialog_button(page, "Make map")
    mod.wait_for(lambda: page.state()["name"] == "My map" and not page.ev("!!document.querySelector('.dlg')"), 10)
    page.press_act("play")
    check(mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg h2')"), 10) and page.ev("document.querySelector('.dlg h2').textContent") == "This map cannot be played yet", "a blank map is refused")
    text = page.ev("document.querySelector('.dlg-in').textContent")
    check("two colors that each have a hill and starting ants" in text and "Green has no hill" in text and "Black has no starting ants" in text and not page.ev("!!document.querySelector('.pseat')"), "in plain words, with the check's own sentences and no seats")
    check([b for b in page.ev("[].map.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent; })")] == ["OK"], "with OK only")
    page.shot(args.shots, shots_prefix + "-desktop-refusal-blank")
    dialog_button(page, "OK")
    mod.make_extended(page, 100, 100)
    page.press_act("play")
    # (an Extended map plays up to 512 x 512: a blank one of 100 x 100 is refused for its colors, as a blank Classic map is, not for its size; no Extended map that the editor makes is bigger than the game plays)
    check(mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg h2')"), 10) and page.ev("document.querySelector('.dlg h2').textContent") == "This map cannot be played yet", "a blank Extended map of 100 x 100 is refused")
    text = page.ev("document.querySelector('.dlg-in').textContent")
    check("bigger than the game can play" not in text and "Green has no hill" in text and "Black has no starting ants" in text, "for its colors, not for its size: the check's own sentences are there and nothing about a map that is too big")
    names = page.ev("[].map.call(document.querySelectorAll('.dlg-foot .btn'), function (b) { return b.textContent; })")
    check(names == ["OK"] and not page.ev("!!document.querySelector('.pseat')"), "with OK only (no way to a Classic copy, which is for a map that is too big), and no Start: %s" % names)
    page.shot(args.shots, shots_prefix + "-desktop-refusal-extended")
    dialog_button(page, "OK")
    # a browser that does not open the tab
    page.choose_file(tiny_path)
    leave_unsaved(page, mod)
    mod.wait_for(lambda: page.state()["name"] == "TINY", 10)
    page.ev("window.open = function () { return null; }")
    page.press_act("play")
    mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg.play')"), 10)
    dialog_button(page, "Start the game")
    check(mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg h2') && document.querySelector('.dlg h2').textContent === 'The test game could not open'"), 5) and "Allow pop-ups" in page.ev("document.querySelector('.dlg-in').textContent"), "a browser that does not open the tab is told so, in plain words")
    page.key("Escape")


def run_play_phone(page, tab, args, check, mod, shots_prefix="mapmaker-play"):
    dt = tab.dt
    print("[web map maker] Play this map, 390 x 844 touch")
    tiny_path = os.path.join(mod.REPO, "Original-Ants", "Maps", "TINY.LVL")
    tab.emulate(390, 844, 2, mobile=True)
    check(page.open() and page.ready(), "the page starts")
    page.choose_file(tiny_path)
    check(mod.wait_for(lambda: page.state()["name"] == "TINY"), "TINY is opened")
    page.press_act("menu")
    check(mod.wait_for(lambda: page.ev("!!document.querySelector('.menulist')"), 5), "the Menu opens")
    items = json.loads(page.ev("JSON.stringify([].map.call(document.querySelectorAll('.menulist .btn'), function (b) { var r = b.getBoundingClientRect(); return [b.textContent, b.className, r.height]; }))"))
    check(items[-1][0].endswith("Play this map") and "gold" in items[-1][1] and all(i[2] >= 43.5 for i in items), "Play this map is the last entry of the Menu, gold, and every entry is 44 pixels: %s" % items[-1])
    page.shot(args.shots, shots_prefix + "-phone-menu")
    page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent.indexOf('Play this map') >= 0; })[0].click()")
    check(mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg.play')"), 15), "the start dialog opens")
    time.sleep(0.4)
    got = json.loads(page.ev("""JSON.stringify((function () { var d = document.querySelector('.dlg.play').getBoundingClientRect(), out = [];
        [].forEach.call(document.querySelectorAll('.dlg.play .pc, .dlg.play .btn'), function (n) { var b = n.getBoundingClientRect(); if (b.height < 43.5) out.push('small ' + n.textContent); if (b.left < -0.5 || b.right > innerWidth + 0.5) out.push('outside ' + n.textContent); });
        return {inside: d.left >= 0 && d.right <= innerWidth + 0.5 && d.bottom <= innerHeight + 0.5, out: out, sideways: document.documentElement.scrollWidth > innerWidth}; })())"""))
    check(got["inside"] and not got["out"] and not got["sideways"], "the sheet, its answers and its buttons are inside the screen, 44 pixels, no sideways scroll (%s)" % got)
    page.shot(args.shots, shots_prefix + "-phone-dialog")
    known = {t["targetId"] for t in pages_of(dt)}
    b = page.box(".dlg-foot .btn:last-child")
    page.tab.tap(b["x"] + b["w"] / 2, b["y"] + b["h"] / 2)
    game = game_tab(mod, dt, known, (390, 844), mobile=True)
    check(game is not None, "a tap on Start opens the game in a tab of its own")
    if game is None:
        return
    check(wait_ready(mod, game), "the game page loads the game")
    time.sleep(1.5)
    h = json.loads(game.ev("JSON.stringify((function () { var s = document.getElementById('test-strip'), b = s.querySelector('button').getBoundingClientRect(), r = s.getBoundingClientRect(); return {back: b.height, left: r.left, right: r.right, w: innerWidth, sideways: document.documentElement.scrollWidth > innerWidth, lines: s.children.length}; })())"))
    check(h["back"] >= 43.5 and h["left"] >= 0 and h["right"] <= h["w"] + 0.5 and not h["sideways"], "the strip fits the phone, its way back is 44 pixels, no sideways scroll: %s" % h)
    game.save_shot(args.shots, shots_prefix + "-phone-game")
    ask_back(mod, game, check, "phone")
    game.save_shot(args.shots, shots_prefix + "-phone-leave")
    press_back(mod, game, dt, check, "phone")
    check(mod.wait_for(lambda: page.ev("!!document.getElementById('play-toast')"), 5, 0.1), "back in the map maker, a line says so")
    page.shot(args.shots, shots_prefix + "-phone-back")
    # the refusal on a phone: Menu, New, an Extended map of 100 x 100
    page.ev("(function () { var t = document.getElementById('play-toast'); if (t) t.parentNode.removeChild(t); })()")
    page.press_act("menu")
    mod.wait_for(lambda: page.ev("!!document.querySelector('.menulist')"), 5)
    page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent === 'New'; })[0].click()")
    mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg h2')"), 5)
    leave_unsaved(page, mod)
    mod.wait_for(lambda: page.ev("!!document.querySelector('.kinds')"), 5)
    page.ev("document.querySelectorAll('.kind input')[1].click()")
    page.type_into("#size-rows", "100")
    page.type_into("#size-columns", "100")
    dialog_button(page, "Make map")
    mod.wait_for(lambda: page.state()["rows"] == "100", 15)
    page.press_act("menu")
    mod.wait_for(lambda: page.ev("!!document.querySelector('.menulist')"), 5)
    page.ev("[].filter.call(document.querySelectorAll('.menulist .btn'), function (b) { return b.textContent.indexOf('Play this map') >= 0; })[0].click()")
    check(mod.wait_for(lambda: page.ev("!!document.querySelector('.dlg h2')"), 10) and page.ev("document.querySelector('.dlg h2').textContent") == "This map cannot be played yet" and "bigger than the game can play" not in page.ev("document.querySelector('.dlg-in').textContent"), "a blank Extended map of 100 x 100 is refused on a phone too, for its colors")
    small = page.ev("[].filter.call(document.querySelectorAll('.dlg-foot .btn'), function (n) { return n.getBoundingClientRect().height < 43.5; }).length")
    check(small == 0 and not page.ev("document.documentElement.scrollWidth > innerWidth"), "its buttons are 44 pixels, no sideways scroll")
    page.shot(args.shots, shots_prefix + "-phone-refusal")
