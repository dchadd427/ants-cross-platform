#!/usr/bin/env python3
"""The front page, the lobby that it is, and the way into and out of a game, in a REAL browser (opt-in; see tests/scripts/test_web_home.sh and docs/NETWORK_PORT.md, "The front page").

The front page is the owner's lobby (web/lobby.html, the pictures he approved; network protocol 16): opening "/" makes a ROOM on the game server and seats the visitor in it (a code of six letters and
numbers, shown as `k7m 2xq`; a link, ?room=<code>, to send on), four colour cards (Black top left, Green top right, Red bottom left, Blue bottom right; a colour that nobody holds is open, a bot of a level or
Nobody; players are dragged between colours), the map and START! at its side, and the footer with the version, the links and the picture's shape (Screen: Classic 4:3, 16:10, 16:9, which is the default, or 21:9). A visitor who
opens somebody's link, or types a code into "Have a code?", is asked for a name first. START hands every browser that is in the room to the game page, which takes its seat with the key that the lobby page
wrote to the browser's storage; START with nobody else in the match plays a game on this computer. Needs a running web page that has nginx's routes (the web image of this tree: `docker build -t ants-beta .`,
run it on a port), a Chromium-based browser and Python 3, and, because opening "/" makes a room, the site's /ws leading to a game server: --ws-port is the port that it leads to, and the check starts the
native `ants_server` of this tree there (as web_rejoin_check.py does). The DevTools protocol is spoken with the client of web_hidden_check.py, standard library only. Each part opens the site in a throwaway
headless browser (its own profile and port; nothing of yours is touched), the parts with several people in one browser each. Parts:

  * front    the page at "/" on a first visit (a room of its own: a new code of six letters and numbers from the alphabet without look-alikes, the link with it, Copy link, Have a code? and More ways to play;
             the four cards in their places, the host's with You and the pencil, the others open with their drop-down; the map side with its picture, arrows and the Treasure default; START!; the footer's version, build, links
             and notice; Screen with 16:9 the default and 4:3 remembered, a stale or bad value ignored); How it works; Have a code? with its lines for a bad, an own and a code that has no room; the name card of a link to
             a room that is gone (and the room of its own, with the strip that says so); 23 widths from 320 to 1600 px: no sideways scroll, two columns from 1041 px, the cards two by two above 720 px and one under another
             below it, nothing sticking out of a card, the grip's dots clear of the names, START! in view on a phone, the map's name not cut off; Share where the browser has it; the game server that cannot be reached (the gold
             strip, and the room made by itself when it is back); the contrast of all text and of the form controls (4.5:1) in each of these states, at 1440 and 390 px;
  * lobby    one host and guests, each a browser of its own: a guest joins through the link and its name card (a bad name is refused with the reason) and takes the next free colour; the host drags a player onto an
             open colour (a move) and onto a taken one (a swap), or taps the grip and then the colour (Esc lets go); with three players Team 1 and Team 2 appear on each colour that plays, a team goes with its player, a
             third colour on a full team is refused with the reason, and a guest sees the teams as read-only tabs; a bot of a level takes a colour; a player renames themselves with the pencil (a bad name is refused, a name
             with markup stays text); the host's Remove asks first (Keep, Esc, one question at a time) and the removed player gets a room of their own with the strip that says why; a typed code (spaces and capitals do not
             matter) leads through the name card into the room, a code that has no room does not; a reload brings the seat back; the page holds up at 320 px with a name of 32 wide letters and the question open; START
             hands everybody into the real game: every page goes to the game page with its seat's key kept in the browser, the server's status and /busy show the match (three people and a bot, the teams that were set),
             every game runs it, the state hashes agree, a game page that is reloaded takes its seat back, and a link opened now gets a room of its own;
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
             (the server's status lists both seats, no bot and no START that did not lead; /busy counts two people) and the two games' state hashes agree;
  * old      the old addresses: /?join=...&room=... and /?embed=1 open the game page (a shared link asks for a name), /four.html?room=... goes to /?room=... and the front page asks for the name of a shared link (with
             its way back to the front page), /play.html with nothing is the game page's own setup screen (no argument but the picture's shape), /?map=...&players=1 asks for a name (a step without that line: a game on this computer is not
             recorded) and plays a game on this computer;
  * room     an address that hosts a match (/?map=treasure&players=3&fill=easy&teams=0+1) makes the test room's panel (a new code of six letters and numbers, shown in two groups of three; the map, the seats and the team are the room's own
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

MAP_LINE_LEAST = 2.9                                                         # the map line's contrast at the tile's lightest end is 2.95:1 in the approved picture; it must not get worse (the owner decides whether the picture's colour stays)

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
  var rows = [], info = null, walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
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
    if (el.closest('#info')) { info = Math.round(worst * 100) / 100; continue; }          // (the map line: cream on the clay tile as the owner's picture 1 draws it, held apart below)
    rows.push([Math.round(worst * 100) / 100, t.slice(0, 40)]);
  }
  rows.sort(function (a, b) { return a[0] - b[0]; });
  return JSON.stringify({ texts: rows.length, lowest: rows.slice(0, 3), info: info });
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


def agree(players, limit=60):
    """Waits until the games of `players` (rejoin.Player objects) have reported at least two ticks in common (a game reports every 100 ticks, 5 s of play) and returns (the common ticks, whether every game
    reported the same hash at each of them, whether any game reported one tick with two hashes)."""
    deadline = time.time() + limit
    common = []
    while time.time() < deadline:
        for p in players:
            p.syncs()
        common = [t for t in sorted(players[0].ticks) if all(t in p.ticks for p in players)]
        if len(common) >= 2:
            break
        time.sleep(1.0)
    same = bool(common) and all(len({p.ticks[t] for p in players}) == 1 for t in common)
    return common, same, any(p.conflicts for p in players)


def console_trouble(players):
    """What the pages of `players` (rejoin.Player objects) said in their consoles that is no good: an exception, or an error that is no failed download of a file."""
    return [(i, kind, text[:120]) for i, p in enumerate(players) for (_, kind, text) in p.console if kind == "exception" or (kind == "error" and "Failed to load resource" not in text)]


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

        def rename_by_pencil(t, name, refused=False):
            """The player's own colour: the pencil turns the name into a field; the name is typed (a real insertion) and Enter ends it. The next step waits until the card shows the name (the page
            has the room's word for it: a pencil pressed before that does nothing), or, for a name that the page refuses (`refused`), a moment."""
            click_on(t, "#slots li.k-person [data-edit]")
            wait_for(lambda: t.ev("!!document.querySelector('#slots .pname input')"), 5)
            t.call("Input.insertText", {"text": name})
            key_press(t, "Enter", "Enter", 13, "\r")
            if refused:
                time.sleep(0.8)
            else:
                wait_for(lambda: any(c["badge"] == "You" and c["name"] == name for c in lobby_of(t)["cards"]), 6)
                time.sleep(0.3)

        def set_modes(t, modes):
            """The host's drop-downs, one after another ({"red": "medium", ...}): a person takes a moment between two, so the room's reply to one comes before the next change is made."""
            for colour, word in modes.items():
                set_mode(t, colour, word)
                wait_for(lambda c=colour, w=word: card_of(lobby_of(t), c)["mode"] == w, 5)
                time.sleep(0.2)

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

        def contrast(where, prepare="", restore="", forms=True, least=30, t=None):
            """Every text of the page of `t` (default: the tab) is at least 4.5:1 (`prepare` shows what is closed, `restore` closes it again), and so is every drop-down and field, and its placeholder."""
            t = t or tab
            found = json.loads(t.ev((prepare + "; " if prepare else "") + "var found = " + CONTRAST_JS + "; " + (restore + "; " if restore else "") + "found"))
            check(found["texts"] >= least and found["lowest"][0][0] >= 4.5, "%s: the text contrast is at least 4.5:1 for all %d texts (lowest: %s)" % (where, found["texts"], found["lowest"]))
            if found.get("info") is not None:
                check(found["info"] >= MAP_LINE_LEAST, "%s: ... but for the map line, which is cream on the clay tile as the owner's picture 1 draws it (his call: 4.5:1 would need another colour), it is not lower than %s:1 (%s)" % (where, MAP_LINE_LEAST, found["info"]))
            if forms:
                f = json.loads(t.ev(FORMS_JS))
                check(f["n"] >= 2 and f["lowest"][0][0] >= 4.5, "%s: ... and for all %d drop-downs and fields, with their placeholders (lowest: %s)" % (where, f["n"], f["lowest"]))

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
                aspect: ['4-3', '16-10', '16-9', '21-9'].map(function (id) { return document.getElementById('aspect-' + id).checked; }).concat([document.body.getAttribute('data-aspect')]),
                tags: Array.prototype.map.call(document.querySelectorAll('.fit-tag'), function (t) { return [t.id, t.hidden, getComputedStyle(t).display]; }), screenSize: [screen.width, screen.height],
                shapeLabels: Array.prototype.map.call(document.querySelectorAll('.shape .seg label'), function (l) { return l.textContent; }),
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
            contrast("1440 px with How it works up")
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
                  "the footer names the version and the build (%r)" % re.sub(r"\s+", " ", info["footer"]).strip()[:80])
            hrefs = {n[1]: (n[0], n[2], n[3]) for n in info["nav"]}
            check([n[1] for n in info["nav"]] == ["How it works", "Watch live", "Watch replays", "Sprites and sounds", "Changelog", "GitHub", "Feedback"] and hrefs["How it works"][0] == "BUTTON" and hrefs["Watch live"][1] == "/watch.html" and hrefs["Watch replays"][1] == "/watch.html"
                  and hrefs["Sprites and sounds"][1] == "/asset_catalog/" and hrefs["Changelog"][1] == "/changelog.html" and "github.com" in hrefs["GitHub"][1] and "issues" not in hrefs["GitHub"][1] and hrefs["Feedback"][1].endswith("/issues"),
                  "the footer links: How it works, Watch live, Watch replays, Sprites and sounds, Changelog, GitHub, Feedback (%s)" % [n[1] for n in info["nav"]])
            check(all(hrefs[k][2] == "_blank" for k in ("Sprites and sounds", "Changelog", "GitHub", "Feedback")) and hrefs["Watch live"][2] is None and hrefs["Watch replays"][2] is None, "the links that leave the front page keep their new tab, the two that watch a match do not")
            check(info["notice"] == LOBBY_NOTICE and info["noticeShown"], "the footer says that online matches are recorded, kept for 30 days and public with the players' names (%r)" % info["notice"])
            stats = json.loads(value("JSON.stringify({hidden: document.getElementById('stats').hidden, text: document.getElementById('stats').textContent.replace(/\\s+/g, ' ').trim()})"))
            check(stats["hidden"] or re.match(r"^\d[\d,]* matches? being played · \d[\d,]* players? online(\d[\d,]* games? played \(\d[\d,]* today\))?$", stats["text"]) is not None,
                  "the line of numbers is either not there (a site with no /stats) or says what it counts: matches and players now, and, where the site counts them, the games played (%r; the dot between the two is drawn by the page's style)" % (stats["text"],))
            check(info["scrollW"] <= info["innerW"], "no horizontal scroll at 1440")

            # the picture's shape: 16:9 is the default, 4:3 is chosen under Screen and remembered
            check(info["aspect"] == [False, False, True, False, "16:9"], "Screen: 16:9 is chosen on a first visit, and the other three are not (%s)" % info["aspect"])
            check(info["shapeLabels"] == ["Classic 4:3", "16:10", "16:9", "21:9"], "Screen offers Classic 4:3, 16:10, 16:9 and 21:9, left to right (%s)" % info["shapeLabels"])
            # "fills your screen": under the shape nearest to this computer's screen (None when no shape is within 8 per cent), shown only where there is a mouse
            sw, sh = info["screenSize"]
            ratios = {"4-3": 4 / 3, "16-10": 8 / 5, "16-9": 16 / 9, "21-9": 7 / 3}
            near = min(ratios, key=lambda k: max(ratios[k] / (sw / sh), (sw / sh) / ratios[k])) if sw > 0 and sh > 0 else None
            if near and max(ratios[near] / (sw / sh), (sw / sh) / ratios[near]) - 1 > 0.08:
                near = None
            check([t[0][4:] for t in info["tags"] if not t[1]] == ([near] if near else []), "the tag \"fills your screen\" is under the shape nearest to the %d x %d screen (%s) and under no other (%s)" % (sw, sh, near, info["tags"]))
            check(all(t[2] == "none" for t in info["tags"] if t[1]), "a hidden tag takes no room")
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
            for asked in ("16:10", "21:9"):
                tab.open(web + "?aspect=" + asked, wait=False)
                time.sleep(1.5)
                dashed = asked.replace(":", "-")
                check(value("[document.getElementById('aspect-%s').checked, document.getElementById('aspect-16-9').checked, document.body.getAttribute('data-aspect')]" % dashed) == [True, False, asked], "?aspect=%s in the address chooses %s" % (asked, asked))
            tab.open(web + "?aspect=3:2", wait=False)
            time.sleep(1.5)
            check(value("[document.getElementById('aspect-16-9').checked, document.body.getAttribute('data-aspect')]") == [True, "16:9"], "?aspect=3:2 is not a shape the page offers: ignored, 16:9")
            for picked in ("16:10", "21:9"):                        # the two shapes that were added: pressed, remembered, and the next visit starts with them (the game page reads the same key)
                tab.open(web, wait=False)
                time.sleep(1.5)
                dashed = picked.replace(":", "-")
                real_click("label[for=aspect-%s]" % dashed)
                chosen = json.loads(value("JSON.stringify([document.getElementById('aspect-%s').checked, document.getElementById('aspect-16-9').checked, document.body.getAttribute('data-aspect'), localStorage.getItem('ants.aspect.v2')])" % dashed))
                check(chosen == [True, False, picked, picked], "a press on %s chooses it and the browser remembers it (%s)" % (picked, chosen))
                tab.open(web, wait=False)
                time.sleep(1.5)
                check(value("[document.getElementById('aspect-%s').checked, document.body.getAttribute('data-aspect')]" % dashed) == [True, picked], "... and the next visit starts with %s chosen" % picked)
            tab.ev("localStorage.removeItem('ants.aspect.v2'); localStorage.removeItem('ants.aspect'); 1")

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
                    if m["plan"] != [False, True] or m["link"] or m["copy"]["left"] < panel["left"] or m["copy"]["right"] > panel["right"]:
                        problems.append("the phone's short line, no link field and a Copy link inside the room's box: %s" % ([m["plan"], m["link"]],))
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
            # the tag under the LAST button (21:9) ends with the button: on a computer with a mouse and a 3440 x 1440 screen (nearest to 21:9), in a narrow window, the tag stays inside it and the page does not scroll sideways
            fresh_tab()
            tab.call("Emulation.setDeviceMetricsOverride", {"width": 720, "height": 900, "deviceScaleFactor": 1, "mobile": False, "screenWidth": 3440, "screenHeight": 1440})       # (no touch emulation call: a second one makes Chromium report hover: none)
            tab.open(web, wait=False)
            front_ready()
            wrong_tag = []
            for width in (320, 360, 370, 440, 480, 600, 720):
                tab.call("Emulation.setDeviceMetricsOverride", {"width": width, "height": 900, "deviceScaleFactor": 1, "mobile": False, "screenWidth": 3440, "screenHeight": 1440})
                time.sleep(0.3)
                m = json.loads(value("JSON.stringify({sw: document.documentElement.scrollWidth, iw: document.documentElement.clientWidth, tag: (function () { var t = document.querySelector('.fit-tag:not([hidden])'); if (!t) return null; var r = t.getBoundingClientRect(); return [t.id, Math.round(r.left), Math.round(r.right)]; })()})"))
                if m["sw"] > m["iw"] or not m["tag"] or m["tag"][0] != "fit-21-9" or m["tag"][1] < 0 or m["tag"][2] > m["iw"]:
                    wrong_tag.append((width, m))
            check(not wrong_tag, "a computer with a mouse and a 3440 x 1440 screen, windows of 320 to 720 px: the tag under 21:9 stays inside the page and the page does not scroll sideways (against the width that a scrollbar leaves) (wrong: %s)" % (wrong_tag,))
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
                contrast("390 px")
                real_click("#how-open")
                contrast("390 px with How it works up")
                real_click("#how-close")
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

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("lobby") and not can_play:
            note("lobby: left out (no game server: give --ws-port)")
        if wanted("lobby") and can_play:
            print("[web home] the lobby: a host and players, each a browser of its own, then START into the real game")
            ready = args.ready_timeout

            def person(label, width=1366, height=900):
                p = rejoin.Player(path, label, 0, web, ready)
                people.append(p)
                p.resize(width, height)
                return p

            def named(status):
                return {s.get("seat"): s.get("name") for s in (status.get("players") or [])}

            def phone(p, width, height=700):
                p.call("Emulation.setDeviceMetricsOverride", {"width": width, "height": height, "deviceScaleFactor": 1, "mobile": True, "screenWidth": width, "screenHeight": height})

            def until(p, predicate, timeout=8.0):
                return bool(wait_for(lambda: predicate(lobby_of(p)), timeout, 0.15))

            def say(p):
                return lobby_of(p)["toast"]

            def toast_has(p, text, timeout=4.0):
                return bool(wait_for(lambda: text in lobby_of(p)["toast"], timeout, 0.1))

            host = person("Host")
            host.call("Page.navigate", {"url": web})
            check(bool(wait_for(lambda: seated(host), 25)), "the host opens the front page: its room is made and it sits in a colour")
            hs = lobby_of(host)
            code = code_of(hs)
            codes.append(code)
            check(re.match(r"^[%s]{6}$" % CODE_ALPHABET, code) is not None, "the room's code is six letters and numbers (%s)" % code)
            rename_by_pencil(host, "Ann")
            ok = until(host, lambda s: card_of(s, "green")["name"] == "Ann" and not card_of(s, "green")["ask"])
            check(ok and named(server.room(code) or {}) == {0: "Ann"} and host.ev("localStorage.getItem('ants.name')") == "Ann",
                  "the host changes its name with the pencil: the card shows Ann, the room has it (%s) and the browser remembers it" % named(server.room(code) or {}))

            # ---- a guest opens the link: the name card, then the next free colour
            pal = person("Pal")
            pal.call("Page.navigate", {"url": hs["link"]})
            check(bool(wait_for(lambda: open_name_card(pal), 15)), "a guest opens the room's link and is asked for a name first")
            card = json.loads(pal.ev(NAME_CARD_JS))
            check(card["title"] == "Join the room %s %s" % (code[:3], code[3:]) and card["button"] == "Join" and card["own"] == "Start a room of my own instead" and card["placeholder"] in NAMES
                  and card["hint"] == "The name the other players see. Leave it empty to be called %s." % card["placeholder"] and card["notice"]["text"] == LOBBY_NOTICE and card["notice"]["shown"] and card["input"] == "",
                  "the card says \"Join the room %s %s\", asks for the name (the field is empty, its hint says what an empty field means), and shows the notice about recorded matches (%s)" % (code[:3], code[3:], card["hint"]))
            fill_name_card(pal, "Zoë")
            time.sleep(0.4)
            card = json.loads(pal.ev(NAME_CARD_JS))
            check(card["shown"] and "printable ASCII" in card["msg"] and (server.room(code) or {}).get("joined") == 1, "a name with an accent is refused with the reason and the guest does not join (%r)" % card["msg"][:50])
            fill_name_card(pal, "Bot (Zed)")
            time.sleep(0.4)
            check("Bot (" in json.loads(pal.ev(NAME_CARD_JS))["msg"] and (server.room(code) or {}).get("joined") == 1, "a name that begins like a computer player's is refused too")
            fill_name_card(pal, "Pal")
            ok = wait_for(lambda: seated(pal) and not open_name_card(pal), 15)
            ps = lobby_of(pal)
            hs = lobby_of(host)
            check(bool(ok) and ps["code"] == hs["code"] and ps["heading"] == "Ann’s room" and ps["guest"], "the guest is in the room, which is named after its host (%r, code %s)" % (ps["heading"], ps["code"]))
            status = server.room(code) or {}
            check(status.get("joined") == 2 and named(status) == {0: "Ann", 1: "Pal"} and status.get("leader") == 0,
                  "the next free colour is Red, in seat order (the server: Ann at seat 0, Pal at seat 1, Ann leads: %s)" % named(status))
            pg, pr = card_of(ps, "green"), card_of(ps, "red")
            check(pg["name"] == "Ann" and pg["badge"] == "Host" and pg["stat"] == "Starts the match." and not pg["pencil"] and not pg["remove"] and not pg["grip"]
                  and pr["name"] == "Pal" and pr["badge"] == "You" and pr["stat"] == "Waiting for the host to start." and pr["pencil"] and not pr["remove"] and not pr["grip"] and pr["label"] == "Red: Pal, you",
                  "the guest's screen: Ann is the Host (\"Starts the match.\"), Pal is You with a pencil; no Remove, no grip, nothing to drag (%s)" % {k: pr[k] for k in ("badge", "stat", "pencil", "grip")})
            check(all(c["kind"] == "open" and c["mode"] is None and c["who"] == "Open" and not c["grip"] for c in (card_of(ps, "black"), card_of(ps, "blue")))
                  and ps["start"]["text"] == "Waiting for Ann" and ps["start"]["disabled"] and ps["mapDisabled"] and ps["slotsNote"] == "Ann arranges the colours and starts the match." and ps["plan"].startswith("Ann starts the match when everybody is in."),
                  "... the open colours say Open with no drop-down, START says \"Waiting for Ann\" and is off, the map cannot be changed, and the line under the cards says who arranges them (%r)" % ps["slotsNote"])
            hr = card_of(hs, "red")
            check(hr["name"] == "Pal" and hr["stat"] == "In the room" and hr["remove"] and hr["grip"] and not hr["pencil"] and hr["badge"] == "" and toast_has(host, "Pal joined as Red.")
                  and hs["plan"].startswith("You and Pal play on Treasure. Blue and Black are open and stay empty unless a player joins first."),
                  "the host's screen: Pal in Red with Remove and a grip, a note that Pal joined as Red, and the line under START counts the two (%r)" % hs["plan"])
            check(not any(c["teams"] for c in hs["cards"]) and hs["teamsNote"] == "Teams need three or four players." and not any(c["teams"] or c["tab"] for c in ps["cards"]) and ps["teamsNote"] == "",
                  "with two players there are no team buttons; the host's line says teams need three or four, the guest's has none (%r, %r)" % (hs["teamsNote"], ps["teamsNote"]))

            # ---- the host drags: onto an open colour (a move), onto a taken one (a swap), by the grip's tap and the colour's tap
            seen = {}

            def mid():
                seen["drag"] = json.loads(host.ev("""JSON.stringify({chip: !!document.querySelector('.chip'), chipText: (document.querySelector('.chip') || {}).textContent || '',
                    drop: (function () { var d = document.querySelector('.slot.drop'); return d ? [d.getAttribute('data-c'), d.getAttribute('data-drop')] : null; })(), lifted: Array.prototype.map.call(document.querySelectorAll('#slots li.lifted'), function (l) { return l.getAttribute('data-c'); }) })"""))
            drag_on(host, "#slots li[data-c=red] [data-grip]", "#slots li[data-c=black] .antbox", during=mid)
            check(seen["drag"]["chip"] and "Pal" in seen["drag"]["chipText"] and seen["drag"]["drop"] == ["black", "Move here"] and seen["drag"]["lifted"] == ["red"],
                  "while Pal is dragged a token with the name follows the pointer, the colour it came from is lifted and the open one under it says \"Move here\" (%s)" % seen["drag"])
            ok = until(host, lambda s: card_of(s, "black")["name"] == "Pal" and card_of(s, "red")["kind"] == "open")
            status = server.room(code) or {}
            check(ok and named(status) == {0: "Ann", 3: "Pal"}, "dropped on the open Black: Pal moves there, and the room has it (%s)" % named(status))
            ok = until(pal, lambda s: card_of(s, "black")["badge"] == "You" and card_of(s, "red")["kind"] == "open") and toast_has(pal, "You moved to Black.")
            check(ok, "Pal's own screen follows: You are at Black, and a note says so (%r)" % say(pal))
            quinn = person("Quinn")
            quinn.call("Page.navigate", {"url": hs["link"]})
            wait_for(lambda: open_name_card(quinn), 15)
            fill_name_card(quinn, "Quinn")
            ok = wait_for(lambda: seated(quinn) and not open_name_card(quinn), 15)
            status = server.room(code) or {}
            check(bool(ok) and named(status) == {0: "Ann", 1: "Quinn", 3: "Pal"} and toast_has(host, "Quinn joined as Red."), "a second guest takes the next free colour, Red (%s) and the host is told" % named(status))
            seen.clear()
            drag_on(host, "#slots li[data-c=red] [data-grip]", "#slots li[data-c=black] .antbox", during=mid)
            check(seen["drag"]["drop"] == ["black", "Swap places"], "a person dragged onto a taken colour: it says \"Swap places\" (%s)" % (seen["drag"]["drop"],))
            ok = until(host, lambda s: card_of(s, "black")["name"] == "Quinn" and card_of(s, "red")["name"] == "Pal")
            status = server.room(code) or {}
            check(ok and named(status) == {0: "Ann", 1: "Pal", 3: "Quinn"} and toast_has(pal, "swapped colours."), "... and the two swap places (the server: %s; Pal's note: %r)" % (named(status), say(pal)))
            host.ev("document.querySelector('#slots li[data-c=black] [data-grip]').focus(); 1")
            key_press(host, "Enter", "Enter", 13, "\r")
            st = lobby_of(host)
            check(card_of(st, "black")["picked"] and card_of(st, "red")["target"] and st["slotsNote"] == "Now tap the colour where Quinn goes (tap the same player again to let go).",
                  "the keyboard: Enter on a grip picks the player up, the other colours become targets, and the line says what to do (%r)" % st["slotsNote"])
            key_press(host, "Escape", "Escape", 27)
            check(not any(c["picked"] for c in lobby_of(host)["cards"]), "Esc lets go")
            click_on(host, "#slots li[data-c=black] [data-grip]")
            check(card_of(lobby_of(host), "black")["picked"], "a tap on the grip picks the player up")
            click_on(host, "#slots li[data-c=blue] .antbox")
            ok = until(host, lambda s: card_of(s, "blue")["name"] == "Quinn" and card_of(s, "black")["kind"] == "open" and not any(c["picked"] for c in s["cards"]))
            status = server.room(code) or {}
            check(ok and named(status) == {0: "Ann", 1: "Pal", 2: "Quinn"}, "... and a tap on an open colour puts the player there (the server: %s)" % named(status))
            host.shot(args.shots, "home_lobby_three")

            # ---- three play: Team 1 and Team 2 on each colour that plays
            st = lobby_of(host)
            check([bool(card_of(st, c)["teams"]) for c in COLOURS] == [False, True, True, True] and st["teamsNote"] == "Free for all. For a team, put two colours on the same team." and not st["teamsWarn"],
                  "with three players Team 1 and Team 2 appear under each colour that plays (not under the open one), and the line says free for all (%r)" % st["teamsNote"])
            click_on(host, "#slots button[data-seat='0'][data-side='1']")
            click_on(host, "#slots button[data-seat='1'][data-side='1']")
            ok = until(host, lambda s: s["teamsNote"] == "Teams: Green + Red against Blue.")
            status = server.room(code) or {}
            st = lobby_of(host)
            g, r, b = card_of(st, "green"), card_of(st, "red"), card_of(st, "blue")
            check(ok and g["teams"][0]["on"] and r["teams"][0]["on"] and not g["teams"][1]["on"] and not b["teams"][0]["on"] and b["teams"][0]["dim"] and not b["teams"][1]["dim"],
                  "Team 1 on Green and on Red: \"%s\", the third colour's Team 1 is dashed (full) and its Team 2 is free" % st["teamsNote"])
            print("  note: the server's plan with that team: %r" % status.get("plan"))
            check(status.get("plan", "").endswith(" 0+1"), "the room holds the team: Green and Red (plan %r)" % status.get("plan"))
            click_on(host, "#slots button[data-seat='2'][data-side='1']")
            st = lobby_of(host)
            check(st["teamsWarn"] and st["teamsNote"] == "Team 1 has two colours already. Press one of them to take it off first." and not card_of(st, "blue")["teams"][0]["on"],
                  "a third colour on the full Team 1 is refused, with the reason in the gold team line (%r)" % st["teamsNote"])
            ps = lobby_of(pal)
            check([card_of(ps, c)["tab"] for c in COLOURS] == ["", "Team 1", "Team 1", ""] and ps["teamsNote"] == "Teams: Green + Red against Blue." and not any(c["teams"] for c in ps["cards"]),
                  "the guest sees the teams as read-only tabs, Team 1 on Green and Red, and the line under the cards (%s, %r)" % ([card_of(ps, c)["tab"] for c in COLOURS], ps["teamsNote"]))
            drag_on(host, "#slots li[data-c=red] [data-grip]", "#slots li[data-c=black] .antbox")
            ok = until(host, lambda s: card_of(s, "black")["name"] == "Pal" and s["teamsNote"] == "Teams: Green + Black against Blue.")
            st = lobby_of(host)
            check(ok and card_of(st, "black")["teams"][0]["on"] and toast_has(host, "Pal moved to Black. Team 1 goes along."),
                  "Pal dragged to the open Black takes the team along: Team 1 is on at Black now (%r; %r)" % (st["teamsNote"], st["toast"]))
            ok = until(pal, lambda s: card_of(s, "black")["tab"] == "Team 1" and card_of(s, "green")["tab"] == "Team 1" and card_of(s, "red")["kind"] == "open")
            check(ok, "... on Pal's own screen too (the tabs: %s)" % ([card_of(lobby_of(pal), c)["tab"] for c in COLOURS],))
            click_on(host, "#slots button[data-seat='3'][data-side='1']")
            ok = until(host, lambda s: s["teamsNote"] == "Free for all. For a team, put two colours on the same team.")
            check(ok and (server.room(code) or {}).get("plan", "").endswith("-"), "a press on a lit button takes it off: no team (%r)" % lobby_of(host)["teamsNote"])
            click_on(host, "#slots button[data-seat='3'][data-side='1']")
            ok = until(host, lambda s: s["teamsNote"] == "Teams: Green + Black against Blue.") and wait_for(lambda: (server.room(code) or {}).get("plan", "").endswith(" 0+3"), 5)
            check(bool(ok), "... and a press puts it back, and the room holds it again (plan %r)" % (server.room(code) or {}).get("plan"))

            # ---- a bot of a level
            time.sleep(0.6)                                          # (the host's page has the room's word for the team before the next change is made)
            set_mode(host, "red", "medium")
            ok = until(host, lambda s: card_of(s, "red")["kind"] == "bot")
            st = lobby_of(host)
            status = server.room(code) or {}
            red = card_of(st, "red")
            check(ok and red["mode"] == "medium" and red["stat"] == "Computer player" and not red["remove"] and red["grip"] and status.get("plan", "")[1:2] == "m" and toast_has(host, "Red is now a Medium bot."),
                  "Red set to a Medium bot: its card says Computer player (a grip, no Remove) and the room's plan has it (%r)" % status.get("plan"))
            lit = [[t["on"] for t in card_of(st, c)["teams"]] for c in COLOURS]
            check(lit == [[True, False], [True, False], [False, True], [False, True]] and st["teamsNote"] == "Teams: Green + Black against Red + Blue.",
                  "four play: Black and Green are on Team 1, and the other two colours show Team 2 lit though nobody pressed it; the line says \"%s\" (Team 1 and Team 2 lit, in the order Black, Green, Red, Blue: %s)" % (st["teamsNote"], lit))
            ps = lobby_of(pal)
            check([card_of(ps, c)["tab"] for c in COLOURS] == ["Team 1", "Team 1", "Team 2", "Team 2"] and card_of(ps, "red")["who"] == "Medium bot",
                  "the guest sees them: Black and Green on Team 1, Red (the Medium bot) and Blue on Team 2 (%s)" % [card_of(ps, c)["tab"] for c in COLOURS])
            host.shot(args.shots, "home_lobby_bot")

            # ---- the pencil: a player renames themselves (a refused name, markup, a name)
            rename_by_pencil(pal, "Bot (x", refused=True)
            check("Bot (" in lobby_of(pal)["toast"] and card_of(lobby_of(host), "black")["name"] == "Pal", "a name that begins like a computer player's is refused with the reason, and the host still sees Pal (%r)" % lobby_of(pal)["toast"][:60])
            rename_by_pencil(pal, "<i>P</i> & co")
            ok = until(host, lambda s: card_of(s, "black")["name"] == "<i>P</i> & co")
            hb = card_of(lobby_of(host), "black")
            check(ok and hb["markup"] == 0 and named(server.room(code) or {}).get(3) == "<i>P</i> & co", "a name with markup in it is shown as the text it is (no element is made of it; the room has it as typed)")
            rename_by_pencil(pal, "Pip")
            ok = until(host, lambda s: card_of(s, "black")["name"] == "Pip")
            seen_names = (card_of(lobby_of(host), "black")["name"], named(server.room(code) or {}).get(3), card_of(lobby_of(pal), "black")["name"], pal.ev("localStorage.getItem('ants.name')"))
            check(ok and seen_names == ("Pip", "Pip", "Pip", "Pip"),
                  "the guest renames with the pencil: the host's card and the room say Pip, and the guest's own card and browser remember it (host's card, room, guest's card, guest's storage: %s)" % (seen_names,))

            # ---- Remove, with its question
            click_on(host, "#slots li[data-c=blue] [data-remove]")
            st = lobby_of(host)
            blue = card_of(st, "blue")
            focus = host.ev("document.activeElement.getAttribute('data-remove-no')")
            check(blue["ask"] == "Remove Quinn from the room?RemoveKeep" and blue["classes"].find("removing") != -1 and focus == "2" and not blue["remove"],
                  "Remove asks first: \"Remove Quinn from the room?\" with Remove and Keep, and the focus is on Keep (%r)" % blue["ask"])
            contrast("1366 px, the host's screen with three players, a bot, teams and the question open", t=host, least=40)
            host.shot(args.shots, "home_lobby_ask")
            click_on(host, "#slots li[data-c=black] [data-remove]")
            st = lobby_of(host)
            check(card_of(st, "black")["ask"] != "" and card_of(st, "blue")["ask"] == "" and card_of(st, "blue")["remove"], "one question at a time: asking about Pip closes the one about Quinn")
            key_press(host, "Escape", "Escape", 27)
            st = lobby_of(host)
            check(not any(c["ask"] for c in st["cards"]) and host.ev("document.activeElement.getAttribute('data-remove')") == "3", "Esc is Keep: the question closes and the focus goes back to Remove")
            click_on(host, "#slots li[data-c=blue] [data-remove]")
            click_on(host, "#slots li[data-c=blue] [data-remove-no]")
            st = lobby_of(host)
            check(not any(c["ask"] for c in st["cards"]) and card_of(st, "blue")["name"] == "Quinn" and (server.room(code) or {}).get("joined") == 3, "Keep closes the question and nothing changes (the room still has Quinn)")
            click_on(host, "#slots li[data-c=blue] [data-remove]")
            click_on(host, "#slots li[data-c=blue] [data-remove-yes]")
            ok = until(host, lambda s: card_of(s, "blue")["kind"] == "open")
            status = server.room(code) or {}
            check(ok and named(status) == {0: "Ann", 3: "Pip"} and toast_has(host, "Quinn was removed. Blue is open again."), "Remove: Blue opens at once and the host is told (%r; the room: %s)" % (say(host), named(status)))
            ok = wait_for(lambda: lobby_of(quinn)["banner"]["shown"] and lobby_of(quinn)["heading"] == "Your room", 10)
            qs = lobby_of(quinn)
            own_code = code_of(qs)
            check(bool(ok) and qs["banner"]["text"] == "The host removed you from the room, so this one is yours now. Send the link on if you like." and qs["banner"]["button"] == "OK" and "warn" not in qs["banner"]["cls"],
                  "the removed player's page makes a room of its own under the same name and a green strip says why (%r)" % qs["banner"]["text"])
            check(own_code != code and qs["path"] == "/" and qs["search"] == "" and card_of(qs, "green")["name"] == "Quinn" and (server.room(own_code) or {}).get("lobby") is True and named(server.room(own_code) or {}) == {0: "Quinn"},
                  "... a new code (%s), no room in the address, Quinn the host of it: a lobby on the server" % own_code)
            quinn.shot(args.shots, "home_lobby_removed")
            contrast("1366 px, the removed player's screen", t=quinn, least=40)
            click_on(quinn, "#banner-x")
            check(lobby_of(quinn)["banner"]["shown"] is False, "OK closes the strip")

            # ---- a typed code: "Have a code?"
            click_on(quinn, "#havecode")
            quinn.call("Input.insertText", {"text": code[:3].upper() + " " + code[3:]})
            click_on(quinn, "#joingo")
            asked = wait_for(lambda: open_name_card(quinn), 5)
            card = json.loads(quinn.ev(NAME_CARD_JS))
            check(bool(asked) and card["title"] == "Join the room %s %s" % (code[:3], code[3:]) and card["input"] == "Quinn", "the host's code, typed with a capital and a space, opens the name card with the remembered name (%r)" % card["title"])
            click_on(quinn, "#name-step-go")
            ok = wait_for(lambda: seated(quinn) and lobby_of(quinn)["code"] == lobby_of(host)["code"] and lobby_of(quinn)["guest"], 10)
            qs = lobby_of(quinn)
            status = server.room(code) or {}
            check(bool(ok) and qs["heading"] == "Ann’s room" and card_of(qs, "blue")["badge"] == "You" and named(status) == {0: "Ann", 2: "Quinn", 3: "Pip"}
                  and quinn.ev("document.getElementById('joinbox').hidden && document.getElementById('joincode').value === ''") is True and not qs["banner"]["shown"],
                  "Join: Quinn is back in Ann's room, in Blue, the code field has closed (the removal is no ban: the room has %s)" % named(status))
            check(toast_has(host, "Quinn joined as Blue.") and card_of(lobby_of(host), "blue")["name"] == "Quinn", "the host is told, and the cards agree")
            check((server.room(own_code) or {}).get("joined", 0) == 0 or server.room(own_code) is None, "the room that Quinn had of its own is left (%s)" % ((server.room(own_code) or {}).get("joined"),))

            # ---- a reload brings the seat back
            before = named(server.room(code) or {})
            pal.call("Page.reload", {"ignoreCache": False})
            ok = wait_for(lambda: seated(pal) and lobby_of(pal)["code"] == lobby_of(host)["code"], 15)
            ps = lobby_of(pal)
            status = server.room(code) or {}
            check(bool(ok) and card_of(ps, "black")["badge"] == "You" and card_of(ps, "black")["name"] == "Pip" and named(status) == before and status.get("joined") == 3 and ps["guest"],
                  "a guest reloads the page: it is back in the same room, in the same colour and name, and the room has no second Pip (%s)" % named(status))
            host.call("Page.reload", {"ignoreCache": False})
            ok = wait_for(lambda: seated(host) and lobby_of(host)["code"] == hs["code"], 15)
            st = lobby_of(host)
            check(bool(ok) and not st["guest"] and card_of(st, "green")["badge"] == "You" and card_of(st, "red")["mode"] == "medium" and card_of(st, "blue")["name"] == "Quinn" and st["teamsNote"] == "Teams: Green + Black against Red + Blue." and (server.room(code) or {}).get("leader") == 0,
                  "the host reloads: it leads the same room again, with the bot, the guests and the teams as they were (%r)" % st["teamsNote"])
            check(pal.ev("sessionStorage.getItem('ants.lobby') !== null") and host.ev("sessionStorage.getItem('ants.lobby') !== null") and not any(k.startswith("ants.rejoin.") for k in (host.ev("Object.keys(localStorage)") or [])),
                  "the tab keeps its seat in the session storage; no game key is in the browser before START")

            # ---- a phone: a name of 32 wide letters and the question open
            rename_by_pencil(pal, "W" * 32)
            ok = until(host, lambda s: card_of(s, "black")["name"] == "W" * 32)
            phone(host, 320)
            phone(pal, 320)
            time.sleep(0.6)
            click_on(host, "#slots li[data-c=black] [data-remove]")
            time.sleep(0.4)
            for who, p in (("the host's", host), ("the guest's", pal)):
                m = json.loads(p.ev(LAYOUT_JS))
                bad = [m["strays"][:3] if m["strays"] else None, [(c["c"], c["outside"][:2]) for c in m["cards"] if c["outside"]], [(c["c"], c["dots"]) for c in m["cards"] if c["dots"]]]
                check(bool(ok) and m["scrollW"] <= m["innerW"] and not any(bad), "320 px, %s screen with a name of 32 wide letters in a colour%s: no sideways scroll, nothing out of a card (%s)" % (who, " and the Remove question open" if p is host else "", [b for b in bad if b]))
            host.shot(args.shots, "home_lobby_phone_host")
            pal.shot(args.shots, "home_lobby_phone_guest")
            contrast("320 px, the host's screen with the question open", t=host, least=40)
            key_press(host, "Escape", "Escape", 27)
            rename_by_pencil(pal, "Pip")
            until(host, lambda s: card_of(s, "black")["name"] == "Pip")
            host.resize(1366, 900)
            pal.resize(1366, 900)
            time.sleep(0.5)
            contrast("1366 px, the guest's screen with the teams as tabs", t=pal, least=40)

            # ---- START: everybody goes into the real game
            st = lobby_of(host)
            check(st["start"]["text"] == "START!" and not st["start"]["disabled"] and st["plan"] == "You, a Medium bot, Quinn and Pip play on Treasure.",
                  "before START: the line under it names who plays (%r)" % st["plan"])
            players_now = [host, pal, quinn]
            host.shot(args.shots, "home_lobby_before_start")
            base = site_busy() or {"matches": -1, "players": 0}                  # (what the site counts before START: the server may hold other rooms of the earlier parts)
            click_on(host, "#start")
            seats = {id(host): 0, id(quinn): 2, id(pal): 3}
            names = {id(host): "Ann", id(quinn): "Quinn", id(pal): "Pip"}
            gone = [bool(wait_for(lambda p=p: "join=" in (p.ev("location.search") or ""), 30)) for p in players_now]
            check(all(gone), "START: every page leaves the lobby for the game page, the host's and both guests' (%s)" % gone)
            readied = [bool(wait_for(lambda p=p: p.ev("!!window.isReadyToPlay"), ready)) for p in players_now]
            check(all(readied), "... and every game is ready (%s)" % readied)
            for who, p in (("Ann", host), ("Pip", pal), ("Quinn", quinn)):
                seat = seats[id(p)]
                wait_for(lambda p=p, seat=seat: p.ev("location.search") == "?join=/ws&room=%s&aspect=16:9&seat=%d" % (code, seat), 20)
                given = game_args(p)
                check(given == ["./this.program", "--join-url", door, "--room", code, "--seat", str(seat), "--name", names[id(p)], "--aspect", "16:9"] and p.ev("location.search") == "?join=/ws&room=%s&aspect=16:9&seat=%d" % (code, seat),
                      "%s's game page: the door, the room, seat %d, the name and the shape and nothing else (the server holds the plan); the address keeps the code and the shape, not the name (%s)" % (who, seat, p.ev("location.search")))
                check(stored_rejoin(p) == ["ants.rejoin.%s.%d" % (code, seat)] and not p.ev("!!document.getElementById('name-step') && !document.getElementById('name-step').hidden"),
                      "... the key of the seat is in the browser (%s) and the game does not ask for a name" % stored_rejoin(p))
            began = time.time()
            runs = [match_runs(p) for p in players_now]
            check(all(runs), "every game runs the match by itself, with the \"Get ready\" dialog closed (%s, %.0f s)" % (runs, time.time() - began))
            status = server.room(code) or {}
            bots = status.get("bots") or []
            seated_people = {k: v for k, v in named(status).items() if k not in [b.get("seat") for b in bots]}
            check(status.get("state") == "running" and seated_people == {0: "Ann", 2: "Quinn", 3: "Pip"} and [(b.get("seat"), b.get("level")) for b in bots] == [(1, "medium")] and status.get("joined") == 4 and status.get("ignored_start_requests", 0) == 0,
                  "the server's status: the match runs, Ann, Quinn and Pip at seats 0, 2 and 3, a Medium bot at seat 1, no START that was not the leader's heard (%s)" % dict({k: status.get(k) for k in ("state", "joined", "plan", "ignored_start_requests")}, people=seated_people))
            note("the server's status of the match: %s" % {k: status[k] for k in status if k in ("teams", "team", "plan", "expected", "bots")})
            hosted = site_busy()
            check(hosted is not None and hosted.get("matches") == base["matches"] + 1 and hosted.get("players", 0) >= 3, "the site's /busy shows the match: one match more than before START, with its three people counted (before %s, now %s)" % (base, hosted))
            common, same, conflicts = agree(players_now)
            check(len(common) >= 2 and same and not conflicts, "the three games' state hashes agree at the ticks that all reported (%s)" % common[:6])
            check(not any("out of sync" in d for p in players_now for d in p.dialogs), "no game said that it was out of sync")
            host.shot(args.shots, "home_lobby_game_host")
            # a reload of one game page: the key that START left in the browser brings its seat back, and the match goes on
            quinn.call("Page.reload", {"ignoreCache": False})
            time.sleep(2.0)
            back = wait_for(lambda: quinn.ev("!!window.isReadyToPlay"), ready) and match_runs(quinn, 120)
            status = server.room(code) or {}
            again = {k: v for k, v in named(status).items() if k not in [b.get("seat") for b in status.get("bots") or []]}
            check(bool(back) and quinn.ev("location.search") == "?join=/ws&room=%s&aspect=16:9&seat=2" % code and status.get("joined") == 4 and again == seated_people and stored_rejoin(quinn) == ["ants.rejoin.%s.2" % code],
                  "a game page that is reloaded takes its seat back with the key in the browser: the same address, the same seat, no second Quinn in the room (%s)" % again)
            # a link that is opened now (the match has started): a room of its own, with the strip that says so
            late = person("Late")
            late.call("Page.navigate", {"url": hs["link"]})
            wait_for(lambda: open_name_card(late), 15)
            fill_name_card(late, "Late")
            ok = wait_for(lambda: seated(late) and lobby_of(late)["banner"]["shown"], 15)
            ls = lobby_of(late)
            check(bool(ok) and ls["banner"]["text"] == "The match in that room has already started, so this room is yours instead. Send the link on if you like." and "warn" not in ls["banner"]["cls"] and code_of(ls) != code and not ls["guest"]
                  and ls["search"] == "" and (server.room(code_of(ls)) or {}).get("lobby") is True,
                  "a link opened while the match runs gives a room of its own and a green strip that says so (%r)" % ls["banner"]["text"])
            trouble = console_trouble(people)
            check(not trouble, "no page said anything bad in its console: no exception, no error but a file that is not there (%s)" % trouble[:3])
            for p in people:
                try:
                    p.close()
                except Exception:                                # noqa: BLE001
                    pass
            people.clear()

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("play") and not can_play:
            note("play: left out (no game server: give --ws-port)")
        if wanted("play") and can_play:
            print("[web home] START with a bot in every other colour: this tab goes into a match against Medium bots, which starts by itself")
            clear_storage()
            load(web, settle=1.5)
            front_ready()
            rename_by_pencil(tab, "Bob")
            set_modes(tab, {"red": "medium", "blue": "medium", "black": "medium"})
            st = lobby()
            code = code_of(st)
            codes.append(code)
            check(st["plan"] == "You, a Medium bot, a Medium bot and a Medium bot play on Treasure." and not st["start"]["disabled"] and st["start"]["text"] == "START!" and card_of(st, "green")["name"] == "Bob",
                  "before START: Bob and a Medium bot in each of the other colours, and the line under START says so (%r)" % st["plan"])
            before = pages()
            base = site_busy() or {"matches": -1, "players": 0}                  # (what the site counts before START: the server may hold other rooms of the earlier parts)
            real_click("#start")
            ok = wait_for(lambda: "join=" in tab.ev("location.search"), 20)
            check(bool(ok), "START takes this tab to the game page of the room")
            if ok and wait_for(lambda: tab.ev("!!window.isReadyToPlay"), args.ready_timeout):
                time.sleep(1.0)
                check(pages() == before, "no new tab or window was opened (%d pages before and after)" % before)
                given = game_args(tab)
                check(given == ["./this.program", "--join-url", door, "--room", code, "--seat", "0", "--name", "Bob", "--aspect", "16:9"],
                      "the game's arguments: the site's door, the room (%s: the lobby's own, six letters and numbers), seat 0, the name and the shape, and nothing else (the server holds the plan: the map, the bots, the teams) (%s)" % (code, [x for x in given if x != "./this.program"]))
                check(re.match(r"^[%s]{6}$" % CODE_ALPHABET, code) is not None and tab.ev("location.search") == "?join=/ws&room=%s&aspect=16:9&seat=0" % code,
                      "the address bar keeps the room's code and the shape and the seat that the room gave this window (the game tells the page: it stands at the end), but not the name (%s)" % tab.ev("location.search"))
                check(stored_rejoin(tab) == ["ants.rejoin.%s.0" % code], "the key of the seat is in the browser, for a reload of the game page (%s)" % stored_rejoin(tab))
                check(not tab.ev("document.getElementById('name-step') && !document.getElementById('name-step').hidden"), "the game does not ask for a name (the front page chose it)")
                began = time.time()
                started = match_runs(tab, 60)
                check(started, "the match starts by itself, with no key pressed (no START of the player's after this one, the \"Get ready\" dialog closes by itself: %.0f s)" % (time.time() - began))
                shot("home_play_start")
                if started:
                    status = server.room(code) or {}
                    bots = status.get("bots", [])
                    names = {p.get("seat"): p.get("name") for p in status.get("players", [])}
                    check(status.get("state") == "running" and [(b.get("seat"), b.get("level")) for b in bots] == [(1, "medium"), (2, "medium"), (3, "medium")] and names.get(0) == "Bob" and status.get("joined") == 4,
                          "the server's status: running, a Medium bot at each of the seats 1, 2 and 3 (seated by the leader's START), Bob at seat 0 (%s)" % {"state": status.get("state"), "joined": status.get("joined"), "bots": [(b.get("seat"), b.get("name")) for b in bots]})
                    check(status.get("ignored_start_requests", 0) == 0, "no START that was not the leader's was heard (%s)" % status.get("ignored_start_requests"))
                    hosted = site_busy()
                    check(hosted is not None and hosted.get("matches") == base["matches"] + 1 and hosted.get("players", 0) >= 1, "the site's /busy shows the match: one match more than before START, with its person counted (the bots do not count: before %s, now %s)" % (base, hosted))
                    first = tab.shot()
                    t0 = time.time()
                    time.sleep(max(0.0, args.bot_seconds - 1.0))
                    later = tab.shot()
                    cv = tab.geometry()["canvas"]
                    box = score_strip(cv)
                    changed = aspect.count_changed_pixels(first, later, box[0], box[1], box[2], box[3])
                    shot("home_play_later")
                    check(changed > 60, "the bots' scores at the bottom of the picture changed in %.0f s (%d pixels of the strip): their ants move" % (time.time() - t0, changed))
                    check(tab.ev("Module._ants_match_running()") == 1, "the match is still running in this tab")
                    check(not any("out of sync" in d for d in dialogs), "no dialog said that the game was out of sync")
                    # the Menu: the way back to the front page, in the same tab; it asks first while a match runs
                    before = pages()
                    dialogs.clear()
                    answer["accept"] = False
                    tab.ev("document.getElementById('menu-btn').click(); 1")
                    time.sleep(0.8)
                    check(len(dialogs) == 1 and "menu" in dialogs[0].lower() and "join=" in tab.ev("location.search"), "menu: while a match runs the link asks first (%s) and No stays in the game" % dialogs)
                    answer["accept"] = True
                    tab.ev("document.getElementById('menu-btn').click(); 1")
                    back = wait_for(lambda: tab.ev("location.search") == "" and front_up(), 20)
                    check(back and pages() == before, "menu: Yes goes back to the front page in the same tab (no new tab)")
                    if back:
                        front_ready()
                        remembered = lobby()
                        check(code_of(remembered) != code and card_of(remembered, "green")["name"] == "Bob" and remembered["map"] == "treasure" and tab.ev("localStorage.getItem('ants.name')") == "Bob" and not remembered["guest"],
                              "menu: the front page makes a room again, a new code (%s), for the same name, and the map is Treasure (%s)" % (code_of(remembered), card_of(remembered, "green")["name"]))
                        note("the front page after the Menu of a match that goes on: rejoin line %s (%r)" % ("shown" if remembered["rejoin"] else "not shown", tab.ev("document.getElementById('rejoin-note').textContent")))
            else:
                check(False, "the game page of the room is ready")

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("solo") and not can_play:
            note("solo: left out (no game server: give --ws-port)")
        if wanted("solo") and can_play:
            print("[web home] START with every other colour Nobody: a game for one on this computer, in this tab")
            clear_storage()
            load(web, settle=1.5)
            front_ready()
            rename_by_pencil(tab, "Bob")
            set_modes(tab, {"red": "nobody", "blue": "nobody", "black": "nobody"})
            st = lobby()
            check(not st["start"]["disabled"] and st["start"]["text"] == "START!" and st["plan"] == "A game for one: only your colony is on the map." and not any(c["teams"] for c in st["cards"]),
                  "every other colour Nobody: START is on and the line under it says that it is a game for one (%r)" % st["plan"])
            code = code_of(st)
            before = pages()
            real_click("#start")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/play.html", 20)
            where = tab.ev("location.pathname + location.search") if ok else ""
            check(ok and "map=treasure" in where and "bots=" not in where and "join=" not in where and "room=" not in where and "aspect=16:9" in where,
                  "START takes this tab to /play.html with the map and the shape (16:9, the default): no bots, no room (the game page takes the name out of the address: it is in the game's arguments) (%s)" % where)
            if ok and wait_for(lambda: tab.ev("!!window.isReadyToPlay"), args.ready_timeout):
                check(pages() == before, "no new tab or window was opened (%d pages before and after)" % before)
                given = [x for x in game_args(tab) if x != "./this.program"]
                check("--map" in given and "--play" in given and "--alone" in given and "--bot" not in given and "--join-url" not in given and "--room" not in given and "--teams" not in given and given[given.index("--name") + 1:][:1] == ["Bob"]
                      and given[given.index("--aspect") + 1:][:1] == ["16:9"],
                      "the game's arguments are a game for one on this computer: --map, --play, --alone, the name and the shape 16:9, no --bot, no room, no --teams (%s)" % given)
                check(not tab.ev("document.getElementById('name-step') && !document.getElementById('name-step').hidden"), "the game does not ask for a name (the front page chose it)")
                started = start_by_enter()
                check(started, "the match starts once the quick help is closed (Enter), with the \"Get ready\" dialog, which closes by itself")
                if started:
                    time.sleep(3.0)
                    check(tab.ev("Module._ants_match_running()") == 1, "... and goes on with its one colony (the match does not end at once)")
                    seats = [tab.ev("Module._ants_probe(%d)" % n) for n in (20, 21, 22, 23, 24)]
                    check(seats[0] == 1 and seats[1] > 0 and seats[2:] == [0, 0, 0],
                          "... and only Green's colony is there: the roster is Green alone (1) and Green has ants, while Red, Blue and Black have none (roster %s, ants of Green, Red, Blue, Black %s)" % (seats[0], seats[1:]))
                    check(not any("out of sync" in d for d in dialogs), "no dialog said that the game was out of sync")
                    shot("home_solo_running")
                status = server.room(code) or {}
                check(status.get("joined", 0) == 0 or status.get("state") in ("finished", "ended", None), "the room that the front page made is let go of: nobody is in it now (%s)" % {k: status.get(k) for k in ("state", "joined", "reason")})
            else:
                check(False, "the game page of a game on this computer is ready")
            # Classic 4:3, chosen under Screen, is what reaches the game (the choice is remembered by the browser)
            clear_storage()
            load(web, settle=1.5)
            front_ready()
            rename_by_pencil(tab, "Bob")
            real_click("label[for=aspect-4-3]")
            set_modes(tab, {"red": "nobody", "blue": "nobody", "black": "nobody"})
            real_click("#start")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/play.html", 20)
            where = tab.ev("location.pathname + location.search") if ok else ""
            check(ok and "aspect=4:3" in where, "with Classic 4:3 chosen under Screen, START takes this tab to the game with that shape (%s)" % where)
            if ok and wait_for(lambda: tab.ev("!!window.isReadyToPlay"), args.ready_timeout):
                given = [x for x in game_args(tab) if x != "./this.program"]
                check(given[given.index("--aspect") + 1:][:1] == ["4:3"] and tab.ev("document.getElementById('game-stage').getAttribute('data-aspect')") == "4:3", "... and the game is the 4:3 one (--aspect 4:3, the stage says 4:3)")
            else:
                check(False, "the 4:3 game is ready")

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("leave"):
            print("[web home] Leave game: the results' button and the quit dialog's Yes take this tab back to the front page")
            if not can_play:
                note("leave: the games in a room of the game server are left out (no game server: give --ws-port)")

            def press(name, code, vk, text="", modifiers=0):
                for kind in ("keyDown", "keyUp"):
                    tab.call("Input.dispatchKeyEvent", {"type": kind, "key": name, "code": code, "windowsVirtualKeyCode": vk, "text": text if kind == "keyDown" else "", "modifiers": modifiers})

            def quit_dialog_yes():
                """Ctrl+Q opens the game's quit dialog (the Control key goes down first, as on a keyboard) and Y answers Yes. Whether the dialog was up."""
                tab.ev("document.getElementById('canvas').focus(); 1")
                tab.call("Input.dispatchKeyEvent", {"type": "keyDown", "key": "Control", "code": "ControlLeft", "windowsVirtualKeyCode": 17, "modifiers": 2})
                press("q", "KeyQ", 81, modifiers=2)
                tab.call("Input.dispatchKeyEvent", {"type": "keyUp", "key": "Control", "code": "ControlLeft", "windowsVirtualKeyCode": 17, "modifiers": 0})
                opened = bool(wait_for(lambda: tab.ev("Module._ants_probe(5)") == 1, 5))
                press("y", "KeyY", 89, "y")
                return opened

            def leave_button(shape):
                """Where a person clicks Leave Game on the results, in the canvas's own pixels (the middle of the button): the original's rectangle (ScorecardModal::QUIT_BTN_*, read from the source, so
                that the check follows the layout) moved right by the extra width of the wide page."""
                with open(os.path.join(REPO, "include", "ants_app", "scorecard.hpp"), encoding="utf-8") as f:
                    header = f.read()
                found = {n: re.search(r"QUIT_BTN_%s\s*=\s*(\d+);" % n, header) for n in "XYWH"}
                missing = [n for n, m in found.items() if m is None]
                if missing:
                    raise RuntimeError("include/ants_app/scorecard.hpp has no QUIT_BTN_%s: this check reads the Leave Game button from it" % ", QUIT_BTN_".join(missing))
                x, y, w, h = (int(found[n].group(1)) for n in "XYWH")
                return x + (shape[0] - 640) + w / 2, y + h / 2

            def at_front_page():
                """The tab is on the front page itself: its path "/" with no arguments, and its colour cards."""
                return bool(wait_for(lambda: tab.ev("location.pathname") == "/" and tab.ev("location.search") == "" and front_up(), 20))

            def rejoin_offered():
                return bool(tab.ev("document.getElementById('rejoin') ? !document.getElementById('rejoin').hidden : false"))

            def leave_from_results(shape, what):
                """The quit dialog's Yes with one opponent left ends the match and the results are up; a click on Leave Game. Whether the tab is on the front page after it."""
                check(quit_dialog_yes(), "%s: Ctrl+Q opens the quit dialog" % what)
                ended = bool(wait_for(lambda: tab.ev("Module._ants_match_running()") == 0, 15))
                check(ended, "%s: Yes, with one opponent left, ends the match: the results are up" % what)
                if not ended:
                    return False
                time.sleep(1.5)                                                       # (the results build their rows 250 ms later, and the Leave Game button comes with them)
                shot("home_leave_results_%s" % what.replace(" ", "_").replace(":", "x"))
                lx, ly = leave_button(shape)
                x, y = aspect.screen_of_canvas(tab.geometry(), lx, ly, shape[0], shape[1])
                before = pages()
                tab.click(x, y)
                went = at_front_page()
                check(went and pages() == before, "%s: Leave Game takes this tab back to the front page, the menu of the site (no new tab; now %s)" % (what, tab.ev("location.pathname + location.search") if went else "elsewhere"))
                return went

            def front_with(modes):
                """A first visit to the front page under the name Bob, the other colours set as `modes` says ({"red": "medium", ...}); the room's code."""
                clear_storage()
                load(web, settle=1.5)
                front_ready()
                rename_by_pencil(tab, "Bob")
                set_modes(tab, modes)
                return code_of(lobby())

            for shape_name, shape in (("16:9", (960, 540)), ("4:3", (640, 480))):             # a game on this computer against one Easy bot, each shape of the picture (the button is in another place)
                clear_storage()
                load(web + "play.html?map=tiny&bots=easy,none,none&name=Bob&aspect=" + shape_name, ready=True, settle=1.5)
                started = start_by_enter()
                check(started, "a game on this computer against one bot (%s) starts" % shape_name)
                if started:
                    leave_from_results(shape, "game on this computer %s" % shape_name)
                if can_play and front_up():
                    front_ready()
                    check(rejoin_offered() is False, "... and the front page offers no rejoin (a game on this computer has no room)")

            if can_play:
                # a match in a room of the game server: the lobby's START with one Medium bot (the other colours Nobody)
                code = front_with({"red": "medium", "blue": "nobody", "black": "nobody"})
                real_click("#start")
                ok = wait_for(lambda: "join=" in tab.ev("location.search"), 20) and wait_for(lambda: tab.ev("!!window.isReadyToPlay"), args.ready_timeout)
                check(bool(ok), "START with one bot takes this tab to the game page of a room")
                if ok:
                    code = tab.ev("(function () { var a = ANTS_ARGS; return a[a.indexOf('--room') + 1]; })()")
                    started = bool(close_quick_help() and wait_for(lambda: tab.ev("Module._ants_match_running()") == 1, 30) and wait_for(lambda: tab.ev("Module._ants_probe(5)") == 0, 60, 0.5))
                    check(started, "the match against the bot starts by itself (room %s)" % code)
                    if started and leave_from_results((960, 540), "match in a room"):
                        front_ready()
                        check(not rejoin_offered(), "... and the front page does not offer to rejoin the match that was left")
                        check(not any(k.endswith(".0") and code in k for k in (tab.ev("Object.keys(localStorage)") or [])), "... and the browser keeps no key of that seat (%s)" % stored_rejoin(tab))
                # the quit dialog's Yes with three opponents left (a match goes on without the player): the game leaves for the front page at once, and the seat is dropped, not held
                front_with({"red": "medium", "blue": "medium", "black": "medium"})
                real_click("#start")
                ok = wait_for(lambda: "join=" in tab.ev("location.search"), 20) and wait_for(lambda: tab.ev("!!window.isReadyToPlay"), args.ready_timeout)
                check(bool(ok), "START with three bots takes this tab to the game page of a room")
                if ok:
                    code = tab.ev("(function () { var a = ANTS_ARGS; return a[a.indexOf('--room') + 1]; })()")
                    started = bool(close_quick_help() and wait_for(lambda: tab.ev("Module._ants_match_running()") == 1, 30) and wait_for(lambda: tab.ev("Module._ants_probe(5)") == 0, 60, 0.5))
                    check(started, "the match against three bots starts by itself (room %s)" % code)
                    if started:
                        check((server.room(code) or {}).get("joined") == 4, "the room lists the player and the three bots")
                        before = pages()
                        check(quit_dialog_yes(), "match in a room with three bots: Ctrl+Q opens the quit dialog")
                        went = at_front_page()
                        check(went and pages() == before, "... and Yes takes this tab back to the front page at once (no new tab)")
                        dropped = bool(wait_for(lambda: (server.room(code) or {}).get("state") == "finished", 10))
                        status = server.room(code) or {}
                        check(dropped and status.get("reason") == "everybody left", "... the seat is dropped on the server at once, not held for a page that is gone: with nobody left the room ends (%s: %s)" % (status.get("state"), status.get("reason")))
                        if went:
                            front_ready()
                            check(not rejoin_offered(), "... and the front page does not offer to rejoin it")

        if wanted("friend") and not can_play:
            note("friend: left out (no game server: give --ws-port)")
        if wanted("friend") and can_play:
            print("[web home] two people: the host's room, a friend who comes through the link and its name card; START takes both into the match")
            host = rejoin.Player(path, "Host", 0, web, args.ready_timeout)
            people.append(host)
            guest = rejoin.Player(path, "Pal", 1, web, args.ready_timeout, link_seat=False)
            people.append(guest)
            host.call("Page.navigate", {"url": web})
            wait_for(lambda: seated(host), 25)
            rename_by_pencil(host, "Host")
            wait_for(lambda: card_of(lobby_of(host), "green")["name"] == "Host", 8)
            hs = lobby_of(host)
            room_code = code_of(hs)
            codes.append(room_code)
            guest.call("Page.navigate", {"url": hs["link"]})
            asked = wait_for(lambda: open_name_card(guest), 20)
            check(bool(asked), "the friend opens the host's link and is asked for a name first (the link has none)")
            if asked:
                fill_name_card(guest, "Pal")
                ok = wait_for(lambda: seated(guest) and not open_name_card(guest) and lobby_of(guest)["guest"], 15)
                status = server.room(room_code) or {}
                names = {p.get("seat"): p.get("name") for p in status.get("players", [])}
                check(bool(ok) and names == {0: "Host", 1: "Pal"} and status.get("lobby") is True,
                      "the friend is in the room, in Red: the server lists Host at Green and Pal at Red (%s), and the room is still a lobby (%s)" % (names, status.get("state")))
                hs = lobby_of(host)
                gs = lobby_of(guest)
                check(hs["plan"] == "You and Pal play on Treasure. Blue and Black are open and stay empty unless a player joins first." and gs["start"]["disabled"] and gs["start"]["text"] == "Waiting for Host",
                      "the host's line counts the two of them (%r); the friend's START waits for the host" % hs["plan"])
                host.shot(args.shots, "home_friend_lobby")
                base = site_busy() or {"matches": -1, "players": 0}              # (what the site counts before START: the server may hold other rooms of the earlier parts)
                click_on(host, "#start")
                gone = [bool(wait_for(lambda p=p: "join=" in (p.ev("location.search") or ""), 30)) for p in (host, guest)]
                check(all(gone), "START takes both pages to the game page of the room (%s)" % gone)
                ready = [bool(wait_for(lambda p=p: p.ev("!!window.isReadyToPlay"), args.ready_timeout)) for p in (host, guest)]
                check(all(ready), "... and both games are ready (%s)" % ready)
                for who, p, seat in (("Host", host, 0), ("Pal", guest, 1)):
                    given = game_args(p)
                    check(given == ["./this.program", "--join-url", door, "--room", room_code, "--seat", str(seat), "--name", who, "--aspect", "16:9"],
                          "%s's game: the door, the same room, seat %d, the name and the shape (%s)" % (who, seat, [x for x in given if x != "./this.program"]))
                began = time.time()
                started = None
                both = False
                while time.time() - began < 150 and not both:                    # (the match begins by itself: nobody presses START in a game page, and no Enter is needed)
                    status = server.room(room_code) or {}
                    if started is None and status.get("state") in ("running", "loading"):
                        started = time.time() - began
                    both = started is not None and host.in_match() and guest.in_match()
                    time.sleep(0.5)
                check(started is not None, "the match starts by itself once both games are in the room (nobody pressed START in a game page, and no key: %s s after the games were ready)" % (round(started) if started is not None else "never"))
                check(bool(both), "both games run the match, the \"Get ready\" dialog closed")
                status = server.room(room_code) or {}
                names = {p.get("seat"): p.get("name") for p in status.get("players", [])}
                check(status.get("joined") == 2 and names == {0: "Host", 1: "Pal"} and not status.get("bots"), "the server's status: Host at Green, Pal at Red, nobody else (Blue and Black stay empty), no bot (%s)" % names)
                check(status.get("ignored_start_requests", 0) == 0, "no START of a game that did not lead was heard (%s)" % status.get("ignored_start_requests"))
                hosted = site_busy()
                check(hosted is not None and hosted.get("matches") == base["matches"] + 1 and hosted.get("players", 0) >= 2, "the site's /busy shows the match: one match more than before START, with its two people counted (before %s, now %s)" % (base, hosted))
                common, same, conflicts = agree([host, guest], 40)
                check(len(common) >= 2 and same and not conflicts, "the two games' state hashes agree at the ticks that both reported (%s)" % common[:6])
                host.shot(args.shots, "home_friend_host")
                guest.shot(args.shots, "home_friend_guest")
            for p in people:
                try:
                    p.close()
                except Exception:                                        # noqa: BLE001
                    pass
            people.clear()

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("old"):
            print("[web home] the old addresses")
            clear_storage()
            before = pages()
            load(web + "?join=/ws&room=demo-small-2p-abc123&aspect=16:9", ready=False, settle=2.0)
            info = json.loads(value("JSON.stringify({stage: !!document.getElementById('game-stage'), lobby: !!document.getElementById('slots'), ask: !document.getElementById('name-step').hidden, args: ANTS_ARGS})"))
            check(info["stage"] and not info["lobby"] and info["ask"], "a shared game link (/?join=/ws&room=...) opens the game page, which asks for a name")
            check(tab.ev("(function () { var a = document.getElementById('name-step-back'); return !!a && a.getAttribute('href') === '/' && !!a.closest('.name-step') && a.textContent.indexOf('front page') !== -1; })()"),
                  "... and the name step has its way out: a link to the front page inside the card")
            check("--join-url" in info["args"] and "--map" not in info["args"] and "--play" not in info["args"], "... a game of the server: no local parameter reaches it (%s)" % info["args"])
            at = info["args"].index("--room") if "--room" in info["args"] else -1
            check(at != -1 and info["args"][at + 1] == "demo-small-2p-abc123",
                  "... its code is a name like any other (a code of the earlier kind stays valid), and with no create block in the address no block reaches the game (%s)" % info["args"])
            check(not any(x in info["args"] for x in ("--room-map", "--room-seats", "--room-teams", "--room-leader-start", "--platform")), "... none of --room-map, --room-seats, --room-teams, --room-leader-start and --platform is made up")
            load(web + "?embed=1&aspect=16:9", ready=False, settle=1.5)
            check(tab.ev("!!document.getElementById('game-stage') && document.body.classList.contains('embed')"), "/?embed=1 opens the game page as a frame's game")
            load(web + "four.html?room=k7m2xq&roommap=small&roomseats=2&fill=medium", ready=False, settle=2.0)
            wait_for(lambda: open_name_card(tab), 10)
            info = json.loads(value("JSON.stringify({path: location.pathname, search: location.search, front: !!document.getElementById('slots'), game: !!document.getElementById('game-stage')})"))
            check(info["path"] == "/" and info["search"].startswith("?room=k7m2xq&roommap=small&roomseats=2&fill=medium") and info["front"] and not info["game"],
                  "/four.html?room=... goes to /?room=... for good, with its whole query, and that is the front page (%s%s)" % (info["path"], info["search"]))
            card = json.loads(value(NAME_CARD_JS))
            check(card["shown"] and card["title"] == "Join the match k7m 2xq" and card["button"] == "Join" and card["own"] == "← Back to the front page" and card["inside"] and not card["sideways"],
                  "... and the front page asks for the name of the shared link (its code in two groups of three), with its way back to the front page (%r, %r)" % (card["title"], card["own"]))
            check(card["notice"]["text"] == LOBBY_NOTICE and card["notice"]["shown"] and card["notice"]["below"] and card["notice"]["above"] and card["notice"]["inside"],
                  "... and that step has the notice about recorded matches between its line and its way back, inside the card (%s)" % card["notice"])
            real_click("#name-step-own")
            went = wait_for(lambda: tab.ev("location.pathname + location.search") == "/" and front_up() and not open_name_card(tab), 15)
            check(bool(went) and (not can_play or front_ready()), "... and the way back takes this tab to the front page itself (no room in the address%s)" % ("; it makes a room of its own" if can_play else ""))
            load(web + "play.html", ready=True, settle=1.0)
            info = json.loads(value("JSON.stringify({args: ANTS_ARGS})"))
            given = [a for a in info["args"] if a != "./this.program"]                             # (the runtime puts the program's own name in front)
            check(given == ["--aspect", "16:9"], "/play.html with nothing is today's front page: no argument but the picture's shape (%s)" % given)
            shot("home_play_plain")
            load(web + "?map=small&players=1&fill=hard", ready=False, settle=1.5)
            wait_for(lambda: open_name_card(tab), 10)
            card = json.loads(value(NAME_CARD_JS))
            check(card["shown"] and card["button"] == "Play" and card["title"] == "Play a game on this computer" and not card["notice"]["shown"],
                  "/?map=small&players=1&fill=hard still asks for a name (its button says Play; no notice about recorded matches, a game on this computer is not recorded) and plays a game on this computer (%r)" % card["title"])
            real_click("#name-step-go")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/play.html", 20)
            where = tab.ev("location.pathname + location.search") if ok else ""
            check(ok and "map=small" in where and "bots=hard" in where and "join=" not in where, "... after the name it takes this tab to /play.html with the map and the Hard bots (%s)" % where)
            check(pages() == before, "no new tab was opened by any of it")

        # ------------------------------------------------------------------------------------------------------------------------------------------------------------
        if wanted("room"):
            print("[web home] an address that hosts a match: the room panel, and Play in this tab")
            clear_storage()
            load(web + "?map=treasure&players=3&fill=easy&teams=0%2B1", settle=1.5)
            wait_for(lambda: open_name_card(tab), 10)
            card = json.loads(value(NAME_CARD_JS))
            check(card["shown"] and card["title"] == "Host a match" and card["button"] == "Host" and card["notice"]["shown"], "the address asks for a name first (Host a match), with the notice about recorded matches (%r)" % card["title"])
            fill_name_card(tab, "Ann")
            time.sleep(0.8)
            room = tab.ev("document.getElementById('room-code').textContent")
            check(tab.ev("!document.getElementById('room-panel').hidden") and re.match(r"^[%s]{3} [%s]{3}$" % (CODE_ALPHABET, CODE_ALPHABET), room) is not None,
                  "the address makes the room panel, and the room's code is a new one of six letters and numbers (the lobby's alphabet), shown in two groups of three (%s)" % room)
            code = room.replace(" ", "")
            codes.append(code)
            check(tab.ev("document.getElementById('room-map').textContent") == "Treasure map, 3 players" and tab.ev("location.search") == "?room=%s&roommap=treasure&roomseats=3&roomteams=0%%2B1&fill=easy" % code,
                  "... the room's choices are in its create block, which the address bar of the page keeps with the plain code, the bots and no &teams= (%s, %s)" % (tab.ev("document.getElementById('room-map').textContent"), tab.ev("location.search")))
            check(tab.ev("document.body.classList.contains('in-room') && document.getElementById('lobby').hidden && document.getElementById('rejoin').hidden && getComputedStyle(document.querySelector('.mast p')).display === 'none'"),
                  "... in its room mode: the lobby and the header's line give way to the room")
            before = pages()
            tab.ev("document.getElementById('play-tab').click(); 1")
            ok = wait_for(lambda: tab.ev("location.pathname") == "/" and "join=" in tab.ev("location.search"), 15)
            time.sleep(1.0)
            a = json.loads(tab.ev("JSON.stringify({search: location.search, args: ANTS_ARGS})")) if ok else {"search": "", "args": []}
            check(ok and ("room=" + code + "&roommap=treasure&roomseats=3&roomteams=0%2B1&") in a["search"] and re.search(r"[?&]teams=", a["search"]) is None and pages() == before,
                  "Play in this tab takes this tab to the game page of the room (the plain code and its create block, no &teams=: %s), no new tab" % a["search"])
            check("--join-url" in a["args"] and "--fill-bots" in a["args"] and a["args"][a["args"].index("--fill-bots") + 1] == "easy" and "--name" in a["args"], "... with the room, the leader's bots and the name in the game's arguments (%s)" % a["args"])
            argument = lambda name: a["args"][a["args"].index(name) + 1] if name in a["args"] else None
            check(argument("--room") == code and argument("--room-map") == "treasure" and argument("--room-seats") == "3" and argument("--room-teams") == "0+1" and "--teams" not in a["args"] and "--room-leader-start" not in a["args"]
                  and argument("--name") == "Ann",
                  "... the room's choices are the create block that the game sends in its first Hello (--room-map treasure, --room-seats 3, --room-teams 0+1), so there is no --teams, and the name is the one that was typed (%s)" % a["args"])

        if wanted("game"):
            print("[web home] the game page in the front page's look")
            play = web + "play.html"
            fresh_tab()

            def contrast_of(where, at_least, prepare="", restore=""):
                """Every visible text of the game page is at least 4.5:1 (`prepare` shows what is closed: the guide's panels, the More list; `restore` closes it again)."""
                found = json.loads(value(prepare + "; var found = " + CONTRAST_JS + "; " + restore + "; found"))
                check(found["texts"] >= at_least and found["lowest"][0][0] >= 4.5, "%s: the text contrast is at least 4.5:1 for all %d texts (lowest: %s)" % (where, found["texts"], found["lowest"]))

            def hold(pattern):
                """Hold the requests that match `pattern` (the DevTools Fetch domain): they are asked for and never answered until release() is called."""
                held = []

                def on_event(msg):
                    if msg.get("method") == "Fetch.requestPaused" and msg.get("sessionId") == tab.session:
                        held.append(msg["params"]["requestId"])

                tab.dt.handlers.append(on_event)
                tab.call("Fetch.enable", {"patterns": [{"urlPattern": pattern, "requestStage": "Request"}]})

                def release():
                    tab.call("Fetch.disable")
                    tab.dt.handlers.remove(on_event)
                    for request in held:
                        try:
                            tab.call("Fetch.continueRequest", {"requestId": request})
                        except (RuntimeError, TimeoutError):
                            pass
                return release

            def centre_xy(selector):
                return json.loads(value("JSON.stringify((function () { var b = document.querySelector(%s).getBoundingClientRect(); return [b.x + b.width / 2, b.y + b.height / 2]; })())" % json.dumps(selector)))

            # --- the look, and the loading screen: the game's index.js is held, so that the page stays at its loading screen (the page's own watchdog is 60 s), and then, with a watchdog of
            # 2.5 s, at its failure card
            clear_storage()
            release = hold("*index.js*")
            script = None
            try:
                tab.emulate(1440, 900, 1)
                tab.open(play + "?aspect=16:9", wait=False)
                reached = wait_for(lambda: tab.ev("document.getElementById('status-text').textContent") == "Starting the game...", 90)
                check(bool(reached), "the loading screen: the data downloads and the page says \"Starting the game...\" while the game's program is held")
                loading = json.loads(value("""JSON.stringify((function () {
                    var o = document.getElementById('splash-overlay'), logo = o.querySelector('.splash-logo'), bar = document.getElementById('progress-container'), s = getComputedStyle(o);
                    var l = logo.getBoundingClientRect(), box = document.getElementById('game-container').getBoundingClientRect(), body = getComputedStyle(document.body);
                    var first = document.querySelector('header .btn'), f = getComputedStyle(first);
                    return { shown: s.display !== 'none' && parseFloat(s.opacity) > 0.99, bg: s.backgroundImage, logoLoaded: logo.complete && logo.naturalWidth === 581, logo: [l.width, l.height],
                             barShown: getComputedStyle(bar).display !== 'none', barBg: getComputedStyle(bar).backgroundColor, fill: getComputedStyle(document.getElementById('progress-fill')).backgroundColor,
                             fillWidth: document.getElementById('progress-fill').style.width, old: !!o.querySelector('.splash-title'), text: document.getElementById('status-text').textContent,
                             inside: l.left >= box.left && l.right <= box.right && l.top >= box.top && l.bottom <= box.bottom,
                             page: { bg: body.backgroundImage, font: body.fontFamily, button: f.backgroundColor, face: f.backgroundImage, shadow: f.boxShadow, radius: f.borderTopLeftRadius, frame: getComputedStyle(document.body, '::after').boxShadow,
                                     fontLoaded: Array.from(document.fonts).some(function (x) { return x.family.indexOf('Libre Franklin') !== -1 && x.status === 'loaded'; }) } };
                })())"""))
                check(loading["shown"] and "front/clay.png" in loading["bg"] and not loading["old"], "the loading screen is the clay with no \"ANTS\" word on it (%s)" % loading["bg"])
                check(loading["logoLoaded"] and loading["inside"] and 100 < loading["logo"][0] <= 240, "... it shows the \"ants!\" logo (%.0f x %.0f px), whole and inside the picture's box" % tuple(loading["logo"]))
                check(loading["barShown"] and loading["barBg"] == "rgb(7, 11, 15)" and loading["fill"] == "rgb(43, 99, 87)" and loading["fillWidth"] == "100%", "... and a teal bar in a black box (%s, %s, %s)" % (loading["barBg"], loading["fill"], loading["fillWidth"]))
                page = loading["page"]
                check("front/clay.png" in page["bg"] and page["font"].startswith('"Libre Franklin"') and page["fontLoaded"], "the page is on the clay, in the game's own font Libre Franklin (loaded from the site)")
                check(page["button"] == "rgb(43, 99, 87)" and "rgb(157, 13, 23)" in page["shadow"] and "rgb(43, 95, 67)" in page["frame"] and "157, 13, 23" in page["frame"],
                      "a button of the header is the teal one with the red shadow, and the page has the thin green frame with its red line (%s)" % page["shadow"][:60])
                check(page["radius"].endswith("px") and 6 <= float(page["radius"][:-2]) <= 12, "... and the button's corners are rounded, as on the front page (%s)" % page["radius"])
                check("linear-gradient" in page["face"] and "url(" not in page["face"], "... and its face is lit from above, as on the front page (%s)" % page["face"][:40])
                shot("game_loading_1440")
                contrast_of("the loading screen at 1440 px", 20)
                tab.emulate(390, 844, 2, mobile=True)
                time.sleep(0.6)
                inside = value("(function () { var l = document.querySelector('.splash-logo').getBoundingClientRect(), b = document.getElementById('game-container').getBoundingClientRect(); return l.left >= b.left && l.right <= b.right && l.top >= b.top && l.bottom <= b.bottom; })()")
                check(inside is True, "the loading screen of a phone (390 px): the logo and the message fit the small picture's box")
                shot("game_loading_390")
                contrast_of("the loading screen at 390 px", 20)
                # More opened while the loading screen is up: its list is above the loading screen (the picture's box keeps that screen's layer to itself)
                x, y = centre_xy("header .more summary")
                tab.click(x, y)
                time.sleep(0.4)
                over = json.loads(value("""JSON.stringify((function () { var list = document.querySelector('.more-list'), r = list.getBoundingClientRect(), splash = document.getElementById('splash-overlay'), o = splash.getBoundingClientRect(), was = splash.style.pointerEvents;
                    splash.style.pointerEvents = 'auto';                                 // (the loading screen takes no pointer: let it, so that the point's topmost element is what is painted there)
                    var x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2, mid = document.elementFromPoint(x, y);
                    splash.style.pointerEvents = was;
                    return { open: document.querySelector('header .more').open, above: !!(mid && mid.closest('.more-list')), over: x >= o.left && x <= o.right && y >= o.top && y <= o.bottom }; })())"""))
                check(over["open"] and over["above"] and over["over"], "More opened over the loading screen: its list is on top of it, not under it (%s)" % over)
                shot("game_loading_390_more")
                tab.click(x, y)
                time.sleep(0.3)
                # the watchdog (2.5 s here) puts the failure card up: the front page's notice with the teal button, no logo (the card needs the room)
                script = tab.call("Page.addScriptToEvaluateOnNewDocument", {"source": "window.ANTS_START_TIMEOUT_MS = 2500;"})["identifier"]
                tab.emulate(1440, 900, 1)
                tab.open(play + "?aspect=16:9", wait=False)
                card_up = wait_for(lambda: tab.ev("!!document.getElementById('load-failure')"), 90)
                check(bool(card_up), "the game that does not start puts its failure card up (the page's watchdog)")
                if card_up:
                    card = json.loads(value("""JSON.stringify((function () {
                        var c = document.getElementById('load-failure'), b = c.querySelector('button'), bs = getComputedStyle(b), cs = getComputedStyle(c), box = document.getElementById('game-container').getBoundingClientRect(), r = c.getBoundingClientRect();
                        return { text: c.innerText.replace(/\\s+/g, ' '), button: bs.backgroundColor, shadow: bs.boxShadow, bg: cs.backgroundColor, logo: getComputedStyle(document.querySelector('.splash-logo')).display,
                                 clicks: getComputedStyle(document.getElementById('splash-overlay')).pointerEvents, inside: r.left >= box.left && r.right <= box.right && r.top >= box.top && r.bottom <= box.bottom };
                    })())"""))
                    check("could not be started" in card["text"] and "Reload" in card["text"] and card["button"] == "rgb(43, 99, 87)" and "rgb(157, 13, 23)" in card["shadow"] and card["bg"] == "rgb(59, 13, 16)",
                          "the failure card is the front page's notice with the teal Reload button (%s)" % card["text"][:70])
                    check(card["logo"] == "none" and card["clicks"] == "auto" and card["inside"], "... the logo gives it the room, it takes the clicks, and it is inside the picture's box")
                    shot("game_load_failed")
                    contrast_of("the failure card", 4)
            finally:
                release()
                if script:
                    tab.call("Page.removeScriptToEvaluateOnNewDocument", {"identifier": script})
            fresh_tab()

            # --- the page at 19 widths: no sideways scroll, the header's controls, the picture, the bar and the guide inside the frame
            LAYOUT = """JSON.stringify((function () {
                var de = document.documentElement, rect = function (e) { var b = e.getBoundingClientRect(); return [b.left, b.top, b.right, b.bottom]; };
                var shown = function (e) { return !!e && getComputedStyle(e).display !== 'none' && e.getBoundingClientRect().width > 0 && (!e.checkVisibility || e.checkVisibility()); };
                var controls = Array.prototype.filter.call(document.querySelectorAll('header a, header button, header summary'), shown).map(function (e) { return { text: e.innerText.trim(), rect: rect(e) }; });
                return { inner: [de.clientWidth, window.innerHeight], scroll: [de.scrollWidth, de.scrollHeight], controls: controls, box: rect(document.getElementById('game-container')), bar: rect(document.querySelector('footer.bar')),
                         panel: rect(document.getElementById('info-panel')), header: rect(document.querySelector('header')), more: shown(document.querySelector('header .more')), view: rect(document.getElementById('view-bar')),
                         seg: Array.prototype.filter.call(document.querySelectorAll('.seg button'), shown).map(rect) };
            })())"""
            clear_storage()
            load(play + "?aspect=16:9", ready=True, settle=1.5)
            wrong = {"scroll": [], "header": [], "overlap": [], "frame": [], "picture": [], "foot": []}
            for width in (320, 360, 375, 390, 414, 440, 441, 480, 600, 699, 700, 701, 768, 900, 1024, 1100, 1280, 1440, 1600):
                tab.emulate(width, 900, 1)
                time.sleep(0.5)
                m = json.loads(value(LAYOUT))
                narrow = width <= 700
                if m["scroll"][0] > m["inner"][0]:
                    wrong["scroll"].append((width, m["scroll"][0], m["inner"][0]))
                texts = [c["text"] for c in m["controls"]]
                want = (["", "Menu", "Full" if width <= 440 else "Fullscreen", "More"] if narrow else ["", "Menu", "Sprites and sounds", "Changelog", "Reset", "Fullscreen", "GitHub", "Feedback"])
                height = m["header"][3] - m["header"][1]
                if texts != want or m["more"] != narrow or (narrow and height > 60):
                    wrong["header"].append((width, texts, "More" if m["more"] else "no More", round(height)))
                for i, a in enumerate(m["controls"]):
                    for b in m["controls"][i + 1:]:
                        same_row = min(a["rect"][3], b["rect"][3]) - max(a["rect"][1], b["rect"][1]) > 0.5 * (a["rect"][3] - a["rect"][1])
                        if same_row and min(a["rect"][2], b["rect"][2]) - max(a["rect"][0], b["rect"][0]) > 0.5:
                            wrong["overlap"].append((width, a["text"] or "logo", b["text"] or "logo"))
                if any(c["rect"][0] < 10 or c["rect"][2] > m["inner"][0] - 10 for c in m["controls"]) or any(s[0] < 10 or s[2] > m["inner"][0] - 10 for s in m["seg"]) or m["panel"][0] < 10 or m["panel"][2] > m["inner"][0] - 10:
                    wrong["frame"].append((width, [round(v) for v in m["panel"]]))
                ring, shadow = (5, 3) if narrow else (8, 4)
                box = m["box"]
                if box[0] - ring < 10 or box[2] + ring + shadow > m["inner"][0] - 10 or box[2] - box[0] < 16 or abs((box[0] + box[2]) / 2 - m["inner"][0] / 2) > 1.5:
                    wrong["picture"].append((width, [round(v, 1) for v in box]))
                if abs(m["bar"][0]) > 0.5 or abs(m["bar"][2] - m["inner"][0]) > 0.5:
                    wrong["foot"].append((width, [round(v, 1) for v in m["bar"]]))
            check(not wrong["scroll"], "no sideways scroll from 320 to 1600 px (wrong: %s)" % (wrong["scroll"],))
            check(not wrong["header"], "the header: up to 700 px one row of the logo, Menu, Fullscreen (Full up to 440 px) and More; above it the logo and the seven controls in their order (wrong: %s)" % (wrong["header"],))
            check(not wrong["overlap"], "no control of the header lies over another or over the logo, in one row or two, at every width (wrong: %s)" % (wrong["overlap"][:6],))
            check(not wrong["frame"], "the header's controls, the pair buttons and the guide stay inside the thin green frame at every width (wrong: %s)" % (wrong["frame"],))
            check(not wrong["picture"], "the picture with its ring and shadow is centred and inside the frame at every width (wrong: %s)" % (wrong["picture"],))
            check(not wrong["foot"], "the footer's bar spans the window at every width (wrong: %s)" % (wrong["foot"],))

            # --- 1440 px: the page that is up, in every part
            fresh_tab()
            load(play + "?aspect=16:9", ready=True, settle=1.5)
            shot("game_1440")
            footer = json.loads(value("""JSON.stringify({text: document.querySelector('footer').innerText.replace(/\\s+/g, ' '), ids: [!!document.getElementById('game-version'), !!document.getElementById('game-build-id')],
                links: Array.prototype.map.call(document.querySelectorAll('footer nav a'), function (a) { return a.innerText; }), bg: getComputedStyle(document.querySelector('footer')).backgroundImage})"""))
            check("Version" in footer["text"] and "build" in footer["text"] and "@@" not in footer["text"] and footer["ids"] == [True, True] and "linear-gradient" in footer["bg"] and footer["links"] == ["Menu", "Watch replays", "Sprites and sounds", "Changelog", "GitHub", "Feedback"],
                  "the footer is the front page's emerald bar with the version, the build and the links (%r)" % footer["text"][:90])
            contrast_of("the game page at 1440 px", 60, "document.querySelectorAll('#info-panel details').forEach(function (d) { d.open = true; })")

            # --- the pairs under the game: the chosen button is pressed in, a click keeps its id and what it remembers
            pairs = json.loads(value("""JSON.stringify(['aspect-16-9', 'aspect-4-3', 'lock-on', 'lock-off'].map(function (id) { var e = document.getElementById(id), s = getComputedStyle(e);
                return [id, e.getAttribute('aria-checked'), s.backgroundColor, s.transform, (s.boxShadow.match(/rgb\\(157, 13, 23\\) (\\d+)px (\\d+)px/) || []).slice(1).join('x')]; }))"""))
            pressed, raised = ("rgb(16, 43, 37)", "matrix(1, 0, 0, 1, 1, 2)"), ("rgb(43, 99, 87)", "none")
            check([tuple(p[2:4]) for p in pairs] == [pressed, raised, pressed, raised] and [p[1] for p in pairs] == ["true", "false", "true", "false"],
                  "the pairs under the game: 16:9 and Locked are pressed in (dark, moved), Classic 4:3 and Free are teal (%s)" % pairs)
            check([p[4] for p in pairs] == ["1x1", "2x3", "1x1", "2x3"], "... a teal button has the front page's red shadow (2 x 3 px), a pressed one the small one (1 x 1 px) (%s)" % [p[4] for p in pairs])
            x, y = centre_xy("#lock-off")
            tab.click(x, y)
            time.sleep(0.4)
            after = json.loads(value("JSON.stringify(['lock-on', 'lock-off'].map(function (id) { var e = document.getElementById(id); return [e.getAttribute('aria-checked'), getComputedStyle(e).backgroundColor]; }).concat([localStorage.getItem('ants.pointerlock')]))"))
            check(after == [["false", raised[0]], ["true", pressed[0]], "off"], "Free clicked: it is pressed in, Locked is teal, and the browser remembers \"off\" (%s)" % (after,))
            x, y = centre_xy("#lock-on")
            tab.click(x, y)
            time.sleep(0.4)
            check(value("document.getElementById('lock-on').getAttribute('aria-checked') + '/' + localStorage.getItem('ants.pointerlock')") == "true/on", "Locked clicked again: pressed in, remembered \"on\"")
            x, y = centre_xy("#aspect-4-3")
            tab.click(x, y)
            back = wait_for(lambda: tab.ev("!!window.isReadyToPlay && location.search.indexOf('aspect=4:3') !== -1"), 60)
            time.sleep(1.0)
            chosen = json.loads(value("JSON.stringify([['aspect-16-9', 'aspect-4-3'].map(function (id) { var e = document.getElementById(id); return [e.getAttribute('aria-checked'), getComputedStyle(e).backgroundColor]; }), localStorage.getItem('ants.aspect.v2'), document.getElementById('game-stage').getAttribute('data-aspect')])")) if back else []
            check(chosen == [[["false", raised[0]], ["true", pressed[0]]], "4:3", "4:3"], "Classic 4:3 clicked: the page restarts with it, it is pressed in, 16:9 is teal, the browser remembers it (%s)" % (chosen,))
            shot("game_classic_1440")
            clear_storage()

            # --- a phone held upright and on its side
            tab.emulate(390, 844, 2, mobile=True)
            tab.open(play + "?aspect=16:9", settle=1.5)
            m = json.loads(value(LAYOUT))
            tip = value("getComputedStyle(document.getElementById('mobile-tip-banner')).display")
            panels = value("Array.prototype.map.call(document.querySelectorAll('#info-panel details'), function (d) { return d.open ? 1 : 0; }).reduce(function (a, b) { return a + b; }, 0)")
            check(m["scroll"][0] <= m["inner"][0] and tip == "flex" and panels == 1 and m["more"] is True, "a phone held upright (390 px): no sideways scroll, the tip is shown, one panel of the guide is open, More is there (%s, %s, %d)" % (m["scroll"][0], tip, panels))
            shot("game_390")
            contrast_of("the game page at 390 px", 40, "document.querySelectorAll('#info-panel details').forEach(function (d) { d.open = true; })",
                        "document.querySelectorAll('#info-panel details').forEach(function (d, i) { d.open = i === 0; })")
            # More: a <details>, no script: closed at first, it opens over the picture with the other five links, whole and in the window, and closes again
            check(value("document.querySelector('header .more').open") is False, "More is closed at first")
            x, y = centre_xy("header .more summary")
            tab.click(x, y)
            time.sleep(0.4)
            more = json.loads(value("""JSON.stringify((function () {
                var list = document.querySelector('.more-list'), r = list.getBoundingClientRect(), items = Array.prototype.map.call(list.querySelectorAll('a, button'), function (e) { var b = e.getBoundingClientRect(); return [e.innerText.trim(), b.left, b.right, b.top, b.bottom]; });
                var mid = document.elementFromPoint((r.left + r.right) / 2, r.top + 8), picture = document.getElementById('game-container').getBoundingClientRect();
                return { open: document.querySelector('header .more').open, items: items, list: [r.left, r.right, r.top, r.bottom], above: !!(mid && mid.closest('.more-list')), overPicture: r.top < picture.bottom && r.bottom > picture.top && r.left < picture.right };
            })())"""))
            check(more["open"] and [i[0] for i in more["items"]] == ["Sprites and sounds", "Changelog", "Reset", "GitHub", "Feedback"] and more["list"][0] >= 10 and more["list"][1] <= 390 - 10 and all(i[1] >= 10 and i[2] <= 380 for i in more["items"]),
                  "More opens the other five links, whole and inside the window (%s)" % ([i[0] for i in more["items"]],))
            check(more["above"] and more["overPicture"], "... over the picture and above it (the loading screen's layer does not cover it)")
            shot("game_390_more")
            contrast_of("the More list", 5)
            tab.click(x, y)
            time.sleep(0.3)
            check(value("document.querySelector('header .more').open") is False, "... and closes with the same click")
            tab.emulate(844, 390, 2, mobile=True)
            tab.open(play + "?aspect=16:9", settle=1.5)
            m = json.loads(value(LAYOUT))
            check(m["scroll"][0] <= m["inner"][0] and m["view"][3] <= m["inner"][1] + 1 and m["header"][3] - m["header"][1] <= 60 and not m["more"],
                  "a phone on its side (844 x 390): the header is one slim row, the whole picture and the bar under it fit the window, no sideways scroll (bar ends at %.0f of %d)" % (m["view"][3], m["inner"][1]))
            shot("game_844")
            tab.emulate(1440, 900, 1)

            # --- the logo is the way back to the front page, as Menu is: it asks while a match runs or a room is joined (No stays), and does not ask at the setup screen
            clear_storage()
            load(play, ready=True, settle=1.0)
            before = pages()
            dialogs.clear()
            tab.ev("document.getElementById('logo-link').click(); 1")
            back = wait_for(lambda: tab.ev("location.pathname") == "/" and bool(tab.ev("document.getElementById('slots') ? 1 : 0")), 20)
            check(back and not dialogs and pages() == before, "the logo at the setup screen (no match): back to the front page in this tab, nothing asked (%s)" % dialogs)
            tab.emulate(1440, 900, 1)
            tab.open(play + "?map=treasure&bots=medium&name=Ann", settle=2.0)
            if start_by_enter():
                dialogs.clear()
                answer["accept"] = False
                tab.ev("document.getElementById('logo-link').click(); 1")
                time.sleep(0.8)
                check(len(dialogs) == 1 and "Leave the game and go back to the menu?" in dialogs[0] and tab.ev("location.pathname") == "/play.html",
                      "the logo while a match runs asks \"Leave the game and go back to the menu?\" (%s) and No stays in the game" % dialogs)
                check(tab.ev("Module._ants_match_running()") == 1, "... the match goes on")
                answer["accept"] = True
                tab.ev("document.getElementById('logo-link').click(); 1")
                back = wait_for(lambda: tab.ev("location.pathname") == "/" and bool(tab.ev("document.getElementById('slots') ? 1 : 0")), 20)
                check(back and pages() == before, "... and Yes goes back to the front page in the same tab (no new tab)")
            else:
                check(False, "the local game started (Enter at the quick help)")
            answer["accept"] = True
            dialogs.clear()
            tab.open(play + "?join=/ws&room=k7m2xq&roommap=small&roomseats=2&name=Ann&aspect=16:9", wait=False)
            time.sleep(2.0)
            answer["accept"] = False
            tab.ev("document.getElementById('logo-link').click(); 1")
            time.sleep(0.8)
            check(len(dialogs) == 1 and "menu" in dialogs[0].lower() and tab.ev("location.pathname") == "/play.html", "the logo in a joined room asks too, and No stays (%s)" % dialogs)
            answer["accept"] = True
            dialogs.clear()

            # --- the name step of a shared link: the front page's name step (a banner, the black field, the teal button), whole in a small window, with its notice
            clear_storage()
            for label, width, height, dpr, mobile in (("1440 px", 1440, 900, 1, False), ("390 px", 390, 844, 2, True)):
                tab.emulate(width, height, dpr, mobile)
                tab.open(play + "?join=/ws&room=k7m2xq&roommap=small&roomseats=2&aspect=16:9", wait=False)
                shown = wait_for(lambda: tab.ev("!document.getElementById('name-step').hidden"), 20)
                step = json.loads(value("""JSON.stringify((function () {
                    var c = document.querySelector('.name-step'), r = c.getBoundingClientRect(), i = document.getElementById('name-step-input'), b = document.getElementById('name-step-go'), t = document.getElementById('name-step-title');
                    return { inside: r.left >= 0 && r.right <= window.innerWidth && r.top >= 0 && r.bottom <= window.innerHeight, bg: getComputedStyle(c).backgroundImage, field: getComputedStyle(i).backgroundColor, button: getComputedStyle(b).backgroundColor,
                             title: getComputedStyle(t).backgroundColor, text: t.textContent };
                })())""")) if shown else {}
                check(bool(shown) and step["inside"] and "front/clay.png" in step["bg"] and step["field"] == "rgb(7, 11, 15)" and step["button"] == "rgb(43, 99, 87)" and step["title"] == "rgb(43, 99, 87)",
                      "the name step of a shared link at %s: a clay card with a teal banner, the black field and the teal Join button, whole in the window (%s)" % (label, step))
                check(bool(shown) and step["text"] == "Join the match k7m 2xq", "... its title names the room by its code in two groups of three, as the front page does (%r)" % step.get("text"))
                contrast_of("the name step at %s" % label, 4)
                tab.ev("var i = document.getElementById('name-step-input'); i.value = 'Zo\\u00eb'; document.getElementById('name-step-go').click(); 1")
                time.sleep(0.3)
                note = json.loads(value("JSON.stringify((function () { var m = document.getElementById('name-step-msg'), s = getComputedStyle(m), r = m.getBoundingClientRect(), c = document.querySelector('.name-step').getBoundingClientRect(); return { text: m.textContent, bg: s.backgroundColor, inside: r.bottom <= c.bottom && r.right <= c.right }; })())"))
                check("printable ASCII" in note["text"] and note["bg"] == "rgb(59, 13, 16)" and note["inside"], "... a name that is refused says so in the front page's notice, inside the card (%s)" % note["text"][:50])
                contrast_of("the name step's notice at %s" % label, 5)
                shot("game_name_step_%s" % label.split()[0])
                back = json.loads(value("""JSON.stringify((function () {
                    var c = document.querySelector('.name-step').getBoundingClientRect(), a = document.getElementById('name-step-back'), r = a.getBoundingClientRect(), h = document.querySelector('.name-step-hint').getBoundingClientRect();
                    return { href: a.getAttribute('href'), text: a.textContent, bg: getComputedStyle(a).backgroundColor, inside: r.left >= c.left && r.right <= c.right && r.top >= h.bottom && r.bottom <= c.bottom };
                })())"""))
                check(back["href"] == "/" and "front page" in back["text"] and back["bg"] == "rgb(43, 99, 87)" and back["inside"],
                      "... and its way out at %s: a teal \"Back to the front page\" link under the hint, inside the card (%s)" % (label, back))
                notice = json.loads(value(CARD_NOTICE_JS))
                check(notice["text"] == NOTICE_TEXT and notice["shown"] and notice["sameType"] and notice["order"] and notice["inside"],
                      "... and its notice at %s: online matches are recorded, kept for 30 days and public with the players' names, in the hint's own type, between the hint and the way out, inside the card (%s)" % (label, notice))
                real_click("#name-step-back")
                time.sleep(1.0)
                check(tab.ev("location.pathname + location.search") == "/" and tab.ev("!!document.getElementById('slots')"), "... which takes the friend to the front page (the colour cards, no room in the address)")
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
