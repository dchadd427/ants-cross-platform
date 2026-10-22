#!/usr/bin/env python3
"""The owner's page, web/admin.html with web/admin_page.js and web/admin.css (run by ./run_tests.sh --fast and by the CI; docs/MAP_SUBMISSIONS.md "The owner's page").

  - the page's own code is RUN (node, when it is installed): what it shows to nobody (the sign-in box), to an account with a starting password (the choice of its own), to an Admin and to a Moderator (the first
    design's pictures: the account menu, the four tabs, the rows, the rename box, the decline box, the question before a take-down, the users, the password shown once, the log, the empty states), what each button
    sends with the CSRF token, a session that ends, that no password is kept or put in an address, and that no text of a person becomes markup (tests/scripts/web_admin_check.js); the check is shown able to fail:
    broken copies of the script are refused by it;
  - the page is not reachable from anywhere: no other file of web/ names the address or the file, there is no link in the page, and the script has no way to make markup from text (no innerHTML, no
    document.write, no eval) and no address of another site.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "web", "admin_page.js")
CHECK_JS = os.path.join(REPO, "tests", "scripts", "web_admin_check.js")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_check(script=SCRIPT):
    return subprocess.run([shutil.which("node"), CHECK_JS, script], capture_output=True, text=True, timeout=120)


class ThePagesOwnCode(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "node is not installed: the owner's page was NOT run (tests/scripts/test_web_admin.py)")
    def test_the_page_shows_and_sends_what_the_pictures_say(self):
        done = run_check()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r"\d+ checks, 0 failed")

    @unittest.skipUnless(shutil.which("node"), "node is not installed")
    def test_the_check_can_fail(self):
        text = read("web", "admin_page.js")
        broken = {
            "no CSRF token is sent": ("init.headers['X-Ants-Csrf'] = me && me.csrf ? me.csrf : '';", "init.headers['X-Ants-Csrf'] = '';"),
            "the cookie is not sent": ("credentials: 'same-origin'", "credentials: 'omit'"),
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
            "an expired session is not noticed": ("if (r.status === 404) return start();\n                    if (done && done(r)) return;", "if (done && done(r)) return;"),
            "a list that fails shows nothing and says nothing": ("(the session ended: the page asks again who is here)\n                    if (r.status !== 200) { toast('The list could not be loaded. Reload the page.'); return null; }", "(the session ended: the page asks again who is here)\n                    if (r.status !== 200) { st.data = { counts: {}, maps: [] }; draw(); return null; }"),
            "the list is not asked again after a change": ("Promise.resolve(load()).then(release, release);", "release();"),
            "a link leads out of the owner's page": ("add(app, header(section.title, true), add(wrap, host));", "add(app, header(section.title, true), add(wrap, host), el('a'));"),
            "the page behind the question is not inert": ("if (n !== d.node && n !== host.parentNode && n !== app) n.setAttribute('inert', '');", ""),
            "the page behind a box is not inert": ("Array.prototype.slice.call(app.children).forEach(function (n) { n.setAttribute('inert', ''); });", ""),
            "Escape does not close a box": ("if (e.key === 'Escape') { closeModal(); return; }", ""),
            "the question does not hear the document's keys": ("doc.addEventListener('keydown', d.onKey, true);", ""),
            "the question leaves its key listener behind": ("st.keyOff = function () { doc.removeEventListener('keydown', d.onKey, true); };", "st.keyOff = function () {};"),
            "a bidirectional control stays in player text": ("[\\u061c\\u200e\\u200f\\u202a-\\u202e\\u2066-\\u2069]/g, ''));        // (bidirectional", "[\\u061d]/g, ''));        // (bidirectional"),
            "a bidirectional control stays in a typed name": ("[\\u061c\\u200e\\u200f\\u202a-\\u202e\\u2066-\\u2069]/g, ''));\n            n.setAttribute('data-untrusted', TYPED);", "[\\u061d]/g, ''));\n            n.setAttribute('data-untrusted', TYPED);"),
            "the typed name is lost behind the question": ("st.open.typed = input.value; st.dialog = m;", "st.dialog = m;"),
            "Save name and approve asks no question": ("st.open.typed = input.value; st.dialog = m; st.dialogKind = 'approve'; st.dialogBody = { name: input.value }; draw(); });", "act(m.id, 'approve', { name: input.value }); });"),
            "the question forgets the owner's name": ("act(m.id, 'approve', body || {},", "act(m.id, 'approve', {},"),
            "the password box shows what is typed": ("input.type = attrs.type || 'text';", "input.type = 'text';"),
            "the eye does not show the password": ("input.type = shown ? 'text' : 'password';", ""),
            "the sign-in password has the wrong autocomplete": ("{ type: 'password', autocomplete: 'current-password', maxlength: 256 });\n            var go", "{ type: 'password', autocomplete: 'off', maxlength: 256 });\n            var go"),
            "the new password has the wrong autocomplete": ("var first = field('New password', prefix + '-new', { type: 'password', autocomplete: 'new-password'", "var first = field('New password', prefix + '-new', { type: 'password', autocomplete: 'current-password'"),
            "the password goes in the address": ("call('POST', '/admin/api/login', { username: typedName, password: pass.input.value })", "call('POST', '/admin/api/login?p=' + pass.input.value, { username: typedName })"),
            "the sign-in answer is not the same for both": ("else if (r.status === 401) showLogin('Wrong username or password', false, typedName);", "else if (r.status === 401) showLogin('Wrong password', false, typedName);"),
            "a lock does not dim the button": ("if (locked) { go.disabled = true; go.className += ' dis'; }", ""),
            "a starting password gets the maps": ("if (me.must_change) return showChoose();", ""),
            "Save is live when the two differ": ("function ready() { return chars(first.input.value) >= MIN_CHARS && first.input.value === second.input.value; }", "function ready() { return chars(first.input.value) >= MIN_CHARS; }"),
            "eight characters are enough": ("var MIN_CHARS = 12;", "var MIN_CHARS = 8;"),
            "the characters are counted as bytes": ("function chars(text) { return Array.from(String(text)).length; }", "function chars(text) { return unescape(encodeURIComponent(String(text))).length; }"),
            "a Moderator is offered the users": ("if (me.role === 'admin') add(menu, item('Users',", "if (true) add(menu, item('Users',"),
            "the menu does not close with Escape": ("if (e.key === 'Escape') { shut(); btn.focus(); }", ""),
            "a new password does not renew the token": ("me.csrf = r.json && r.json.csrf ? r.json.csrf : me.csrf;", ""),
            "a session that ended is not noticed": ("if (r.status === 404 || r.status === 401 || (r.status === 403 && r.json && r.json.error === 'password_change_required')) { closeModal(); start(); return true; }", ""),
            "the starting role is the greater one": ("choose('moderator');\n                var rolebox", "choose('admin');\n                var rolebox"),
            "Remove asks no question": ("add(ua, button('Remove', 'sm bad', function () { askRemove(u); }));", "add(ua, button('Remove', 'sm bad', function () { act(u, 'remove'); }));"),
            "Reset password asks no question": ("add(ua, button('Reset password', 'sm', function () { askReset(u); }));", "add(ua, button('Reset password', 'sm', function () { act(u, 'reset', function (j) { showPassword('New password for ' + u.name, null, 'New password for ', u.name, j.password, null, u.you ? start : null); }); }));"),
            "the Reset question sends the wrong call": ("closeModal();\n                    act(u, 'reset', function (j) {", "closeModal();\n                    act(u, 'disable', function (j) {"),
            "the Reset question leaves the focus on the red button": ("openModal('Reset this password?', [userCard(u), el('div', 'lead', 'A new password is made and you see it once. The old one stops working at once, and if they are signed in now, they are signed out.')], [cancel, go], cancel);", "openModal('Reset this password?', [userCard(u), el('div', 'lead', 'A new password is made and you see it once. The old one stops working at once, and if they are signed in now, they are signed out.')], [cancel, go], go);"),
            "the note about the last Admin outlives a new user": ("st.notice = null;                                   // (the note about a refused removal", "// (the note about a refused removal"),
            "the last Admin is not told": ("if (r.json && r.json.error === 'last_admin') { st.notice = user; draw(); return; }", ""),
            "an account name is not marked": ("n.setAttribute('data-untrusted', TYPED);", ""),
            "the log's words are not marked": ("add(row, el('span', 'tm', when(e.at, env.now())), typed('span', 'wh', e.who), wt);", "add(row, el('span', 'tm', when(e.at, env.now())), el('span', 'wh', e.who), wt);"),
            "the decider is not shown": ("if (st.tab !== 'waiting' && m.decided_by) add(line,", "if (false) add(line,"),
            "the one-time box has no words": ("el('b', null, 'Shown once.')", "el('b', null, '')"),
            "Copy copies nothing": ("cb.writeText(password)", "cb.writeText('')"),
            "reset of oneself reloads the list under the password": ("if (user.you && action === 'reset') return; load(); return; }", "load(); return; }"),
            "the sign-out sends no token": ("call('POST', '/admin/api/logout', {})", "env.fetch('/admin/api/logout', { method: 'POST', credentials: 'same-origin', headers: {}, body: '{}' })"),
            "the textarea has no limit": ("area.maxLength = 200;", ""),
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
        mine = {"admin.html", "admin.css", "admin_page.js"}
        for path in self.web_files():
            if os.path.basename(path) in mine and os.path.dirname(path) == os.path.join(REPO, "web"):
                continue
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
            self.assertNotRegex(text, r"/admin\b|admin\.html|admin_page|admin\.css|maps-review", os.path.relpath(path, REPO))
        for name in ("sitemap.xml", "robots.txt"):
            self.assertFalse(os.path.exists(os.path.join(REPO, "web", name)), name + " would list it")

    def test_the_page_links_nothing_and_loads_only_its_own_files(self):
        page = read("web", "admin.html")
        self.assertNotRegex(page, r"<a\b")
        self.assertEqual(re.findall(r'(?:href|src)="([^"]+)"', page), ["/favicon.png", "/front/classic.css", "admin.css", "admin_page.js"])
        self.assertIn('<meta name="robots" content="noindex, nofollow">', page)
        self.assertNotRegex(page, r"<script>|<style|onclick|onload|javascript:")
        css = read("web", "admin.css")
        self.assertNotRegex(css, r"url\(|@import")
        for rule in re.findall(r"\.adm \.(?:chip|linkbtn) \{[^}]*\}", css):                   # (a finger: the tabs and the sign-out are 44 px tall at least, on a phone too)
            self.assertRegex(rule, r"min-height: (4[4-9]|[5-9][0-9])px", rule)
        self.assertEqual(len(re.findall(r"\.adm \.(?:chip|linkbtn) \{", css)), 3)           # (chip twice: wide and phone; the sign-out once)
        self.assertNotIn(".signed", css)
        for name in ("eye", "acct", "rcard"):                                                # (the eye, the account button and the role cards are fingers too)
            self.assertTrue(any(re.search(r"(min-height|height): (4[4-9]|[5-9][0-9])px", r) for r in re.findall(r"\.adm \.%s \{[^}]*\}" % name, css)), name)
        script = read("web", "admin_page.js")
        self.assertEqual(sorted(set(re.findall(r"""['"](/[A-Za-z][^'"\s]*)""", script))), ["/admin/api/audit", "/admin/api/login", "/admin/api/logout", "/admin/api/maps/", "/admin/api/maps?state=", "/admin/api/me", "/admin/api/next-name?name=", "/admin/api/password", "/admin/api/users", "/admin/api/users/", "/front/logo.png", "/picture"])
        self.assertNotRegex(script.replace("http://www.w3.org/2000/svg", ""), r"https?://")          # (the one name that SVG needs is not an address that is loaded)
        for forbidden in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "setTimeout('", 'setTimeout("', "localStorage", "sessionStorage", "document.cookie", "location.href"):
            self.assertNotIn(forbidden, script, forbidden)

    def test_the_registry_holds_the_sections_and_a_later_one_is_one_more_entry(self):
        script = read("web", "admin_page.js")
        self.assertIn("{ id: 'maps', title: 'Maps players sent', render: renderMaps, roles: ['admin', 'moderator'] },", script)
        self.assertIn("{ id: 'users', title: 'Users', render: renderUsers, roles: ['admin'] }", script)          # (a section names who may see it; the server holds the same rule)
        self.assertIn("section.render(host);", script)                              # (the shell knows no section by name: it calls the one that is chosen)
        shell = script[script.index("function showShell()"):script.index("// ---- the section \"Maps players sent\"")]
        self.assertNotIn("Maps players sent", shell)
        self.assertNotIn("Waiting", shell)

    def test_the_text_of_the_pictures_is_in_the_script(self):
        script = read("web", "admin_page.js")
        for words in ("Sign in", "Wrong username or password", "Too many tries. Wait a few minutes.", "Choose your own password", "At least 12 characters. That is the only rule.", "Change password",
                      "The current password is not right. Nothing was changed.", "Your password was changed. Use the new one the next time you sign in.", "Shown once.", "Has not chosen a password yet",
                      "Everyone who can sign in to this page. Only an Admin sees this list.", "Remove this user?", "You are the only user", "Passwords are never written here.",
                      "This page is for the owner only", "Maps players sent", "Rename and approve", "Use next name", "Save name and approve",
                      "Name for this map", "Take this map down?", "Players will no longer find it. It can be put back later.", "Put back", "No approved maps yet", "Go to maps waiting",
                      "A name that is already taken is never replaced.", "Declining asks for a short reason that the maker can read. Nothing is deleted.",
                      "Taking a map down does not delete it. Its name stays protected"):
            self.assertIn(words, script)


if __name__ == "__main__":
    unittest.main()
