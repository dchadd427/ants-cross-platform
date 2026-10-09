#!/usr/bin/env python3
"""The front page, the lobby that it is, and the way into and out of a game, in a REAL browser (opt-in; see tests/scripts/test_web_home.sh and docs/NETWORK_PORT.md, "The front page").

The front page is the owner's lobby (web/lobby.html, the pictures he approved; network protocol 16): opening "/" makes a ROOM on the game server and seats the visitor in it (a code of six letters and
numbers, shown as `k7m 2xq`; a link, ?room=<code>, to send on), four colour cards (Black top left, Green top right, Red bottom left, Blue bottom right; a colour that nobody holds is open, a bot of a level or
Nobody; players are dragged between colours), the map and START! at its side, and the footer with the version, the links and the picture's shape (Screen 16:9, which is the default, or 4:3). A visitor who
opens somebody's link, or types a code into "Have a code?", is asked for a name first. START hands every browser that is in the room to the game page, which takes its seat with the key that the lobby page
wrote to the browser's storage; START with nobody else in the match plays a game on this computer. Needs a running web page that has nginx's routes (the web image of this tree: `docker build -t ants-beta .`,
run it on a port), a Chromium-based browser and Python 3, and, because opening "/" makes a room, the site's /ws leading to a game server: --ws-port is the port that it leads to, and the check starts the
native `ants_server` of this tree there (as web_rejoin_check.py does). The DevTools protocol is spoken with the client of web_hidden_check.py, standard library only. Each part opens the site in a throwaway
headless browser (its own profile and port; nothing of yours is touched), the parts with several people in one browser each. Parts:

  * front    the page at "/" on a first visit (a room of its own: a new code of six letters and numbers from the alphabet without look-alikes, the link with it, Copy link, Have a code? and More ways to play;
             the four cards in their places, the host's with You and the pencil, the others open with their drop-down; the map side with its picture, arrows and the Treasure default; START!; the footer's version, build, links
             and notice; Screen with 16:9 the default and 4:3 remembered, a stale or bad value ignored); How it works; Have a code? with its lines for a bad, an own and a code that has no room; the name card of a link to
             a room that is gone (and the room of its own, with the strip that says so); 22 widths from 320 to 1600 px: no sideways scroll, two columns from 1041 px, the cards two by two above 720 px and one under another
             below it, nothing sticking out of a card, the grip's dots clear of the names, START! in view on a phone, the map's name not cut off; Share where the browser has it; the game server that cannot be reached (the gold
             strip, and the room made by itself when it is back); the contrast of all text and of the form controls (4.5:1) in each of these states, at 1440 and 390 px;
  * lobby    one host and guests, each a browser of its own: a guest joins through the link and its name card (a bad name is refused with the reason) and takes the next free colour; the host drags a player onto an
             open colour (a move) and onto a taken one (a swap), or taps the grip and then the colour (Esc lets go); with three players Team 1 and Team 2 appear on each colour that plays, a team goes with its player, a
             third colour on a full team is refused with the reason, and a guest sees the teams as read-only tabs; a bot of a level takes a colour; a player renames themselves with the pencil (a bad name is refused, a name
             with markup stays text); the host's Remove asks first (Keep, Esc, one question at a time) and the removed player gets a room of their own with the strip that says why; a typed code (spaces and capitals do not
             matter) leads through the name card into the room, a code that has no room does not; a reload brings the seat back; the page holds up at 320 px with a name of 32 wide letters and the question open; START
             hands everybody into the real game: every page goes to the game page with its seat's key kept in the browser, the server's status and /busy show the match (three people and a bot, the teams that were set),
             every game runs it, the state hashes agree, and a link opened now gets a room of its own;
  * play     START with a bot in every other colour takes THIS tab to the game page (no new tab): the arguments are the site's door, the room's code, the seat, the name and the shape and nothing else (the server holds the
             plan), the address keeps the code and the shape, the key of the seat is in the browser, and the match starts by itself (no START of the player's, the "Get ready" dialog closes by itself); the server's status
             lists the three bots (Bot (Medium), seats 1 - 3) and the player, the bots' scores, which the HUD shows at the bottom, rise from 0; the game page's Menu link asks first and, with Yes, goes back to the front page
             in the same tab, which makes a room again for the same name;
  * solo     START with nobody else in the match (every other colour Nobody) takes THIS tab to the game page of a game on this computer, a game for one (the map, --alone and the name, no room, no bot): the match runs
             with its one colony and only that: Red, Blue and Black have no hill, no ants and no eggs, at Green; Classic 4:3, chosen under Screen, reaches that game (and 16:9, the default, is what it gets when nothing is chosen);
  * leave    Leave game: a game on this computer against one bot (in each shape of the picture) played to its results with the quit dialog (Ctrl+Q, Yes), and a click on the results' Leave Game button takes THIS tab back
             to the front page (no new tab); the same for a match in a room of the server (START with one Medium bot); the quit dialog's Yes with three opponents left (the quit ends a match only when one is left) takes the tab
             to the front page at once, and the server drops the seat now (a room with nobody left ends); the front page does not offer to rejoin a match that was left on purpose;
  * friend   two people, each a browser of its own: the host and a player who comes through the link and the name card; START takes both into the real game, the match starts by itself, with no START pressed after that
             (the server's status lists both seats and the empty ones, no START that did not lead was heard) and the two games' state hashes agree;
  * old      the old addresses: /?join=...&room=... and /?embed=1 open the game page (a shared link asks for a name), /four.html?room=... goes to /?room=... and the front page asks for the name of a shared link (with
             its way back to the front page), /play.html with nothing is today's front page (the setup screen, no arguments), /?map=...&players=1 asks for a name (a step without that line: a game on this computer is not
             recorded) and plays a game on this computer;
  * room     an address that hosts a match (/?map=treasure&players=3&fill=easy&teams=0+1) makes the test room's panel (a new six-character code shown in two groups of three; the map, the seats and the team are the room's own
             choices, which every link of it carries), and "Play in this tab" takes this tab to the game page with the room, its choices, the name and the bots of the leader's START;
  * game     the GAME PAGE in the front page's look (web/shell.html at /play.html): the clay, the frame, the font and the logo; the loading screen (the logo, a teal bar in a black box) and its failure card; 19 widths
             from 320 to 1600 px with no sideways scroll, the header's seven controls (one row with "More" up to 700 px: the five other links, over the picture), the picture, the bar and the guide inside the frame, a phone
             on its side; the logo goes back to the front page as Menu does (it asks first while a match runs or a room is joined: No stays); the two pairs under the game (the chosen one is pressed in; the clicks keep their
             ids and what they remember); the name step of a shared link, with that line; the contrast of every text (4.5:1) in each of these states.
Exit status 0: every check passed; 1: a check failed; 3: the check could not be made because the environment is not there (no browser, nothing answers at the page's address, and for the parts that open the front page
or play no game server of this tree or a site whose /ws does not lead to it).
"""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_aspect_check as aspect                                            # noqa: E402
import web_rejoin_check as rejoin                                            # noqa: E402  (its Server: the site's game server of this tree; its Player: a person's own browser)
from web_aspect_check import Browser, NotReachable, Tab                    # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PARTS = ("front", "lobby", "play", "solo", "leave", "friend", "old", "room", "game")
NEEDS_SERVER = ("front", "lobby", "play", "solo", "leave", "friend")         # (the parts that open "/" or play a match in a room: the page makes its room on the game server, and the site's /ws must lead to one)
CODE_ALPHABET = "abcdefghjkmnpqrstuvwxyz23456789"                            # a room's code is six of these (31 symbols: no i, l, o, 0 or 1; web/front/lobby_rules.js makeCode)
NAMES = ("Maple", "Clover", "Pebble", "Sorrel", "Fern", "Bramble", "Juniper", "Flint", "Willow", "Cedar", "Moss", "Thistle")      # the names that the page picks for a visitor who has none (web/front/lobby_rules.js)
COLOURS = ("black", "green", "red", "blue")                                  # the cards in their reading order: the four hills of the maps, top left to bottom right
SEAT = {"green": 0, "red": 1, "blue": 2, "black": 3}                         # (the number of a colour stays its seat)

