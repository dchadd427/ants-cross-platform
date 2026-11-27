#!/usr/bin/env python3
"""The admin page, web/admin.html with web/admin_page.js and web/admin.css (run by ./run_tests.sh --fast and by the CI; docs/MAP_SUBMISSIONS.md "The admin page").

  - the page's own code is RUN (node, when it is installed) against a fake of the DOM and a fake of the game server, the hub and /busy (tests/scripts/web_admin_check.js): who is let in and what each of them sees
    (nobody logged in, a player, a moderator, an admin, the owner, a door that is closed or a part that is not running), the chip and its menu, the five sections (Maps, Chat, Bot matches, Log, Status) as tabs and as a select, the
    Maps lists and their questions, the Chat form and its Ban question and its lists, the section Bot matches (Start now, Running, Schedules: what each button sends, the questions, every refusal in words, the badge), the Log
    and its filters, the Status in every state, what each button sends and with which token (the game server's for the maps, the
    hub's for the chat, none for a change that has no token), that a later call that the door refuses swaps the page to the same card, that nothing a person typed becomes markup and that no Discord id is on the page or
    in a call; the check is shown able to fail: broken copies of the script are refused by it;
  - the page has the site's bar and no other link: nothing else of the site links to it, the script has no way to make markup from text (no innerHTML, no document.write, no eval), no address of another site and
    only the addresses of the admin API, the hub's admin API and the few public ones; the style keeps its targets 44 px tall.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "web", "admin_page.js")
BOTS = os.path.join(REPO, "web", "admin_bots.js")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_admin_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_check(script=SCRIPT, bots=BOTS):
    return subprocess.run([shutil.which("node"), CHECK_JS, script, bots], capture_output=True, text=True, timeout=120)


class ThePagesOwnCode(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "node is not installed: the admin page was NOT run (tests/scripts/test_web_admin.py)")
    def test_the_page_shows_and_sends_what_the_pictures_say(self):
        done = run_check()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")

    @unittest.skipUnless(shutil.which("node"), "node is not installed")
    def test_the_check_can_fail(self):
        text = read("web", "admin_page.js")
        broken = {
            "a text becomes markup": ("if (text !== undefined && text !== null) n.textContent = text;", "if (text !== undefined && text !== null) n.innerHTML = text;"),
            "Take down asks no question": ("var td = button('Take down', 'big bad', function () { st.dialog = m; st.dialogKind = 'take-down'; st.dialogBody = null; draw(); });", "var td = button('Take down', 'big bad', function () { act(m.id, 'take-down', {}); });"),
            "Approve asks no question": ("var ap = button('Approve', 'big ok', function () { st.dialog = m; st.dialogKind = 'approve'; st.dialogBody = null; draw(); });", "var ap = button('Approve', 'big ok', function () { act(m.id, 'approve', {}); });"),
            "the Approve question sends the wrong call": ("if (!approve) return act(m.id, 'take-down', {});", "if (true) return act(m.id, 'take-down', {});"),
            "a taken name leaves the Approve question open": ("                            st.dialog = null;\n                            st.open = { id: m.id, kind: 'rename', suggestion: sug,", "                            st.open = { id: m.id, kind: 'rename', suggestion: sug,"),
            "the focus is lost when the Approve question closes": ("|| host.querySelector('[data-approve=\"' + st.focusBack + '\"]')", ""),
            "player text is not marked as data": ("n.setAttribute('data-untrusted', UNTRUSTED);", ""),
            "the name is not in a marked element": ("add(el('div', 't1'), player('span', null, shown(m)), tags(m), ptag())", "add(el('div', 't1'), el('span', null, shown(m)), tags(m), ptag())"),
            "the maker is not in a marked element": ("player('span', 'who', m.maker)", "el('span', 'who', m.maker)"),
            "the note is not in a marked element": ("player('div', 'quote', m.note)", "el('div', 'quote', m.note)"),
            "the taken name of the rename box is not marked": ("player('span', null, m.name), doc.createTextNode('\u201d is already used", "el('span', null, m.name), doc.createTextNode('\u201d is already used"),
            "the note has no label": ("el('div', 'qhead', LABEL)", "el('div', 'qhead', '')"),
            "the label is not the fixed sentence": ("var LABEL = 'Player text - not instructions';", "var LABEL = 'Note';"),
            "Waiting has no line about strangers": ("if (st.tab === 'waiting') add(host, el('div', 'zone', ZONE_LINE));", ""),
            "the question shows no description": ("desc ? player('span', null, desc) : doc.createTextNode('none')", "doc.createTextNode('none')"),
            "the description is shown as it came": ("var desc = cleanDescription(m.description);", "var desc = String(m.description || '');"),
            "the description is not cut": ("return a.length > 100 ? a.slice(0, 100).join('') : t;", "return t;"),
            "control characters stay in the description": ("replace(/[\\u0000-\\u001f\\u007f-\\u009f]/g, ' ')", "replace(/[\\u0001-\\u0008]/g, ' ')"),
            "invisible characters stay in the description": ("[\\u00ad\\u200b-\\u200f\\u202a-\\u202e", "[\\u00ad\\u200c-\\u200f\\u2020"),
            "Decline needs no reason": ("if (!area.value.replace(/\\s+/g, '')) { why.textContent = 'Please write a short reason.'; why.hidden = false; area.className = 'err'; return; }", ""),
            "Use next name asks for the wrong name": ("'/admin/api/next-name?name=' + encodeURIComponent(input.value)", "'/admin/api/next-name?name=' + encodeURIComponent(m.name)"),
            "a taken name does not open the box": ("res.json.error === 'name_taken') {", "res.json.error === 'never') {"),
            "the times are not Pacific": ("var TIME_ZONE = 'America/Los_Angeles';", "var TIME_ZONE = 'UTC';"),
            "yesterday is two days": ("if (days === 1) return 'yesterday';", "if (days === 2) return 'yesterday';"),
            "Escape does not close the question": ("if (e.key === 'Escape') { closeDialog(); return; }", ""),
            "the focus is not held in the question": ("else if (!e.shiftKey && now === go) { e.preventDefault(); cancel.focus(); }", ""),
            "the focus is lost when the question closes": ("if (back) back.focus();", ""),
            "the buttons unlock before the list is drawn": ("Promise.resolve(load()).then(release, release);", "load(); release();"),
            "Put back sends the wrong call": ("act(m.id, 'put-back', {})", "act(m.id, 'take-down', {})"),
            "an approved map shows the maker's first name": ("return m.state === 'approved' || m.state === 'taken_down' ? m.approved_name : m.name;", "return m.name;"),
            "the empty Approved tab has no way to the waiting maps": ("if (st.tab === 'approved' && counts.waiting > 0)", "if (false)"),
            "the list is not asked again after a change": ("Promise.resolve(load()).then(release, release);", "release();"),
            "the question does not hear the document's keys": ("doc.addEventListener('keydown', d.onKey, true);", ""),
            "the question leaves its key listener behind": ("st.keyOff = function () { doc.removeEventListener('keydown', d.onKey, true); };", "st.keyOff = function () {};"),
            "a bidirectional control stays in player text": ("[\\u061c\\u200e\\u200f\\u202a-\\u202e\\u2066-\\u2069]/g, ''));        // (bidirectional", "[\\u061d]/g, ''));        // (bidirectional"),
            "a bidirectional control stays in a typed name": ("[\\u061c\\u200e\\u200f\\u202a-\\u202e\\u2066-\\u2069]/g, ''));\n            n.setAttribute('data-untrusted', TYPED);", "[\\u061d]/g, ''));\n            n.setAttribute('data-untrusted', TYPED);"),
            "the typed name is lost behind the question": ("st.open.typed = input.value; st.dialog = m;", "st.dialog = m;"),
            "Save name and approve asks no question": ("st.open.typed = input.value; st.dialog = m; st.dialogKind = 'approve'; st.dialogBody = { name: input.value }; draw(); });", "act(m.id, 'approve', { name: input.value }); });"),
            "the question forgets the name that the admin typed": ("act(m.id, 'approve', body || {},", "act(m.id, 'approve', {},"),
            "the decider is not shown": ("if (st.tab !== 'waiting' && m.decided_by) add(line,", "if (false) add(line,"),
            "the textarea has no limit": ("area.maxLength = 200;", ""),
            "a drag from the dimmed page into a box closes it": ("hit = down && e.target === node; down = false; });", "hit = down; down = false; });"),
            "a drag from inside a box to the dimmed page closes it": ("hit = down && e.target === node; down = false; });", "hit = e.target === node; down = false; });"),
            "a list that fails shows nothing and says nothing": ("if (refused(r)) return null;\n                    if (r.status !== 200) { toast('The list could not be loaded. Reload the page.'); return null; }", "if (refused(r)) return null;\n                    if (r.status !== 200) { st.data = { counts: {}, maps: [] }; draw(); return null; }"),
            "a typed name is not marked": ("n.setAttribute('data-untrusted', TYPED);", ""),
            "the name in the chip is not in a marked element": ("var name = typed('b', null, me.name);", "var name = el('b', null, me.name);"),
            "the name in the chip is put in as HTML": ("var name = typed('b', null, me.name);", "var name = el('b'); name.innerHTML = String(me.name); name.setAttribute('data-untrusted', TYPED);"),
            "a box that hides what is typed comes back": ("input.type = 'text';\n                input.id = 'rename-' + m.id;", "input.type = 'password';\n                input.id = 'rename-' + m.id;"),
            "a body that is not JSON is taken for no answer": ("function () { return { status: r.status, json: {} }; }", "function () { throw new Error('not json'); }"),
            "the login link leads somewhere else": ("var LOGIN_HREF = '/hub/login?next=admin';", "var LOGIN_HREF = '/hub/login';"),
            "the login link runs a script": ("go.setAttribute('href', LOGIN_HREF);", "go.setAttribute('href', LOGIN_HREF);\n                go.addEventListener('click', function () {});"),
            "the login link opens another tab": ("go.setAttribute('href', LOGIN_HREF);", "go.setAttribute('href', LOGIN_HREF);\n                go.setAttribute('target', '_blank');"),
            "a person who is not an admin is offered the login": ("if (status === 401) {", "if (status === 401 || status === 403) {"),
            "no answer to the first question shows nothing": ("}, function () { return showRefusal(0, {}); });", "}, function () {});"),
            "a late answer is drawn over the card": ("if (st.dead) return;\n                if (st.keyOff)", "if (st.keyOff)"),
            "a refused list is not noticed": ("if (refused(r)) return null;\n                    if (r.status !== 200) { toast", "if (r.status !== 200) { toast"),
            "a refused change is not noticed": ("release();\n                    if (refused(r)) return;", "release();"),
            "a refused next name is not noticed": ("if (refused(r)) return;\n                        if (r.status === 200)", "if (r.status === 200)"),
            "a refused next name before the rename box is not noticed": ("if (refused(res)) return;\n                                st.open =", "st.open ="),
            'no CSRF token is sent': ("init.headers['X-Ants-Csrf'] = token;", "init.headers['X-Ants-Csrf'] = '';"),
            'the token is left off a decline': ("init.headers['X-Ants-Csrf'] = token;", "init.headers['X-Ants-Csrf'] = path.indexOf('decline') < 0 ? token : '';"),
            'the cookie is not sent': ("var init = { method: method, credentials: 'same-origin', headers: {} };", "var init = { method: method, credentials: 'omit', headers: {} };"),
            'the logout is sent without the cookie': ("return env.fetch('/hub/logout', { method: 'POST', credentials: 'same-origin' })", "return env.fetch('/hub/logout', { method: 'POST', credentials: 'omit' })"),
            'a change with no token is sent anyway': ('if (!token) return Promise.resolve({ status: 0, json: {} });', ''),
            'a change of the hub carries the token of the game server': ('var token = isHub(path) ? hub && hub.csrf : me && me.csrf;', 'var token = me && me.csrf;'),
            'a change of the game server carries the token of the hub': ('var token = isHub(path) ? hub && hub.csrf : me && me.csrf;', 'var token = hub && hub.csrf;'),
            'a link leads out of the admin page': ('add(app, pt, sectionBox(), host);', "add(app, pt, sectionBox(), host, el('a'));"),
            'the page behind the Approve question is not inert': ('inertOthers(d.node);', ''),
            'the page behind the Ban question is not inert': ('inertOthers(shell.node);', ''),
            'a press on the dimmed page does nothing in a question': ('            onBackdrop(scrim, closeDialog);\n', ''),
            'the Ban question leaves its key listener behind': ('                    if (st.keyOff) { st.keyOff(); st.keyOff = null; }\n                    if (shell.node.parentNode)', '                    if (shell.node.parentNode)'),
            'the Sign out of the old door comes back': ("var out = el('button', null, 'Log out');", "var out = el('button', null, 'Sign out');"),
            'the card for nobody logged in has no link': ("return add(c, el('div', 'l2', LOGIN_LINE), add(el('div', 'btnrow'), go));", "return add(c, el('div', 'l2', LOGIN_LINE));"),
            'the card for nobody logged in has no line': ("return add(c, el('div', 'l2', LOGIN_LINE), add(el('div', 'btnrow'), go));", "return add(c, add(el('div', 'btnrow'), go));"),
            'the login link leads somewhere else': ("var LOGIN_HREF = '/hub/login?next=admin';", "var LOGIN_HREF = '/hub/login';"),
            'a person who is not an admin is told that the door is shut': ("if (!(status === 403 && (e === 'not_allowed' || e === 'admin'))) {", 'if (!(status === 403 && false)) {'),
            'a closed door shows the card of a person who is not an admin': ("if (!(status === 403 && (e === 'not_allowed' || e === 'admin'))) {", 'if (!(status === 403)) {'),
            "the hub's refusal of an admin is not taken for a refusal of the door": ("if (!(status === 403 && (e === 'not_allowed' || e === 'admin'))) {", "if (!(status === 403 && e === 'not_allowed')) {"),
            'the person who is not allowed is not named': ("if (you && typeof you.name === 'string') card(cardOf(you.role === 'mod' ? 'mod' : 'none', you.name));", ''),
            'the name on the card is not marked as typed text': ("typed('b', null, name), doc.createTextNode(kind === 'mod'", "el('b', null, name), doc.createTextNode(kind === 'mod'"),
            'a moderator is not told where the tools are': ("kind === 'mod' ? ', a moderator. The tools for moderators are on the front page.' : '.'", "'.'"),
            'a person who may be an admin in a minute is not told to wait': ("if (kind === 'none') add(c,", 'if (false) add(c,'),
            'a late name is drawn over another card': ('if (seq !== cardSeq) return;', ''),
            'a name for nobody is drawn as a person': ("if (j && j.login === true && !you) { card(cardOf('out')); return; }", ''),
            "a game server that does not accept the pass is not told": ("if (status === 403 && e === 'closed') { card(cardOf('line', OFF_KEY)); return Promise.resolve(); }", "if (false) { card(cardOf('line', OFF_KEY)); return Promise.resolve(); }"),
            "a submissions door that is off is not told": ("if (status === 503 && e === 'submissions_off') { card(cardOf('line', OFF_SUBMISSIONS)); return Promise.resolve(); }", "if (false) { card(cardOf('line', OFF_SUBMISSIONS)); return Promise.resolve(); }"),
            "a gate that does not answer is told as a shut door without asking the hub": ("card(cardOf('line')); doorOff(seq); return Promise.resolve(); }", "card(cardOf('line', OFF_DOOR)); return Promise.resolve(); }"),
            "a gate that does not answer is never asked about": ("card(cardOf('line')); doorOff(seq); return Promise.resolve(); }", "card(cardOf('line')); return Promise.resolve(); }"),
            "the plain line is not there while the hub is asked": ("card(cardOf('line')); doorOff(seq); return Promise.resolve(); }", "doorOff(seq); return Promise.resolve(); }"),
            "a shut door is told to a person who is logged in and no admin": ("!(j && j.login === true && j.you && j.you.role === 'admin')", "!(j && j.login === true)"),
            "a shut door is told to nobody who is logged in": ("!(j && j.login === true && j.you && j.you.role === 'admin')", "!j"),
            "a shut door is told on any answer of the hub": ("r.status === 503 && r.json && r.json.error === 'door_off') card(cardOf('line', OFF_DOOR))", "true) card(cardOf('line', OFF_DOOR))"),
            "a hub that is not running is told as a shut door": ("r.status === 503 && r.json && r.json.error === 'door_off') card(cardOf('line', OFF_DOOR))", "r.status === 503) card(cardOf('line', OFF_DOOR))"),
            "a shut door is told over a card that came later": ("if (seq === cardSeq && r.status === 503", "if (r.status === 503"),
            "a late answer of who is here still asks the hub": ("if (seq !== cardSeq || !(j && j.login === true", "if (!(j && j.login === true"),
            'the line that the page is not available is not an alert': ("m.setAttribute('role', cls === 'ok' ? 'status' : 'alert');", "m.setAttribute('role', 'status');"),
            "an answer without a token is trusted by the game server's door": ("if (r.status === 200 && r.json && typeof r.json.csrf === 'string' && r.json.csrf !== '') { me = r.json;", 'if (r.status === 200 && r.json) { me = r.json;'),
            "an answer without a token is trusted by the hub's door": ("if (r.status === 200 && r.json && typeof r.json.csrf === 'string' && r.json.csrf !== '') hub =", 'if (r.status === 200 && r.json) hub ='),
            'a later 401 is a notice': ("if (r.status === 401 || (r.status === 403 && (e === 'not_allowed' || e === 'closed' || e === 'admin'))) {", "if ((r.status === 403 && (e === 'not_allowed' || e === 'closed' || e === 'admin'))) {"),
            'a later 403 not_allowed is a notice': ("(r.status === 403 && (e === 'not_allowed' || e === 'closed' || e === 'admin'))) {", "(r.status === 403 && (e === 'closed' || e === 'admin'))) {"),
            'a later 403 closed is a notice': ("(r.status === 403 && (e === 'not_allowed' || e === 'closed' || e === 'admin'))) {", "(r.status === 403 && (e === 'not_allowed' || e === 'admin'))) {"),
            'a later 403 admin of the hub is a notice': ("(r.status === 403 && (e === 'not_allowed' || e === 'closed' || e === 'admin'))) {", "(r.status === 403 && (e === 'not_allowed' || e === 'closed'))) {"),
            'a refusal leaves the key listener of the question behind': ('        function card(inner) {\n            leave();', '        function card(inner) {'),
            'a refusal leaves the menu open': ("            leave();\n            closeMenu(false);\n            app.textContent = '';", "            leave();\n            app.textContent = '';"),
            'the menu has no Log out request': ("return env.fetch('/hub/logout', { method: 'POST', credentials: 'same-origin' })", "return env.fetch('/hub/logout', { method: 'GET', credentials: 'same-origin' })"),
            'Log out does not ask who is here again': ('                return start();\n            }, function () { toast(', '                return null;\n            }, function () { toast('),
            'a logout that is refused is not said': ("if (r.status !== 200) { toast('That did not work. Reload the page.'); return null; }", ''),
            'Escape does not close the menu': ("function onKey(e) { if (e.key === 'Escape') closeMenu(true); }", 'function onKey(e) {}'),
            'a press elsewhere does not close the menu': ('function onPress(e) { if (!wrap.contains(e.target)) closeMenu(false); }', 'function onPress(e) {}'),
            'the menu leaves its listeners behind': ('            menu.off();\n            if (menu.node.parentNode)', '            if (menu.node.parentNode)'),
            'the owner is not in gold': ("el('small', me.role === 'owner' ? 'own' : null, String(me.role))", "el('small', null, String(me.role))"),
            'a tab does not show its section': ("b.addEventListener('click', function () { show(s); });", ''),
            'the select does not change the section': ("select.addEventListener('change', function () { SECTIONS.forEach(function (s) { if (s.id === select.value) show(s); }); });", ''),
            'the arrow keys do not move between the tabs': ("var to = e.key === 'ArrowRight' ? i + 1 : e.key === 'ArrowLeft' ? i - 1 : -1;", 'var to = -1;'),
            'the number of maps that wait is not on the tab': ("if (s.id === 'maps' && waiting > 0) add(b, el('span', 'cnt', String(waiting)));", ''),
            'Ban asks no question': ("var banButton = button('Ban', 'big bad', function () { askBan(); });", "var banButton = button('Ban', 'big bad', function () { var a = named(user.input); if (a) change('ban', { account: a.account }, typed('span', null, a.shown), 'is banned', user.input); });"),
            'the Ban question bans somebody else': ("change('ban', { account: a.account }, typed('span', null, a.shown), 'is banned', user.input); });", "change('ban', { account: user.input.value + 'x' }, typed('span', null, a.shown), 'is banned', user.input); });"),
            'an account number is accepted in a box': ("if (ID_SHAPED.test(t)) return { why: 'That looks like an account number. Type the username instead.' };", ''),
            'a username of digits is sent without its @': ("return { account: /^[0-9]+$/.test(t) ? '@' + t : t, shown: t };", 'return { account: t, shown: t };'),
            'a name is not trimmed': ("var t = String(text === undefined || text === null ? '' : text).replace(/^\\s+|\\s+$/g, '');", "var t = String(text === undefined || text === null ? '' : text);"),
            'the minutes are not limited': ('if (n < 1 || n > MAX_MINUTES) { say(', 'if (n < 1) { say('),
            'minutes with words are taken as a number': ('var n = /^[0-9]{1,5}$/.test(text) ? parseInt(text, 10) : 0;', 'var n = parseInt(text, 10) || 0;'),
            'a button of a list sends the name': ('change(name, { account: x.ref }, x.name ?', 'change(name, { account: x.name || x.ref }, x.name ?'),
            'a change that the hub refuses for the door is not noticed': ("                        release();\n                        if (refused(r)) return;\n                        say(failure(name, r, who), 'bad');", "                        release();\n                        say(failure(name, r, who), 'bad');"),
            'the lists are not asked again after a change of the chat': ('Promise.resolve(loadLists()).then(release, release);', 'release();'),
            'an unknown account says nothing': ("if (r.status === 404 && e === 'who') return [", 'if (false) return ['),
            'a name in a list is not marked as typed text': ("function personName(x) { return x.name ? typed('div', 'pn', x.name)", "function personName(x) { return x.name ? el('div', 'pn', x.name)"),
            'a short code in a list is not marked as typed text': ("function ref(text) { return typed('code', 'refn', text); }", "function ref(text) { return el('code', 'refn', text); }"),
            'a done line is an alert': ('var m = message(null, cls);', "var m = message(null, 'bad');"),
            'the names of the log are not marked': ("if (name) return add(w, typed('span', null, name));", "if (name) return add(w, el('span', null, name));"),
            'the map name of the log is not marked': ("add(wt, doc.createTextNode(MAP_ACTIONS[r.action] || ''), typed('em', null, r.target));", "add(wt, doc.createTextNode(MAP_ACTIONS[r.action] || ''), el('em', null, r.target));"),
            'the reason of a decline in the log is not marked': ("add(el('span', 'why', 'Reason: '), typed('span', null, r.detail))", "add(el('span', 'why', 'Reason: '), el('span', null, r.detail))"),
            'a time that is not a number stops the list': ("    function when(seconds, nowMs) {\n        if (!isTime(seconds)) return '';", '    function when(seconds, nowMs) {'),
            'the end of a mute that is not a time stops the list': ("    function untilWords(seconds, nowMs) {\n        if (!isTime(seconds)) return '';", '    function untilWords(seconds, nowMs) {'),
            'the time that is left of a mute that is not a time stops the list': ("    function leftWords(seconds, nowMs) {\n        if (!isTime(seconds)) return '';", '    function leftWords(seconds, nowMs) {'),
            'the Ban question can be confirmed at once': ("if (tick() - opened < ASK_DELAY) return; closeAsk(); change('ban',", "closeAsk(); change('ban',"),
            'the Approve and Take down questions can be confirmed at once': ('                    if (tick() - st.dialogAt < ASK_DELAY) return;                 // (a press that comes at once is the second click of a double click, not an answer)\n', ''),
            'the delay of a question is none': ('var ASK_DELAY = 500;', 'var ASK_DELAY = 0;'),
            'the Cancel of a question waits too': ("var cancel = button('Cancel', 'ghost', closeAsk);", "var cancel = button('Cancel', 'ghost', function () { if (tick() - opened < ASK_DELAY) return; closeAsk(); });"),
            'the log is not newest first': ('rows.sort(function (a, b) { return b.at - a.at; });', 'rows.sort(function (a, b) { return a.at - b.at; });'),
            'a log that cannot be had is shown as an empty one': ('st.chat = rs[0].status === 200 ? rs[0].json.modlog || [] : null;', 'st.chat = rs[0].status === 200 ? rs[0].json.modlog || [] : [];'),
            'a status that the door refuses is not noticed': ('if (refused(rs[1])) return;', ''),
            'the meter is never amber': ("pct >= 90 ? 'b' : pct >= 70 ? 'w' : ''", "pct >= 90 ? 'b' : ''"),
            'the time of the next try is not said in minutes': ('Math.max(1, Math.ceil((h.retry || 0) / 60))', 'h.retry'),
            "a placeholder is shown as the site's version": ("return v && v.indexOf('@') < 0 ? v : '';", 'return v;'),
            'an answer of the hub of another shape is trusted': ('rs[1].json && rs[1].json.health && rs[1].json.store ? rs[1].json : null', 'rs[1].json'),
            "the game server's counts are asked for with the cookie": ("env.fetch('/busy', { credentials: 'omit' })", "env.fetch('/busy', { credentials: 'same-origin' })"),
        }
        with tempfile.TemporaryDirectory() as folder:
            for why, (old, new) in broken.items():
                self.assertEqual(text.count(old), 1, why)
                path = os.path.join(folder, "admin_page.js")
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new))
                self.assertNotEqual(run_check(path).returncode, 0, "the check accepted a script where " + why)


class TheOnlyWayIsTheTypedAddress(unittest.TestCase):
    def web_files(self):
        out = []
        for base, _, names in os.walk(os.path.join(REPO, "web")):
            for name in names:
                if name.endswith((".html", ".js", ".css", ".json", ".svg", ".txt", ".xml")):
                    out.append(os.path.join(base, name))
        return out

    def test_no_other_file_of_the_site_names_the_address_or_the_file(self):
        mine = {"admin.html", "admin.css", "admin_analytics.css", "admin_page.js", "admin_bots.js", "admin_analytics.js", "admin_map_pick.js"}
        for path in self.web_files():
            if os.path.basename(path) in mine and os.path.dirname(path) == os.path.join(REPO, "web"):
                continue
            if path == os.path.join(REPO, "web", "home", "chat.js"):
                continue                                                                          # (the account menu's one line, below)
            if path in (os.path.join(REPO, "web", "home", "cmds.js"), os.path.join(REPO, "web", "home", "cmds.css")):
                continue                                                                          # (the /bots builder of the chat borrows the admin page's list of maps and reads two of its calls: below)
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
            self.assertNotRegex(text, r"/admin\b|admin\.html|admin_page|admin\.css|maps-review", os.path.relpath(path, REPO))
        for name in ("sitemap.xml", "robots.txt"):
            self.assertFalse(os.path.exists(os.path.join(REPO, "web", name)), name + " would list it")

    def test_the_chats_bots_builder_borrows_only_the_list_of_maps_and_reads_two_calls(self):
        js = re.sub(r"(?m)^\s*//.*|\s//.*", "", read("web", "home", "cmds.js"))                      # (a comment may name them; the code may name only these)
        self.assertEqual(sorted(set(re.findall(r"['\"](/admin[^'\"\s]*)", js))), ["/admin.css", "/admin/api/bot-matches/summary", "/admin/api/bot-options", "/admin_map_pick.js"], "the admin page's list of maps and its style, and two GETs of its API: what runs at once and the maps (tests/scripts/test_web_home_cmds.py)")
        self.assertNotRegex(js, r"admin_page|admin_bots|admin\.html|maps-review")

    def test_the_front_pages_account_menu_names_the_address_once_and_its_behavior_is_run_in_node(self):
        self.assertEqual(read("web", "home", "chat.js").count("/admin"), 1)                       # (the one line for an administrator: what it shows and to whom is RUN by tests/scripts/web_home_chat_check.js)

    def test_the_page_has_the_bar_and_no_other_link_and_loads_only_its_own_files(self):
        page = read("web", "admin.html")
        bar = re.search(r'<header class="sb".*?</header>', page, re.S).group(0)
        self.assertEqual(bar, re.search(r'<header class="sb".*?</header>', read("web", "legal.html"), re.S).group(0))          # (the very markup of the other pages' bar: the build stamps it)
        self.assertEqual(len(re.findall(r"<a\b", page)), len(re.findall(r"<a\b", bar)))                                 # (no link outside the bar)
        self.assertEqual(re.findall(r'(?:href|src)="([^"]+)"', page.replace(bar, "")), ["/favicon.png?v=9ea2152a", "/front/classic.css", "/site_nav.css", "/update_notice.css", "admin.css", "admin_analytics.css", "/site_nav.js", "/update_notice.js", "admin_map_pick.js", "admin_bots.js", "admin_analytics.js", "admin_page.js"])
        self.assertIn('<meta name="robots" content="noindex, nofollow">', page)
        self.assertNotRegex(page, r"<script>|<style|onclick|onload|javascript:")
        self.assertEqual(len(re.findall(r'<div id="app"></div>', page)), 1)
        css = read("web", "admin.css")
        self.assertNotRegex(css, r"url\(|@import")
        for rule in re.findall(r"\.adm \.chip \{[^}]*\}", css):                                   # (a finger: the tabs are 44 px tall at least, on a phone too)
            self.assertRegex(rule, r"min-height: (4[4-9]|[5-9][0-9])px", rule)
        self.assertEqual(len(re.findall(r"\.adm \.chip \{", css)), 2)                            # (twice: wide and phone)
        self.assertTrue(any(re.search(r"min-height: (4[4-9]|[5-9][0-9])px", r) for r in re.findall(r"\.adm \.btn \{[^}]*\}", css)))        # (the login link is a finger too)
        for selector in (".adm .atab", ".adm .acct", ".adm .amenu button, .adm .amenu a", ".adm .inw input", ".adm .asel select"):             # (every other control of the shell, the chat and the menu: 44 px at least)
            rule = re.search(re.escape(selector) + r" \{[^}]*\}", css)
            self.assertIsNotNone(rule, selector)
            self.assertRegex(rule.group(0), r"min-height: (4[4-9]|[5-9][0-9])px", selector)
        self.assertIn("@media (forced-colors: active)", css)
        script = read("web", "admin_page.js")
        self.assertEqual(sorted(set(re.findall(r"""['"](/[A-Za-z][^'"\s]*)""", script))), ["/admin/api/audit?kind=bots", "/admin/api/audit?kind=maps", "/admin/api/maps/", "/admin/api/maps?state=", "/admin/api/me", "/admin/api/next-name?name=", "/busy", "/hub/admin/api/", "/hub/login?next=admin", "/hub/logout", "/hub/me", "/picture"])
        self.assertNotRegex(script.replace("http://www.w3.org/2000/svg", ""), r"https?://")          # (the one name that SVG needs is not an address that is loaded)
        for forbidden in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "setTimeout('", 'setTimeout("', "localStorage", "sessionStorage", "document.cookie", "location.href"):
            self.assertNotIn(forbidden, script, forbidden)

    def test_the_registry_holds_its_sections_and_the_shell_knows_none_by_name(self):          # (the five sections, with the order of their tabs, are run in node: web_admin_check.js)
        script = read("web", "admin_page.js")
        for entry in ("{ id: 'maps', title: 'Maps', render: renderMaps }", "{ id: 'chat', title: 'Chat', render: renderChat }", "{ id: 'log', title: 'Log', render: renderLog }", "{ id: 'status', title: 'Status', render: renderStatus }"):
            self.assertIn(entry, script)
        self.assertIn("section.render(host);", script)                              # (the shell calls the one that is chosen)
        shell = script[script.index("function paintTabs()"):script.index("// ---- the section \"Maps\"")]
        for word in ("Waiting", "Banned", "Muted", "Refresh"):
            self.assertNotIn(word, shell)


class TheChatSectionOnAPhone(unittest.TestCase):
    def test_every_panel_of_the_chat_section_has_its_own_place_when_the_page_is_one_column(self):
        script, css = read("web", "admin_page.js"), read("web", "admin.css")
        chat = script[script.index("function renderChat(host)"):script.index("// ---- the section \"Log\"")]
        panels = re.findall(r"\.className \+= ' (p-[a-z]+)'", chat)
        self.assertEqual(sorted(panels), ["p-banned", "p-form", "p-muted", "p-noimg", "p-pics"])
        places = {name: int(n) for name, n in re.findall(r"\.adm \.(p-[a-z]+) \{ order: (\d+); \}", css)}
        self.assertEqual(sorted(places), sorted(panels), "a panel without an order would come first on a phone")
        self.assertEqual(len(set(places.values())), len(places), "two panels with one place")
        self.assertEqual(max(places, key=places.get), "p-noimg", "the box for image posting comes last")

if __name__ == "__main__":
    unittest.main()