# The contrast of every visible text with its background (WCAG: (L1 + 0.05) / (L2 + 0.05); a text that is faded by an opacity is mixed with what lies behind it, its face too); text on the clay tile is measured against the tile's two ends (its deepest and its lightest broad shade: CLAY_DEEP
# and CLAY_LIGHT of tools/front_page_art/artlib.py; tests/scripts/test_web_front.py holds the whole tile to the ink's 4.5 : 1), text in the footer against the ends of its gradient and text on a colour's card against the card's
# two ends, so the number is the worst case. Returns JSON: how many texts, the lowest ratio, and the three lowest.
CONTRAST_JS = """(function () {
  function parse(c) { var m = c.match(/rgba?\\(([^)]+)\\)/); if (!m) return null; var p = m[1].split(',').map(parseFloat); return { r: p[0], g: p[1], b: p[2], a: p.length > 3 ? p[3] : 1 }; }
  function lin(v) { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
  function lum(c) { return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b); }
  function ratio(a, b) { var l1 = lum(a), l2 = lum(b); if (l1 < l2) { var t = l1; l1 = l2; l2 = t; } return (l1 + 0.05) / (l2 + 0.05); }
  function mix(c, back, t) { return { r: c.r * t + back.r * (1 - t), g: c.g * t + back.g * (1 - t), b: c.b * t + back.b * (1 - t), a: 1 }; }
  var CLAY = [{ r: 216, g: 71, b: 16 }, { r: 233, g: 94, b: 36 }], BAR = [{ r: 0x2b, g: 0x68, b: 0x5f }, { r: 0x2b, g: 0x6b, b: 0x4f }], CARD = [{ r: 0x0f, g: 0x18, b: 0x20 }, { r: 0x08, g: 0x0c, b: 0x11 }];
  function bgOf(el) {
    for (var e = el; e && e.nodeType === 1; e = e.parentElement) {
      if (e.classList && e.classList.contains('bar')) return BAR;
      var c = parse(getComputedStyle(e).backgroundColor);
      if (e.tagName === 'BODY' || e.tagName === 'HTML') return CLAY;
      if (c && c.a > 0.99) return [c];
      if (e.classList && e.classList.contains('slot') && c && c.a === 0) return CARD;
    }
    return CLAY;
  }
  var rows = [], walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  while (walker.nextNode()) {
    var n = walker.currentNode, t = n.textContent.replace(/\\s+/g, ' ').trim(), el = n.parentElement;
    if (!t || !el || el.closest('[hidden]') || el.closest('script,style,noscript,option')) continue;
    var cs = getComputedStyle(el), r = el.getBoundingClientRect();
    if (cs.display === 'none' || cs.visibility === 'hidden' || parseFloat(cs.opacity) === 0 || r.width === 0 || r.height === 0 || el.closest('.sr')) continue;
    var fg = parse(cs.color), worst = 99, op = 1, owner = null;
    for (var a = el; a && a.nodeType === 1; a = a.parentElement) { var o = parseFloat(getComputedStyle(a).opacity); if (o < 1) { op *= o; owner = a; } }
    var behind = owner && owner.parentElement ? bgOf(owner.parentElement) : [null];
    behind.forEach(function (back) {
      bgOf(el).forEach(function (bg) { worst = Math.min(worst, ratio(back ? mix(fg, back, op) : fg, back ? mix(bg, back, op) : bg)); });
    });
    rows.push([Math.round(worst * 100) / 100, t.slice(0, 40)]);
  }
  rows.sort(function (a, b) { return a[0] - b[0]; });
  return JSON.stringify({ texts: rows.length, lowest: rows.slice(0, 3) });
})()"""

# The form controls, which a text walk does not see: the face of every drop-down and field against its own background (a shut drop-down shows the text of its chosen option), and a field's placeholder against the same
FORMS_JS = """(function () {
  function parse(c) { var m = c.match(/rgba?\\(([^)]+)\\)/); if (!m) return null; var p = m[1].split(',').map(parseFloat); return { r: p[0], g: p[1], b: p[2], a: p.length > 3 ? p[3] : 1 }; }
  function lin(v) { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
  function lum(c) { return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b); }
  function ratio(a, b) { var l1 = lum(a), l2 = lum(b); if (l1 < l2) { var t = l1; l1 = l2; l2 = t; } return (l1 + 0.05) / (l2 + 0.05); }
  function mix(c, back) { return { r: c.r * c.a + back.r * (1 - c.a), g: c.g * c.a + back.g * (1 - c.a), b: c.b * c.a + back.b * (1 - c.a) }; }
  var rows = [];
  Array.prototype.forEach.call(document.querySelectorAll('select, input[type=text]'), function (e) {
    var r = e.getBoundingClientRect();
    if (e.closest('[hidden]') || r.width === 0 || r.height === 0 || getComputedStyle(e).visibility === 'hidden') return;
    var cs = getComputedStyle(e), bg = parse(cs.backgroundColor), label = (e.id || e.className || e.tagName) + (e.tagName === 'SELECT' ? ' ' + e.options[e.selectedIndex].text : '');
    if (!bg || bg.a < 0.99) return;
    rows.push([ratio(mix(parse(cs.color), bg), bg), label]);
    if (e.placeholder) { var ph = parse(getComputedStyle(e, '::placeholder').color); if (ph) rows.push([ratio(mix(ph, bg), bg), label + ' (placeholder)']); }
  });
  rows.sort(function (a, b) { return a[0] - b[0]; });
  return JSON.stringify({ n: rows.length, lowest: rows.slice(0, 3).map(function (x) { return [Math.round(x[0] * 100) / 100, x[1]]; }) });
})()"""

# What a lobby page shows now: the room, every colour's card (who sits there or what it is set to, what its line says, the buttons it has, the team tabs), the notes, START, the strip at the top, the rejoin line, the toast
STATE_JS = """(function () {
  function $(id) { return document.getElementById(id); }
  function text(e) { return e ? e.textContent.replace(/\\s+/g, ' ').trim() : ''; }
  var banner = $('banner');
  var cards = Array.prototype.map.call(document.querySelectorAll('#slots > li'), function (li) {
    var select = li.querySelector('select.mode'), nm = li.querySelector('.nm'), field = li.querySelector('.pname input'), you = li.querySelector('.you'), stat = li.querySelector('.pstat'), tab = li.querySelector('.tab');
    var teams = Array.prototype.map.call(li.querySelectorAll('.teamset button'), function (b) { return { side: +b.getAttribute('data-side'), on: b.getAttribute('aria-pressed') === 'true', dim: b.getAttribute('aria-disabled') === 'true' }; });
    var ask = li.querySelector('.rmask');
    return { seat: +li.getAttribute('data-i'), c: li.getAttribute('data-c'), kind: (/k-(\\w+)/.exec(li.className) || [])[1] || '', label: li.getAttribute('aria-label'), mode: select ? select.value : null,
             modes: select ? Array.prototype.map.call(select.options, function (o) { return o.value + '=' + o.text; }).join(',') : '', name: nm ? text(nm) : field ? field.value : '', who: text(li.querySelector('.pname')), badge: you ? text(you) : '',
             stat: stat ? text(stat) : '', statClass: stat ? stat.className : '', pencil: !!li.querySelector('[data-edit]'), remove: !!li.querySelector('[data-remove]'), ask: ask ? text(ask) : '', grip: !!li.querySelector('[data-grip]'),
             tab: tab ? text(tab) : '', teams: teams, picked: li.classList.contains('picked'), target: li.classList.contains('picked-target'), classes: li.className, markup: nm ? nm.children.length : 0 };
  });
  return JSON.stringify({ path: location.pathname, search: location.search, heading: text($('room-h')), code: text($('code')), link: $('link').value, invite: text($('invite-line')), cards: cards,
    slotsNote: text($('slots-note')), teamsNote: text($('teams-note')), teamsWarn: $('teams-note').classList.contains('warn'),
    start: { text: text($('start')), disabled: $('start').disabled, busy: $('start').getAttribute('aria-busy') }, plan: text($('plan')), planShort: text($('plan-short')), info: text($('info')), map: $('map').value, mapDisabled: $('map').disabled,
    banner: { shown: !banner.hidden, text: text($('banner-text')), button: $('banner-x').hidden ? '' : text($('banner-x')), cls: banner.className }, rejoin: !$('rejoin').hidden, toast: $('toast').hidden ? '' : text($('toast')),
    nameStep: !$('name-step').hidden, offline: document.body.classList.contains('offline'), guest: document.body.classList.contains('guest') });
})()"""

# The layout of the lobby at the width of the window: the sideways scroll, the columns, the places of the four cards (in the order of the markup), whatever sticks out of a card or out of the window, the grip's dots against the
# names and the drop-down, START and the line under it, the link and its buttons, and the widest map name against the room that the map's drop-down leaves it
LAYOUT_JS = """(function () {
  function $(id) { return document.getElementById(id); }
  function rect(e) { var r = e.getBoundingClientRect(); return { left: r.left, right: r.right, top: r.top, bottom: r.bottom, width: r.width, height: r.height }; }
  function shown(e) { if (!e || e.closest('[hidden]')) return false; var r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0 && getComputedStyle(e).display !== 'none'; }
  function textRect(e) { var g = document.createRange(); g.selectNodeContents(e); return g.getBoundingClientRect(); }
  function overlap(a, b) { return a.width > 0 && b.width > 0 && a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top; }
  var panel = document.querySelector('.panel'), side = document.querySelector('.side');
  var cards = Array.prototype.map.call(document.querySelectorAll('#slots > li'), function (li) {
    var lr = li.getBoundingClientRect(), outside = [], dots = [];
    Array.prototype.forEach.call(li.querySelectorAll('.antbox, .cname, .mode, .pname, .pname > *, .pstat, .teamset button, .grip, .rmask, .rmask > *'), function (e) {
      var r = e.getBoundingClientRect();
      if (r.width > 0 && (r.left < lr.left - 0.5 || r.right > lr.right + 0.5 || r.top < lr.top - 0.5 || r.bottom > lr.bottom + 0.5)) outside.push(e.className || e.tagName);
    });
    var dot = li.querySelector('.grip i');
    if (dot) {
      var d = dot.getBoundingClientRect();
      Array.prototype.forEach.call(li.querySelectorAll('.cname, .pstat'), function (e) { if (overlap(d, textRect(e))) dots.push(e.className); });
      Array.prototype.forEach.call(li.querySelectorAll('.mode, .pname > *, .rmask > *, .teamset button'), function (e) { if (overlap(d, e.getBoundingClientRect())) dots.push(e.className || e.tagName); });
    }
    return { c: li.getAttribute('data-c'), rect: rect(li), outside: outside, dots: dots };
  });
  var strays = Array.prototype.filter.call(document.querySelectorAll('button, select, input, a, img, .btn, li, .panel, .banner'), function (e) {
    if (!shown(e) || e.closest('.sr') || e.type === 'radio') return false;
    var r = e.getBoundingClientRect();
    return r.right > window.innerWidth + 0.5 || r.left < -0.5;
  }).map(function (e) { return e.id || e.className || e.tagName; });
  var map = $('map'), cs = getComputedStyle(map), cv = document.createElement('canvas').getContext('2d');
  cv.font = cs.fontWeight + ' ' + cs.fontSize + ' ' + cs.fontFamily;
  var widest = Math.max.apply(null, Array.prototype.map.call(map.options, function (o) { return cv.measureText(o.text).width; }));
  return JSON.stringify({ scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth, innerH: window.innerHeight, docH: document.documentElement.scrollHeight, scrollY: window.scrollY,
    panel: rect(panel), side: shown(side) ? rect(side) : null, maprow: rect(document.querySelector('.maprow')), start: rect($('start')), go: rect(document.querySelector('.go')), cards: cards, strays: strays,
    plan: [shown($('plan')), shown($('plan-short'))], link: shown($('link')), copy: rect($('copy')), invite: rect(document.querySelector('.invite')), share: shown($('share')),
    mapFit: [widest, map.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight)] });
})()"""

LOBBY_NOTICE = "Online matches are recorded and kept for 30 days. The recordings are public and show the players’ names."        # (the line under the name field of the front page's name card and in its footer)
NOTICE_TEXT = "Online matches are recorded and kept for 30 days. Anybody can watch them, live or later, and they show the players’ names."      # (the line under the name field of the game page's name card)
NAME_CARD_JS = """JSON.stringify((function () {
  var card = document.querySelector('.name-step').getBoundingClientRect(), hint = document.getElementById('name-step-hint').getBoundingClientRect(), n = document.getElementById('who-notice'), own = document.getElementById('name-step-own').getBoundingClientRect();
  var r = n.getBoundingClientRect(), s = getComputedStyle(n), h = getComputedStyle(document.getElementById('name-step-hint'));
  return { shown: !document.getElementById('name-step').hidden, title: document.getElementById('name-step-title').textContent, button: document.getElementById('name-step-go').textContent, hint: document.getElementById('name-step-hint').textContent,
           own: document.getElementById('name-step-own').textContent, input: document.getElementById('name-step-input').value, placeholder: document.getElementById('name-step-input').placeholder, msg: document.getElementById('name-step-msg').textContent,
           notice: { text: n.textContent, shown: !n.hidden && r.width > 0 && r.height > 0 && s.display === 'block', below: r.top >= hint.bottom - 1, above: r.bottom <= own.top + 1, sameType: s.fontSize === h.fontSize && s.color === h.color,
                     inside: r.left >= card.left - 1 && r.right <= card.right + 1 && r.bottom <= card.bottom + 1 },
           inside: card.left >= 0 && card.right <= window.innerWidth && card.top >= 0 && card.bottom <= window.innerHeight, sideways: document.documentElement.scrollWidth > window.innerWidth };
})())"""
# The name step of the GAME page (web/shell.html): its notice sits between the hint and the way out, in the hint's own type
CARD_NOTICE_JS = """JSON.stringify((function () {
  var c = document.querySelector('.name-step').getBoundingClientRect(), hint = document.querySelector('.name-step-hint'), n = document.querySelector('.name-step-notice'), back = document.getElementById('name-step-back');
  var r = n.getBoundingClientRect(), h = hint.getBoundingClientRect(), b = back.getBoundingClientRect(), sn = getComputedStyle(n), sh = getComputedStyle(hint);
  return { text: n.textContent, shown: r.width > 0 && r.height > 0, sameType: sn.fontSize === sh.fontSize && sn.color === sh.color && sn.lineHeight === sh.lineHeight, order: r.top >= h.bottom && r.bottom <= b.top,
           inside: r.left >= c.left && r.right <= c.right && r.bottom <= c.bottom };
})())"""


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


# ---- what a person does at a lobby page: `t` is a Tab or a rejoin.Player (both speak `call` and `ev`) ---------------------------------------------------------------------------------------------------------------

def lobby_of(t):
    """What the lobby page of `t` shows now (STATE_JS) as a dict."""
    return json.loads(t.ev(STATE_JS))


def seated(t):
    """The page is in its room: the room has welcomed it (the colours are not greyed out) and a person sits in a colour."""
    return bool(t.ev("!document.body.classList.contains('offline') && !!document.querySelector('#slots li.k-person')"))


def card_of(state, colour):
    """The card of a colour in a lobby state (None when there is none)."""
    return next((c for c in state["cards"] if c["c"] == colour), None)


def code_of(state):
    """The room's plain code, from the link that the page shows."""
    found = re.search(r"[?&]room=([^&]+)", state["link"])
    return found.group(1) if found else ""


def key_press(t, name, code, vk, text="", modifiers=0):
    for kind in ("keyDown", "keyUp"):
        t.call("Input.dispatchKeyEvent", {"type": kind, "key": name, "code": code, "windowsVirtualKeyCode": vk, "text": text if kind == "keyDown" else "", "modifiers": modifiers})


def mouse(t, kind, x, y):
    t.call("Input.dispatchMouseEvent", {"type": kind, "x": x, "y": y, "button": "none" if kind == "mouseMoved" else "left", "buttons": 1 if kind == "mousePressed" else 0, "clickCount": 0 if kind == "mouseMoved" else 1})


def centre_of(t, selector, scroll=True):
    """The middle of an element of the page of `t` (scrolled into view first, unless `scroll` is False)."""
    return json.loads(t.ev("(function () { var e = document.querySelector(%s); if (!e) return JSON.stringify(null); %s var r = e.getBoundingClientRect(); return JSON.stringify({ x: r.left + r.width / 2, y: r.top + r.height / 2 }); })()"
                           % (json.dumps(selector), "e.scrollIntoView({ block: 'center' });" if scroll else "")))


def click_on(t, selector):
    """A click of the pointer on the middle of an element (the pointer moves there first)."""
    p = centre_of(t, selector)
    if p is None:
        raise RuntimeError("nothing at " + selector)
    mouse(t, "mouseMoved", p["x"], p["y"])
    time.sleep(0.1)
    mouse(t, "mousePressed", p["x"], p["y"])
    time.sleep(0.05)
    mouse(t, "mouseReleased", p["x"], p["y"])


def drag_on(t, source, target, during=None):
    """The pointer takes the middle of `source`, goes to the middle of `target` in steps (`during()` is called while it is held there) and lets go."""
    a = centre_of(t, source)
    b = centre_of(t, target, scroll=False)
    if a is None or b is None:
        raise RuntimeError("nothing at %s or %s" % (source, target))
    mouse(t, "mouseMoved", a["x"], a["y"])
    time.sleep(0.1)
    mouse(t, "mousePressed", a["x"], a["y"])
    for i in range(1, 9):
        mouse(t, "mouseMoved", a["x"] + (b["x"] - a["x"]) * i / 8, a["y"] + (b["y"] - a["y"]) * i / 8)
        time.sleep(0.03)
    time.sleep(0.3)
    held = during() if during else None
    mouse(t, "mouseReleased", b["x"], b["y"])
    return held


def set_mode(t, colour, word):
    """The host's drop-down of an open colour: Open, Easy bot, Medium bot, Hard bot or Nobody (the browser's own list cannot be driven: the value is set and the page told, as a pick would)."""
    t.ev("(function () { var s = document.querySelector('#slots li[data-c=%s] select'); s.value = %s; s.dispatchEvent(new Event('change', { bubbles: true })); })(); 1" % (colour, json.dumps(word)))


def open_name_card(t):
    return bool(t.ev("!document.getElementById('name-step').hidden"))


def fill_name_card(t, name, press=True):
    """Types `name` into the name card (a real insertion into the focused field) and presses Join."""
    t.ev("(function () { var i = document.getElementById('name-step-input'); i.focus(); i.select(); })(); 1")
    if name:
        t.call("Input.insertText", {"text": name})
    else:
        t.ev("document.getElementById('name-step-input').value = ''; 1")
    if press:
        click_on(t, "#name-step-go")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--ws-port", type=int, default=0, help="the WebSocket port that the site's /ws and /busy lead to: the check starts its game server there (needed by the parts that open the front page or play: %s)" % ", ".join(NEEDS_SERVER))
    ap.add_argument("--server", default=os.environ.get("ANTS_SERVER_BIN", os.path.join(REPO, "build", "src", "ants_server", "ants_server")), help="the ants_server program of this tree")
    ap.add_argument("--maps", default=os.path.join(REPO, "Original-Ants", "Maps"), help="the maps folder (default: the original's)")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--shots", default="", help="a folder to save screenshots in")
    ap.add_argument("--ready-timeout", type=float, default=120.0, metavar="SECONDS", help="how long a page may take to get its game ready (default 120)")
    ap.add_argument("--only", default="", help="run only the parts whose name contains this text (%s)" % ", ".join(PARTS))
    ap.add_argument("--bot-seconds", type=float, default=45.0, help="how long the bots play before their scores are compared (default 45)")
    args = ap.parse_args()
    aspect.READY_TIMEOUT = args.ready_timeout
    web = args.web if args.web.endswith("/") else args.web + "/"
    door = "ws://" + web.split("//", 1)[1].rstrip("/") + "/ws"                           # (the game server's door as the game is given it)

    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            failures.append(what)

    def note(text):
        print("  note: %s" % text, flush=True)

    def wanted(name):
        return not args.only or args.only in name

    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3
    plays = any(wanted(p) for p in NEEDS_SERVER)
    server = None
    work = None
    if plays:
        if not args.ws_port:
            note("no --ws-port: the parts that open the front page or play in a room of the game server (%s) are left out" % ", ".join(NEEDS_SERVER))
        elif not os.path.isfile(args.server) or not os.access(args.server, os.X_OK):
            print("  SKIP: the game server program is not there (build the target ants_server, or give --server)")
            return 3
    can_play = plays and bool(args.ws_port) and os.path.isfile(args.server)

    browser = None
    people = []
    try:
        try:
            urllib.request.urlopen(web, timeout=10).read(64)
        except (OSError, urllib.error.URLError) as e:
            print("  SKIP: nothing answers at %s (%s)" % (web, e))
            return 3
        if can_play:
            work = tempfile.mkdtemp(prefix="ants_home_check.")
            server = rejoin.Server(args.server, args.maps, args.ws_port, work)
            server.start()
            try:
                busy = json.loads(urllib.request.urlopen(web + "busy", timeout=10).read().decode("utf-8", "replace"))
            except (OSError, urllib.error.URLError, ValueError) as e:
                print("  SKIP: the site's /busy does not answer as the game server's does (%s): does the site's /ws lead to port %d?" % (e, args.ws_port))
                return 3
            if not (isinstance(busy, dict) and set(busy) == {"matches", "players"}):
                print("  SKIP: the site's /busy is not the game server's answer (%r): does the site's /ws lead to port %d?" % (busy, args.ws_port))
                return 3
            print("[web home] the server: %s" % server.describe(), flush=True)

        def site_busy():
            """The site's /busy: what the game server says it is hosting ({"matches": N, "players": M}), or None."""
            try:
                return json.loads(urllib.request.urlopen(web + "busy", timeout=10).read().decode("utf-8", "replace"))
            except (OSError, urllib.error.URLError, ValueError):
                return None

        browser = Browser(path)
        tab = Tab(browser)
        dialogs = []
        answer = {"accept": True}

        def on_dialog(msg):
            if msg.get("method") == "Page.javascriptDialogOpening" and msg.get("sessionId") == tab.session:
                dialogs.append(msg["params"].get("message", ""))
                try:
                    tab.call("Page.handleJavaScriptDialog", {"accept": answer["accept"]})
                except (RuntimeError, TimeoutError):
                    pass

        tab.dt.handlers.append(on_dialog)

        def fresh_tab():
            """A new tab, the old one closed: a tab that was emulated as a phone, or whose window was resized while its page was up, goes on reporting the media of a touch screen (no hover, so no
            Fullscreen mouse control on the game page) whatever it is set to afterwards; a page that is loaded in a new tab, after one emulation of its size, has a mouse."""
            nonlocal tab
            old = tab
            tab = Tab(browser)
            old.close()

        def pages():
            """The number of tabs and windows of the browser (a new tab is a new page target)."""
            return len([t for t in browser.devtools.call("Target.getTargets")["targetInfos"] if t["type"] == "page"])

        def load(url, ready=False, settle=1.0):
            tab.emulate(1440, 900, 1)
            if ready:
                tab.open(url, settle=settle)
            else:
                tab.open(url, wait=False)
                time.sleep(settle + 0.5)

        def clear_storage():
            """The site's storage of this tab emptied (a page of the site that holds no room: the front page makes one at once and keeps its key in the tab's session storage)."""
            tab.open(web + "changelog.html", wait=False)
            time.sleep(1.0)
            tab.ev("try { localStorage.clear(); sessionStorage.clear(); } catch (e) {} 1")

        def value(expression):
            return tab.ev(expression)

        def shot(name):
            tab.save_shot(args.shots, name)

        def front_up():
            """The tab shows the front page (its colour cards are there), whatever room it is in."""
            return bool(tab.ev("!!document.getElementById('slots')"))

        def front_ready(t=None, timeout=20):
            """The front page of `t` (default: the tab) has its room: the room has welcomed it and a person sits in a colour."""
            t = t or tab
            return bool(wait_for(lambda: seated(t), timeout))

        def lobby(t=None):
            return lobby_of(t or tab)

        def real_click(selector):
            click_on(tab, selector)

        def key(name, vk, text="", modifiers=0):
            key_press(tab, name, name, vk, text, modifiers)

        def rename_by_pencil(t, name):
            """The player's own colour: the pencil turns the name into a field; the name is typed (a real insertion) and Enter ends it."""
            click_on(t, "#slots li.k-person [data-edit]")
            wait_for(lambda: t.ev("!!document.querySelector('#slots .pname input')"), 5)
            t.call("Input.insertText", {"text": name})
            key_press(t, "Enter", "Enter", 13, "\r")

        def score_strip(cv):
            """The rectangle of the bottom of the picture where the HUD shows the other players' scores: below the map view, whose last rows show the player's own ants (they move, bots or not), and
            left of the version and the frame counter (which change at every frame; the three score boxes end at 0.81 of the canvas's width, those start at 0.87). The canvas's rectangle ->
            [x0, y0, x1, y1] in page pixels."""
            return [int(cv[0] + cv[2] * 0.17), int(cv[1] + cv[3] * 0.965), int(cv[0] + cv[2] * 0.85), int(cv[1] + cv[3] * 0.995)]

        def enter(t):
            """One press of Enter in the game's picture of the tab or person `t` (the quick help closes with it)."""
            t.ev("document.getElementById('canvas') && document.getElementById('canvas').focus(); 1")
            for kind in ("keyDown", "keyUp"):
                t.call("Input.dispatchKeyEvent", {"type": kind, "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "text": "\r" if kind == "keyDown" else ""})

        def close_quick_help(presses=4):
            """The quick help closes with Enter (an Enter that comes while the loading screen is still up only ends that: it is pressed again, a few seconds apart, until the match runs)."""
            pressed = 0
            for _ in range(presses):
                enter(tab)
                pressed += 1
                if wait_for(lambda: tab.ev("Module._ants_match_running()") == 1, 4.0):
                    break
            return pressed

        def start_by_enter():
            """The quick help closes with Enter and, with --play, the match starts at once (an Enter that comes while the loading screen is still up only ends that: it is pressed again)."""
            tab.ev("document.getElementById('canvas').focus(); 1")
            key = lambda kind: tab.call("Input.dispatchKeyEvent", {"type": kind, "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13, "text": "\r" if kind == "keyDown" else ""})
            for _ in range(4):
                key("keyDown")
                key("keyUp")
                if wait_for(lambda: tab.ev("Module._ants_match_running()") == 1, 2.5):
                    break
            else:
                return False
            return bool(wait_for(lambda: tab.ev("Module._ants_probe(5)") == 0, 60, 0.5))          # the "get ready" dialog closes by itself

        def match_runs(t, timeout=None):
            """The game of `t` (a tab or a person) is in a running match with no dialog up: a match that the lobby's START begins needs no key (the "Get ready" dialog closes by itself)."""
            return bool(wait_for(lambda: t.ev("typeof Module !== 'undefined' && !!Module._ants_match_running && Module._ants_match_running() === 1 && Module._ants_probe(5) === 0"), timeout or args.ready_timeout, 0.5))

        def stored_rejoin(t):
            """The names of the game's key entries in the browser of `t` (names only: room and seat; the keys are secrets and are never read here)."""
            return t.ev("Object.keys(localStorage).filter(function (k) { return k.indexOf('ants.rejoin.') === 0; }).sort()") or []

        def game_args(t):
            return json.loads(t.ev("JSON.stringify(ANTS_ARGS)"))

        def contrast(where, prepare="", restore="", forms=True, least=40):
            """Every text of the page is at least 4.5:1 (`prepare` shows what is closed, `restore` closes it again), and so is every drop-down and field, and its placeholder."""
            found = json.loads(value(prepare + "; var found = " + CONTRAST_JS + "; " + restore + "; found"))
            check(found["texts"] >= least and found["lowest"][0][0] >= 4.5, "%s: the text contrast is at least 4.5:1 for all %d texts (lowest: %s)" % (where, found["texts"], found["lowest"]))
            if forms:
                f = json.loads(value(FORMS_JS))
                check(f["n"] >= 3 and f["lowest"][0][0] >= 4.5, "%s: ... and for all %d drop-downs and fields, with their placeholders (lowest: %s)" % (where, f["n"], f["lowest"]))

        codes = []                                                                         # (the codes of the rooms that the check saw: their alphabet is checked at the end of part front)

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("front") and not can_play:
            note("front: left out (no game server: give --ws-port; opening the front page makes a room)")
        if wanted("front") and can_play:
            print("[web home] the front page: a first visit")
            clear_storage()
            load(web, settle=1.0)
            check(front_ready(), "the page is in a room of its own: the room has welcomed it and the host sits in a colour")
            shot("home_front")
            st = lobby()
            codes.append(code_of(st))
            info = json.loads(value("""JSON.stringify({title: document.title, path: location.pathname, h1: document.querySelectorAll('h1').length,
                logo: (function () { var i = document.querySelector('header.mast img'); return i ? [i.complete && i.naturalWidth === 581, i.getAttribute('alt'), i.getBoundingClientRect().width] : null; })(),
                tag: document.querySelector('.mast .tag').textContent, sub: document.querySelector('.mast .sub').textContent,
                head: [document.getElementById('room-h').textContent, document.querySelector('.code').textContent],
                link: [document.getElementById('link').value, document.getElementById('link').readOnly, document.getElementById('link').getAttribute('aria-label')],
                buttons: ['copy', 'havecode', 'more'].map(function (id) { return document.getElementById(id).textContent; }), share: getComputedStyle(document.getElementById('share')).display, linkLine: document.getElementById('invite-line').textContent,
                joinbox: document.getElementById('joinbox').hidden, morebox: document.getElementById('morebox').hidden,
                font: Array.from(document.fonts).some(function (f) { return f.family.indexOf('Libre Franklin') !== -1 && f.status === 'loaded'; }),
                aspect: [document.getElementById('aspect-16-9').checked, document.getElementById('aspect-4-3').checked, document.body.getAttribute('data-aspect')],
                footer: document.querySelector('footer').textContent, notice: document.getElementById('footer-notice').textContent, noticeShown: document.getElementById('footer-notice').getBoundingClientRect().height > 0,
                nav: Array.prototype.map.call(document.querySelectorAll('footer nav > *'), function (a) { return [a.tagName, a.textContent.trim(), a.getAttribute('href'), a.getAttribute('target'), a.id]; }),
                version: [document.getElementById('game-version').textContent, document.getElementById('game-build-id').textContent], scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth})"""))
            check(info["title"].startswith("Ants (1998)") and info["path"] == "/" and info["h1"] == 1, "the front page is at / and is titled Ants (1998) (%r)" % info["title"])
            check(info["logo"] is not None and info["logo"][0] and info["logo"][1] == "Ants!" and 100 <= info["logo"][2] <= 200 and info["tag"] == "Four colonies, one picnic." and info["sub"] == "The 1998 ant war game, rebuilt for your browser.",
                  "the mast has the logo (loaded, %s px wide) and the two lines under it, and nothing else: no header bar of links (%r)" % (info["logo"] and round(info["logo"][2]), info["tag"]))
            check(bool(info["font"]), "the game's own font, Libre Franklin, is loaded from the site")
            code = code_of(st)
            check(re.match(r"^[%s]{6}$" % CODE_ALPHABET, code) is not None and st["code"] == code[:3] + " " + code[3:] and info["head"] == ["Your room", "Room code " + st["code"]],
                  "the room: \"Your room\" with a code of six letters and numbers (no i, l, o, 0 or 1) shown in two groups of three (%r, the link carries %r)" % (st["code"], code))
            check(info["link"] == [web + "?room=" + code, True, "Invitation link"] and info["linkLine"] == "Players who open this link take the next free colour.",
                  "the link is the page's address with ?room=<code> in a read-only field, and the line under it says what it does (%r)" % (info["link"][0],))
            check(info["buttons"] == ["Copy link", "Have a code?", "More ways to play"] and info["share"] == "none" and info["joinbox"] and info["morebox"],
                  "Copy link, Have a code? and More ways to play are there (the last two open nothing yet; Share is for a phone) (%s)" % (info["buttons"],))
            status = server.room(code) or {}
            check(status.get("lobby") is True and status.get("state") == "waiting" and status.get("leader") == 0 and status.get("joined") == 1 and status.get("starting") is False and status.get("plan", "").startswith("oooo"),
                  "the game server holds that room as a lobby: waiting, the visitor its leader at seat 0, every colour open (%s)" % {k: status.get(k) for k in ("lobby", "state", "leader", "joined", "plan")})
            mine = status.get("players", [{}])[0].get("name") if status.get("players") else None
            check(mine is not None and card_of(st, "green")["name"] == mine and mine in NAMES,
                  "the host plays under a picked name, the one the room shows on the host's card (%r)" % mine)

            # the four cards, in the reading order of the four hills of the maps
            check([c["c"] for c in st["cards"]] == list(COLOURS) and [c["seat"] for c in st["cards"]] == [3, 0, 1, 2], "the four colour cards are built in the reading order Black, Green, Red, Blue, and keep the seats' numbers (%s)" % [(c["c"], c["seat"]) for c in st["cards"]])
            green = card_of(st, "green")
            check(green["kind"] == "person" and green["badge"] == "You" and green["pencil"] and not green["remove"] and green["stat"] == "Host. You start the match." and green["grip"] and green["label"] == "Green: %s, you, host" % green["name"],
                  "Green is the host: the name with You, a pencil to change it, no Remove, the line \"Host. You start the match.\" and the grip (%s)" % {k: green[k] for k in ("badge", "pencil", "remove", "stat", "label")})
            others = [card_of(st, c) for c in ("black", "red", "blue")]
            check(all(c["kind"] == "open" and c["mode"] == "open" and c["stat"] == "Takes the next player who joins." and c["grip"] and c["label"] == c["c"].capitalize() + ": open" and not c["teams"] for c in others)
                  and all(c["modes"] == "open=Open,easy=Easy bot,medium=Medium bot,hard=Hard bot,nobody=Nobody" for c in others),
                  "Black, Red and Blue are open: a drop-down of Open, Easy bot, Medium bot, Hard bot and Nobody that says Open, \"Takes the next player who joins.\", no team buttons (one player) (%s)" % [(c["c"], c["mode"]) for c in others])
            pos = json.loads(value("JSON.stringify(Array.prototype.map.call(document.querySelectorAll('#slots > li'), function (l) { var r = l.getBoundingClientRect(); return [l.getAttribute('data-c'), Math.round(r.left), Math.round(r.top)]; }))"))
            by = {p[0]: p for p in pos}
            check(by["black"][1] < by["green"][1] and abs(by["black"][2] - by["green"][2]) < 3 and by["red"][1] == by["black"][1] and by["blue"][1] == by["green"][1] and abs(by["red"][2] - by["blue"][2]) < 3 and by["red"][2] > by["black"][2] + 50,
                  "at 1440 px they lie as the hills do: Black top left, Green top right, Red bottom left, Blue bottom right (%s)" % pos)
            check(st["slotsNote"] == "Waiting for players. Drag a player onto another colour to move them." and st["teamsNote"] == "" and st["banner"]["shown"] is False and st["rejoin"] is False,
                  "the line under the cards says what to do (%r); no team line with one player, no strip at the top, no rejoin line" % st["slotsNote"])

            # the map side
            side = json.loads(value("""JSON.stringify({preview: (function () { var i = document.getElementById('preview'); return [i.complete && i.naturalWidth > 0, i.getAttribute('src'), i.alt]; })(),
                maps: Array.prototype.map.call(document.getElementById('map').options, function (o) { return o.value + '=' + o.text; }).join(), map: document.getElementById('map').value, info: document.getElementById('info').textContent,
                arrows: [document.getElementById('prev').getAttribute('aria-label'), document.getElementById('next').getAttribute('aria-label'), document.getElementById('prev').disabled, document.getElementById('next').disabled],
                start: (function () { var b = document.getElementById('start'), r = b.getBoundingClientRect(), cs = getComputedStyle(b); return [b.textContent, b.disabled, Math.round(r.width), Math.round(r.height), cs.fontSize]; })(),
                plan: document.getElementById('plan').textContent})"""))
            check(side["preview"] == [True, "front/preview_treasure.png", "The map: Treasure"] and side["map"] == "treasure" and side["info"] == "One person's trash... (12 min)",
                  "the map is Treasure, the default of the page (its picture, its line from the original's setup screen: %r)" % side["info"])
            check(side["maps"] == "tiny=Tiny,small=Small,medium=Medium,gauntlet=Gauntlet,treasure=Treasure,islands=Islands" and side["arrows"] == ["Previous map", "Next map", False, False], "the map's drop-down lists the six maps of the original, with arrows (%s)" % side["maps"])
            check(side["start"][0] == "START!" and not side["start"][1] and side["start"][2] >= 280 and side["start"][3] >= 56 and side["plan"] == "A game for one. Red, Blue and Black are open and stay empty unless a player joins first.",
                  "START! is on and big (%s x %s px) and the line under it says that, alone, it is a game for one: \"%s\"" % (side["start"][2], side["start"][3], side["plan"]))
            real_click("#next")
            ok = wait_for(lambda: lobby()["map"] == "islands", 5)
            status = server.room(code) or {}
            check(bool(ok) and status.get("map") == "ISLANDS.LVL" and lobby()["info"] == "Island hopping, expert map (12 min)" and lobby()["start"]["text"] == "START!",
                  "the arrow > chooses the next map, Islands: the game server's room has it (%s) and the line follows (%r)" % (status.get("map"), lobby()["info"]))
            real_click("#prev")
            real_click("#prev")
            ok = wait_for(lambda: lobby()["map"] == "gauntlet", 5)
            check(bool(ok) and (server.room(code) or {}).get("map") == "GAUNTLET.LVL", "the arrow < twice goes back over Treasure to Gauntlet, and the room follows (%s)" % (server.room(code) or {}).get("map"))
            tab.ev("var m = document.getElementById('map'); m.value = 'tiny'; m.dispatchEvent(new Event('change', { bubbles: true })); 1")
            ok = wait_for(lambda: (server.room(code) or {}).get("map") == "TINY.LVL", 5)
            check(bool(ok) and lobby()["info"] == "Tiny map with no PowerUps (6 min)", "the drop-down chooses a map too (Tiny: %r)" % lobby()["info"])
            tab.ev("var m = document.getElementById('map'); m.value = 'treasure'; m.dispatchEvent(new Event('change', { bubbles: true })); 1")
            wait_for(lambda: (server.room(code) or {}).get("map") == "TREASURE.LVL", 5)

            # the open colours: a bot of a level, Nobody (the drop-down tells the room, the card and the line under START follow)
            set_mode(tab, "red", "hard")
            ok = wait_for(lambda: card_of(lobby(), "red")["kind"] == "bot", 5)
            red = card_of(lobby(), "red")
            check(bool(ok) and red["mode"] == "hard" and red["stat"] == "Computer player" and (server.room(code) or {}).get("plan", "")[1:2] == "h" and lobby()["plan"].startswith("You and a Hard bot play on Treasure."),
                  "Red set to a Hard bot: the card says Computer player, the room's plan has it and the line under START names it (%r)" % lobby()["plan"])
            set_mode(tab, "blue", "nobody")
            ok = wait_for(lambda: card_of(lobby(), "blue")["kind"] == "nobody", 5)
            blue = card_of(lobby(), "blue")
            check(bool(ok) and blue["stat"] == "Stays out of the match." and lobby()["plan"].startswith("You and a Hard bot play on Treasure. Black is open and stays empty unless a player joins first."),
                  "Blue set to Nobody: its card says it stays out of the match, and the line under START leaves it out (%r)" % lobby()["plan"])
            set_mode(tab, "red", "open")
            set_mode(tab, "blue", "open")
            wait_for(lambda: lobby()["plan"].startswith("A game for one."), 5)

            # How it works
            real_click("#how-open")
            how = json.loads(value("JSON.stringify({shown: !document.getElementById('how').hidden, titles: Array.prototype.map.call(document.querySelectorAll('#how h3'), function (h) { return h.textContent; }), role: document.querySelector('#how .panel').getAttribute('role'), focus: document.activeElement.id})"))
            check(how["shown"] and how["role"] == "dialog" and how["focus"] == "how-close" and how["titles"] == ["The room", "The other colours", "Removing a player", "Teams", "START!", "When the host leaves"],
                  "How it works opens a dialog over the page, with the focus on Close (%s)" % how["titles"])
            contrast("1440 px with How it works up", "document.getElementById('how').hidden = false", "document.getElementById('how').hidden = true")
            key("Escape", 27)
            check(bool(wait_for(lambda: value("document.getElementById('how').hidden") is True, 3)) and value("document.activeElement.id") == "how-open", "Esc closes it and the focus goes back to the link that opened it")
            real_click("#how-open")
            real_click("#how-close")
            check(value("document.getElementById('how').hidden") is True, "the Close button closes it too")

            # Have a code?, More ways to play
            real_click("#more")
            more = json.loads(value("JSON.stringify({shown: !document.getElementById('morebox').hidden, buttons: Array.prototype.map.call(document.querySelectorAll('#morebox button'), function (b) { return b.textContent; }), note: document.querySelector('#morebox span').textContent})"))
            check(more["shown"] and more["buttons"] == ["Play every colour myself, in this page", "Play every colour in separate windows"] and more["note"] == "For testing:",
                  "More ways to play opens the two ways to play every colour yourself, for testing (%s)" % more["buttons"])
            real_click("#havecode")
            check(value("!document.getElementById('joinbox').hidden && document.activeElement.id") == "joincode", "Have a code? opens the field, with the focus in it")
            contrast("1440 px with the code field and More ways to play open", least=45)
            real_click("#joingo")
            check(lobby()["start"]["text"] == "START!" and value("document.getElementById('joinhint').textContent") == "Type or paste the code first.", "Join with nothing typed says to type or paste the code first")
            tab.ev("document.getElementById('joincode').focus(); 1")
            tab.call("Input.insertText", {"text": "a!b"})
            real_click("#joingo")
            check(value("document.getElementById('joinhint').textContent") == "A room code has letters and numbers only, like k7m 2xq.", "a code with a sign in it is told: letters and numbers only (%r)" % value("document.getElementById('joinhint').textContent"))
            contrast("1440 px with a line under the code field", least=45)
            tab.ev("document.getElementById('joincode').value = ''; document.getElementById('joincode').focus(); 1")
            tab.call("Input.insertText", {"text": code[:3].upper() + " " + code[3:]})
            real_click("#joingo")
            check(value("document.getElementById('joinhint').textContent") == "That is your own room." and not open_name_card(tab), "the page's own code (capitals and the space do not matter) is \"That is your own room.\" and asks for no name")
            tab.ev("document.getElementById('joincode').value = ''; document.getElementById('joincode').focus(); 1")
            tab.call("Input.insertText", {"text": "zzz zzz"})
            real_click("#joingo")
            asked = wait_for(lambda: open_name_card(tab), 5)
            card = json.loads(value(NAME_CARD_JS))
            check(bool(asked) and card["title"] == "Join the room zzz zzz" and card["button"] == "Join" and card["own"] == "Start a room of my own instead" and card["placeholder"] in NAMES and card["notice"]["text"] == LOBBY_NOTICE,
                  "another code opens the name card first: \"%s\", the field, Join, \"Start a room of my own instead\" and the notice about recorded matches (%s)" % (card["title"], card["placeholder"]))
            contrast("1440 px with the name card up", least=40)
            shot("home_front_name_card")
            click_on(tab, "#name-step-go")
            ok = wait_for(lambda: value("document.getElementById('joinhint').textContent") != "", 8)
            check(bool(ok) and value("document.getElementById('joinhint').textContent") == "There is no room with that code. Check it, or ask the host to send the link again." and not open_name_card(tab) and lobby()["code"] == st["code"],
                  "Join with a code that has no room: \"%s\" and the page's own room stays (%s)" % (value("document.getElementById('joinhint').textContent"), lobby()["code"]))
            tab.ev("document.getElementById('joincode').value = ''; document.getElementById('joincode').focus(); 1")
            tab.call("Input.insertText", {"text": "zzz zzz"})
            real_click("#joingo")
            wait_for(lambda: open_name_card(tab), 5)
            click_on(tab, "#name-step-own")
            check(not open_name_card(tab) and lobby()["code"] == st["code"] and lobby()["heading"] == "Your room", "\"Start a room of my own instead\" closes the card and the page's own room stays")
            real_click("#havecode")
            real_click("#more")
            check(value("document.getElementById('joinbox').hidden && document.getElementById('morebox').hidden") is True, "the two links close what they opened")

            # the footer
            check("Version" in info["footer"] and "@@" not in info["footer"] and "build" in info["footer"] and re.match(r"^\d+\.\d+\.\d+", info["version"][0]) is not None and info["version"][1] != "",
                  "the footer names the version and the build (%r)" % info["footer"][:80])
            hrefs = {n[1]: (n[0], n[2], n[3]) for n in info["nav"]}
            check([n[1] for n in info["nav"]] == ["How it works", "Watch live", "Watch replays", "Sprites and sounds", "Changelog", "GitHub", "Feedback"] and hrefs["How it works"][0] == "BUTTON" and hrefs["Watch live"][1] == "/watch.html" and hrefs["Watch replays"][1] == "/watch.html"
                  and hrefs["Sprites and sounds"][1] == "/asset_catalog/" and hrefs["Changelog"][1] == "/changelog.html" and "github.com" in hrefs["GitHub"][1] and "issues" not in hrefs["GitHub"][1] and hrefs["Feedback"][1].endswith("/issues"),
                  "the footer links: How it works, Watch live, Watch replays, Sprites and sounds, Changelog, GitHub, Feedback (%s)" % [n[1] for n in info["nav"]])
            check(all(hrefs[k][2] == "_blank" for k in ("Sprites and sounds", "Changelog", "GitHub", "Feedback")) and hrefs["Watch live"][2] is None and hrefs["Watch replays"][2] is None, "the links that leave the front page keep their new tab, the two that watch a match do not")
            check(info["notice"] == LOBBY_NOTICE and info["noticeShown"], "the footer says that online matches are recorded, kept for 30 days and public with the players' names (%r)" % info["notice"])
            stats = json.loads(value("JSON.stringify({hidden: document.getElementById('stats').hidden, text: document.getElementById('stats').textContent.replace(/\\s+/g, ' ').trim()})"))
            check(stats["hidden"] or re.match(r"^\d[\d,]* matches? being played · \d[\d,]* players? online( · \d[\d,]* games? played \(\d[\d,]* today\))?$", stats["text"]) is not None,
                  "the line of numbers is either not there (a site with no /stats) or says what it counts (%r)" % (stats,))
            check(info["scrollW"] <= info["innerW"], "no horizontal scroll at 1440")

            # the picture's shape: 16:9 is the default, 4:3 is chosen under Screen and remembered
            check(info["aspect"] == [True, False, "16:9"], "Screen: 16:9 is chosen on a first visit, and 4:3 is not (%s)" % info["aspect"])
            real_click("label[for=aspect-4-3]")
            chosen = json.loads(value("JSON.stringify([document.getElementById('aspect-4-3').checked, document.getElementById('aspect-16-9').checked, document.body.getAttribute('data-aspect'), localStorage.getItem('ants.aspect.v2')])"))
            check(chosen == [True, False, "4:3", "4:3"] and lobby()["code"] == st["code"], "a press on 4:3 chooses it (the page keeps its room) and the browser remembers it (%s)" % (chosen,))
            tab.open(web, wait=False)
            time.sleep(1.5)
            chosen = json.loads(value("JSON.stringify([document.getElementById('aspect-4-3').checked, document.getElementById('aspect-16-9').checked, document.body.getAttribute('data-aspect')])"))
            check(chosen == [True, False, "4:3"], "... and the next visit starts with 4:3 chosen (%s)" % (chosen,))
            real_click("label[for=aspect-16-9]")
            check(value("[document.getElementById('aspect-16-9').checked, localStorage.getItem('ants.aspect.v2')]") == [True, "16:9"], "a press on 16:9 brings it back")
            tab.ev("localStorage.setItem('ants.aspect.v2', 'junk'); 1")
            tab.open(web, wait=False)
            time.sleep(1.5)
            check(value("[document.getElementById('aspect-16-9').checked, document.body.getAttribute('data-aspect')]") == [True, "16:9"], "a remembered value that is no shape is ignored: 16:9")
            tab.ev("localStorage.removeItem('ants.aspect.v2'); localStorage.setItem('ants.aspect', '4:3'); 1")
            tab.open(web, wait=False)
            time.sleep(1.5)
            check(value("[document.getElementById('aspect-16-9').checked, document.body.getAttribute('data-aspect')]") == [True, "16:9"], "a 4:3 that the old key 'ants.aspect' remembers is not read: 16:9 (every browser starts with 16:9)")
            tab.open(web + "?aspect=4:3", wait=False)
            time.sleep(1.5)
            check(value("[document.getElementById('aspect-4-3').checked, document.body.getAttribute('data-aspect')]") == [True, "4:3"], "?aspect=4:3 in the address chooses 4:3")
            tab.open(web + "?aspect=21:9", wait=False)
            time.sleep(1.5)
            check(value("[document.getElementById('aspect-16-9').checked, document.body.getAttribute('data-aspect')]") == [True, "16:9"], "?aspect=21:9 is not a shape the page offers: ignored, 16:9")
            tab.ev("localStorage.removeItem('ants.aspect'); 1")

            # a new tab makes a new room each time: the codes are letters and numbers
            for _ in range(7):
                fresh_tab()
                tab.emulate(1440, 900, 1)
                tab.open(web, wait=False)
                wait_for(lambda: seated(tab), 15)
                codes.append(code_of(lobby()))
            alphabet_ok = all(re.match(r"^[%s]{6}$" % CODE_ALPHABET, c) for c in codes)
            check(alphabet_ok and len(set(codes)) == len(codes) and any(ch.isdigit() for c in codes for ch in c) and any(ch.isalpha() for c in codes for ch in c),
                  "every new tab makes a room of its own with a new code of six letters and numbers (%d codes seen, all different: %s)" % (len(codes), " ".join(codes)))

            # a link to a room that is gone: the name card first, then a room of the visitor's own, with the strip that says so
            fresh_tab()
            tab.emulate(1440, 900, 1)
            tab.open(web + "?room=qqqqqq", wait=False)
            asked = wait_for(lambda: open_name_card(tab), 10)
            card = json.loads(value(NAME_CARD_JS))
            st = lobby()
            check(bool(asked) and card["title"] == "Join the room qqq qqq" and card["notice"]["shown"] and card["notice"]["below"] and card["notice"]["above"] and card["notice"]["inside"] and card["notice"]["sameType"] and not card["sideways"] and card["inside"],
                  "a link to a room (?room=qqqqqq) asks for a name first: the notice about recorded matches is under the hint and above the way out, in the hint's type, inside the card (%s)" % card["notice"])
            check(st["heading"] == "Joining a room" and st["start"]["disabled"] and st["plan"] == "" and st["cards"][0]["kind"] in ("open", "nobody") and not any(c["grip"] or c["pencil"] for c in st["cards"]) and st["offline"],
                  "the page behind the card waits with no names and START off: \"Joining a room\", the colours grey and shut (%s)" % st["heading"])
            check(not tab.ev("document.querySelector('.maprow').getBoundingClientRect().height > 0 && getComputedStyle(document.querySelector('.maprow')).visibility === 'visible'"), "... and the map is not shown, as it is not known yet")
            check(server.room("qqqqqq") is None, "no room was made yet: the server has none under that code")
            contrast("1440 px with the name card of a link", least=40)
            fill_name_card(tab, "Zoeë", press=False)
            click_on(tab, "#name-step-go")
            time.sleep(0.4)
            card = json.loads(value(NAME_CARD_JS))
            check(card["shown"] and "printable ASCII" in card["msg"] and (server.room("qqqqqq") is None), "a name with an accent is refused with the reason, in the card, and nothing starts (%r)" % card["msg"][:60])
            contrast("1440 px with the name card's refusal", least=40)
            fill_name_card(tab, "Bot (Zed)", press=False)
            click_on(tab, "#name-step-go")
            time.sleep(0.4)
            check("Bot (" in json.loads(value(NAME_CARD_JS))["msg"], "a name that begins like a computer player's is refused too (%r)" % json.loads(value(NAME_CARD_JS))["msg"][:60])
            fill_name_card(tab, "Iris")
            ok = front_ready()
            st = lobby()
            check(ok and not open_name_card(tab) and st["heading"] == "Your room" and st["banner"]["shown"] and st["banner"]["text"] == "That room is gone, because everybody left, so this one is yours now. Send the link on if you like." and st["banner"]["button"] == "OK" and "warn" not in st["banner"]["cls"],
                  "Join with a name: the room is not there, so this one is the visitor's own, under that name, and a green strip says so (%r)" % st["banner"]["text"])
            check(card_of(st, "green")["name"] == "Iris" and code_of(st) == "qqqqqq" and (server.room("qqqqqq") or {}).get("lobby") is True and (server.room("qqqqqq") or {}).get("leader") == 0,
                  "... the link's code is made again, for the visitor: a lobby on the server with Iris its leader (%s)" % {k: (server.room("qqqqqq") or {}).get(k) for k in ("lobby", "state", "leader")})
            shot("home_front_gone_strip")
            contrast("1440 px with the strip at the top", least=40)
            real_click("#banner-x")
            check(lobby()["banner"]["shown"] is False, "OK closes the strip")
            check(value("localStorage.getItem('ants.name')") == "Iris", "the name that was typed is remembered by the browser (for the next room)")

            # widths: the layout of the whole page, 320 to 1600 px
            fresh_tab()
            tab.emulate(1440, 900, 1)
            tab.open(web, wait=False)
            front_ready()
            wrong = []
            widths = (320, 339, 340, 360, 374, 375, 390, 414, 480, 600, 720, 721, 768, 900, 1024, 1040, 1041, 1100, 1220, 1280, 1366, 1440, 1600)
            for width in widths:
                tab.emulate(width, 900, 1, width <= 480)       # (a phone's scrollbar is an overlay and takes no width: a browser that shows classic ones leaves 305 px of a 320 px window)
                time.sleep(0.3)
                m = json.loads(value(LAYOUT_JS))
                problems = []
                if m["scrollW"] > m["innerW"]:
                    problems.append("scrolls sideways (%d > %d)" % (m["scrollW"], m["innerW"]))
                if m["strays"]:
                    problems.append("sticks out of the window: %s" % m["strays"][:3])
                panel, start = m["panel"], m["start"]
                two = m["side"] is not None and m["side"]["left"] >= panel["right"] - 1 and abs(m["side"]["top"] - panel["top"]) < 30
                if two != (width >= 1041):
                    problems.append("%s columns" % ("two" if two else "one"))
                if not two and m["side"] is not None and m["side"]["top"] < panel["bottom"] - 1:
                    problems.append("the map side overlaps the room")
                cards = {c["c"]: c["rect"] for c in m["cards"]}
                if [c["c"] for c in m["cards"]] != list(COLOURS):
                    problems.append("the cards are not in the order Black, Green, Red, Blue")
                elif width >= 721:
                    b, g, r, bl = (cards[c] for c in COLOURS)
                    if not (b["left"] < g["left"] and abs(b["top"] - g["top"]) < 3 and abs(r["left"] - b["left"]) < 3 and abs(bl["left"] - g["left"]) < 3 and abs(r["top"] - bl["top"]) < 3 and r["top"] > b["bottom"] - 1):
                        problems.append("the cards do not lie two by two (Black, Green / Red, Blue): %s" % {k: (round(v["left"]), round(v["top"])) for k, v in cards.items()})
                else:
                    seq = [cards[c] for c in COLOURS]
                    if not (all(abs(x["left"] - seq[0]["left"]) < 3 for x in seq) and all(a["bottom"] <= b["top"] + 1 for a, b in zip(seq, seq[1:]))):
                        problems.append("the cards are not one column in the order Black, Green, Red, Blue")
                for c in m["cards"]:
                    r = c["rect"]
                    if r["left"] < panel["left"] - 0.5 or r["right"] > panel["right"] + 0.5:
                        problems.append("%s sticks out of the room's box" % c["c"])
                    if c["outside"]:
                        problems.append("%s: %s stick out of the card" % (c["c"], c["outside"][:2]))
                    if c["dots"]:
                        problems.append("%s: the grip's dots lie over %s" % (c["c"], c["dots"][:2]))
                if width <= 720:                                                         # START stays in view at the bottom of a phone's window, with its line under it (the page is longer than the window)
                    if not (m["docH"] > m["innerH"] and start["top"] >= 0 and start["bottom"] <= m["innerH"] + 0.5 and m["go"]["bottom"] <= m["innerH"] + 0.5 and m["go"]["bottom"] >= m["innerH"] - 30):
                        problems.append("START! is not held at the bottom of the window (%s)" % {k: round(v) for k, v in start.items() if k in ("top", "bottom")})
                    if m["plan"] != [False, True] or m["link"] or abs(m["copy"]["width"] - m["invite"]["width"]) > 1.5 and not m["share"]:
                        problems.append("the phone's short line, no link field and a wide Copy link: %s" % ([m["plan"], m["link"], round(m["copy"]["width"]), round(m["invite"]["width"])],))
                else:
                    if m["plan"] != [True, False] or not m["link"]:
                        problems.append("the long line under START and the link field are shown: %s" % ([m["plan"], m["link"]],))
                    if width >= 1041 and start["bottom"] > m["innerH"]:
                        problems.append("START! is not in the first screen")
                if m["mapFit"][0] > m["mapFit"][1] + 0.5:
                    problems.append("the map's name is cut off (%.0f px of text in %.0f)" % (m["mapFit"][0], m["mapFit"][1]))
                if problems:
                    wrong.append((width, problems[:3]))
            check(not wrong, "%d widths from 320 to 1600 px: no sideways scroll and nothing out of the window, two columns from 1041 px (the room, the map and START! at its side) and one under it, the cards two by two above 720 px and one column below it in the order Black, Green, Red, Blue, "
                             "nothing out of a card or of the room's box, the grip's dots clear of the names and the drop-down, START! held in view on a phone, the longest map name whole (wrong: %s)" % (len(widths), wrong))
            tab.emulate(1366, 900, 1)
            time.sleep(0.4)
            shot("home_front_1366")
            tab.emulate(768, 1000, 1)
            time.sleep(0.4)
            shot("home_front_768")

            # a phone
            stub = tab.call("Page.addScriptToEvaluateOnNewDocument", {"source": "window.__shared = []; navigator.share = function (d) { window.__shared.push(d); return Promise.resolve(); };"})["identifier"]
            try:
                tab.emulate(390, 844, 2, mobile=True)
                tab.open(web, wait=False)
                front_ready()
                phone = json.loads(value("JSON.stringify({scrollW: document.documentElement.scrollWidth, innerW: window.innerWidth, left: document.querySelector('.panel').getBoundingClientRect().left, share: document.getElementById('share').getBoundingClientRect().width > 0, label: document.getElementById('share').textContent})"))
                check(phone["scrollW"] <= phone["innerW"] and phone["left"] >= 15 and phone["share"] and phone["label"] == "Share", "phone width (390): no horizontal scroll, 16 px gutters, and where the browser has navigator.share a Share button beside Copy link (%s)" % phone)
                shot("home_front_phone")
                real_click("#share")
                shared = wait_for(lambda: value("window.__shared"), 3)
                check(isinstance(shared, list) and len(shared) == 1 and shared[0].get("url") == lobby()["link"], "Share hands the room's link to the browser's share sheet (%s)" % (shared,))
                contrast("390 px", "document.getElementById('how').hidden = false", "document.getElementById('how').hidden = true")
                tab.ev("window.scrollTo(0, document.documentElement.scrollHeight); 1")
                time.sleep(0.3)
                shot("home_front_phone_bottom")
                pb = json.loads(value("JSON.stringify({start: document.getElementById('start').getBoundingClientRect().bottom, inner: window.innerHeight, foot: document.querySelector('footer').getBoundingClientRect().bottom, docH: document.documentElement.scrollHeight})"))
                check(abs(pb["foot"] - pb["inner"]) < 2 and pb["start"] <= pb["inner"], "scrolled to the bottom of a phone the footer ends the page and START! has not moved off the window (%s)" % pb)
            finally:
                tab.call("Page.removeScriptToEvaluateOnNewDocument", {"identifier": stub})
            fresh_tab()
            tab.emulate(390, 844, 2, mobile=True)
            tab.open(web + "?room=qqqqqq", wait=False)
            wait_for(lambda: open_name_card(tab), 10)
            card = json.loads(value(NAME_CARD_JS))
            check(card["shown"] and card["inside"] and not card["sideways"] and card["notice"]["inside"], "the name card of a link on a phone (390 px): whole in the window, with its notice, no sideways scroll (%s)" % {k: card[k] for k in ("inside", "sideways")})
            contrast("390 px with the name card up", least=40)
            shot("home_front_phone_name_card")
            tab.emulate(1440, 900, 1)

            # the game server cannot be reached: the gold strip, the colours grey, and the room made by itself when the server is back
            fresh_tab()
            server.stop()
            tab.emulate(1440, 900, 1)
            tab.open(web, wait=False)
            shown = wait_for(lambda: lobby()["banner"]["shown"], 15)
            st = lobby()
            check(bool(shown) and st["banner"]["text"] == "Cannot reach the game server. Check your connection; trying again…" and "warn" in st["banner"]["cls"] and st["banner"]["button"] == "" and st["offline"] and st["start"]["disabled"] is True and st["plan"] == "There is no room yet.",
                  "with no game server behind /ws the page says so in a gold strip with no button and goes on trying (%r)" % st["banner"]["text"])
            shot("home_front_unreachable")
            contrast("1440 px with the game server unreachable", least=30)
            server.start()
            back = wait_for(lambda: seated(tab) and not lobby()["banner"]["shown"], 40, 0.5)
            st = lobby()
            check(bool(back) and st["heading"] == "Your room" and (server.room(code_of(st)) or {}).get("lobby") is True,
                  "... and when the server is back the page gets its room by itself, the strip goes (a room on the server: %s)" % ((server.room(code_of(st)) or {}).get("state"),))
            tab.emulate(1440, 900, 1)
            clear_storage()

    except NotReachable as e:
        print("  SKIP: %s" % e)
        return 3
    except Exception as e:                                      # noqa: BLE001  (a page that hangs or crashes is a failure, never a skip)
        print("  FAIL: the check broke down: %s: %s" % (type(e).__name__, e))
        import traceback
        traceback.print_exc()
        return 1
    finally:
        for p in people:
            try:
                p.close()
            except Exception:                                   # noqa: BLE001
                pass
        if browser is not None:
            browser.close()
        if server is not None:
            server.stop()
        if work:
            shutil.rmtree(work, ignore_errors=True)
    print("[web home] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
