#!/usr/bin/env python3
"""The admin page, web/admin.html with web/admin_page.js and web/admin.css (run by ./run_tests.sh --fast and by the CI; docs/MAP_SUBMISSIONS.md "The admin page").

  - the page's own code is RUN (node, when it is installed): what it shows when the site's door lets a person in (the Maps page with the tabs, the rows, the rename box, the decline box, the question before a
    take-down, the empty states, and a chip with the name and the role), to nobody logged in (one card with a link to the Discord login), to a person who is not an admin (the card's heading) and when the door is
    closed or a part is not running (one line); that a later call that the door refuses swaps the page to the same card; that the page has no password box, no sign-in form and no menu; what each button sends with the
    CSRF token of the first answer; and that no text of a person becomes markup (tests/scripts/web_admin_check.js); the check is shown able to fail: broken copies of the script are refused by it;
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
    @unittest.skipUnless(shutil.which("node"), "node is not installed: the admin page was NOT run (tests/scripts/test_web_admin.py)")
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
            "the list is not asked again after a change": ("Promise.resolve(load()).then(release, release);", "release();"),
            "a link leads out of the admin page": ("add(app, header(section.title, true), add(wrap, host));", "add(app, header(section.title, true), add(wrap, host), el('a'));"),
            "the page behind the question is not inert": ("if (n !== d.node && n !== host.parentNode && n !== app) n.setAttribute('inert', '');", ""),
            "the question does not hear the document's keys": ("doc.addEventListener('keydown', d.onKey, true);", ""),
            "the question leaves its key listener behind": ("st.keyOff = function () { doc.removeEventListener('keydown', d.onKey, true); };", "st.keyOff = function () {};"),
            "a bidirectional control stays in player text": ("[\\u061c\\u200e\\u200f\\u202a-\\u202e\\u2066-\\u2069]/g, ''));        // (bidirectional", "[\\u061d]/g, ''));        // (bidirectional"),
            "a bidirectional control stays in a typed name": ("[\\u061c\\u200e\\u200f\\u202a-\\u202e\\u2066-\\u2069]/g, ''));\n            n.setAttribute('data-untrusted', TYPED);", "[\\u061d]/g, ''));\n            n.setAttribute('data-untrusted', TYPED);"),
            "the typed name is lost behind the question": ("st.open.typed = input.value; st.dialog = m;", "st.dialog = m;"),
            "Save name and approve asks no question": ("st.open.typed = input.value; st.dialog = m; st.dialogKind = 'approve'; st.dialogBody = { name: input.value }; draw(); });", "act(m.id, 'approve', { name: input.value }); });"),
            "the question forgets the name that the admin typed": ("act(m.id, 'approve', body || {},", "act(m.id, 'approve', {},"),
            "the decider is not shown": ("if (st.tab !== 'waiting' && m.decided_by) add(line,", "if (false) add(line,"),
            "the textarea has no limit": ("area.maxLength = 200;", ""),
            "a press on the dimmed page does nothing in the Approve and Take down question": ("                onBackdrop(scrim, closeDialog);\n", ""),
            "a drag from the dimmed page into a box closes it": ("hit = down && e.target === node; down = false; });", "hit = down; down = false; });"),
            "a drag from inside a box to the dimmed page closes it": ("hit = down && e.target === node; down = false; });", "hit = e.target === node; down = false; });"),
            "a list that fails shows nothing and says nothing": ("if (refused(r)) return null;\n                    if (r.status !== 200) { toast('The list could not be loaded. Reload the page.'); return null; }", "if (refused(r)) return null;\n                    if (r.status !== 200) { st.data = { counts: {}, maps: [] }; draw(); return null; }"),
            "a typed name is not marked": ("n.setAttribute('data-untrusted', TYPED);", ""),
            "the role Owner is not named": ("var ROLE_NAMES = { admin: 'Admin', owner: 'Owner' };", "var ROLE_NAMES = { admin: 'Admin' };"),
            "the name in the chip is not in a marked element": ("var name = typed('b', null, me.name);", "var name = el('b', null, me.name);"),
            "the name in the chip is put in as HTML": ("var name = typed('b', null, me.name);", "var name = el('b'); name.innerHTML = String(me.name); name.setAttribute('data-untrusted', TYPED);"),
            "a Sign out button comes back": ("return chip;\n        }\n        function card(inner)", "add(chip, button('Sign out', 'sm', function () {}));\n            return chip;\n        }\n        function card(inner)"),
            "a box that hides what is typed comes back": ("input.type = 'text';\n                input.id = 'rename-' + m.id;", "input.type = 'password';\n                input.id = 'rename-' + m.id;"),
            "the token is left off a decline": ("init.headers['X-Ants-Csrf'] = me && me.csrf ? me.csrf : '';", "init.headers['X-Ants-Csrf'] = path.indexOf('decline') < 0 && me && me.csrf ? me.csrf : '';"),
            "a body that is not JSON is taken for no answer": ("function () { return { status: r.status, json: {} }; }", "function () { throw new Error('not json'); }"),
            "the login link leads somewhere else": ("var LOGIN_HREF = '/hub/login?next=admin';", "var LOGIN_HREF = '/hub/login';"),
            "the card for nobody logged in has no link": ("add(c, el('h1', null, CARD_HEAD), el('div', 'l2', LOGIN_LINE), go);", "add(c, el('h1', null, CARD_HEAD), el('div', 'l2', LOGIN_LINE));"),
            "the card for nobody logged in has no line": ("add(c, el('h1', null, CARD_HEAD), el('div', 'l2', LOGIN_LINE), go);", "add(c, el('h1', null, CARD_HEAD), go);"),
            "the login link runs a script": ("go.setAttribute('href', LOGIN_HREF);", "go.setAttribute('href', LOGIN_HREF);\n                go.addEventListener('click', function () {});"),
            "the login link opens another tab": ("go.setAttribute('href', LOGIN_HREF);", "go.setAttribute('href', LOGIN_HREF);\n                go.setAttribute('target', '_blank');"),
            "a person who is not an admin is offered the login": ("if (status === 401) {", "if (status === 401 || status === 403) {"),
            "a person who is not an admin gets no heading": ("} else if (status === 403 && e === 'not_allowed') {", "} else if (status === 403 && e === 'not_allowed' && false) {"),
            "a closed door shows the heading for a person who is not an admin": ("} else if (status === 403 && e === 'not_allowed') {", "} else if (status === 403) {"),
            "the card shows the words of the server": ("add(c, el('h1', null, CARD_HEAD));\n            } else {", "add(c, el('h1', null, json && json.message ? json.message : CARD_HEAD));\n            } else {"),
            "the line that the page is not available is not an alert": ("var m = el('div', 'msg bad', text);\n            m.setAttribute('role', 'alert');", "var m = el('div', 'msg bad', text);"),
            "no answer to the first question shows nothing": ("}, function () { return showRefusal(0, {}); });", "}, function () {});"),
            "an answer without a token is trusted": ("typeof r.json.csrf === 'string' && r.json.csrf !== ''", "true"),
            "a later 401 is a notice": ("if (r.status === 401 || (r.status === 403 && (e === 'not_allowed' || e === 'closed')))", "if (r.status === 403 && (e === 'not_allowed' || e === 'closed'))"),
            "a later 403 not_allowed is a notice": ("(r.status === 403 && (e === 'not_allowed' || e === 'closed'))) {", "(r.status === 403 && (e === 'closed'))) {"),
            "a later 403 closed is a notice": ("(r.status === 403 && (e === 'not_allowed' || e === 'closed'))) {", "(r.status === 403 && (e === 'not_allowed'))) {"),
            "a refusal leaves the key listener of the question behind": ("function showRefusal(status, json) {\n            leave();", "function showRefusal(status, json) {"),
            "a late answer is drawn over the card": ("if (st.dead) return;\n                if (st.keyOff)", "if (st.keyOff)"),
            "a refused list is not noticed": ("if (refused(r)) return null;\n                    if (r.status !== 200) { toast", "if (r.status !== 200) { toast"),
            "a refused change is not noticed": ("release();\n                    if (refused(r)) return;", "release();"),
            "a refused next name is not noticed": ("if (refused(r)) return;\n                        if (r.status === 200)", "if (r.status === 200)"),
            "a refused next name before the rename box is not noticed": ("if (refused(res)) return;\n                                st.open =", "st.open ="),
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
        self.assertEqual(re.findall(r'(?:href|src)="([^"]+)"', page), ["/favicon.png?v=f34bea03", "/front/classic.css", "admin.css", "admin_page.js"])
        self.assertIn('<meta name="robots" content="noindex, nofollow">', page)
        self.assertNotRegex(page, r"<script>|<style|onclick|onload|javascript:")
        css = read("web", "admin.css")
        self.assertNotRegex(css, r"url\(|@import")
        for rule in re.findall(r"\.adm \.chip \{[^}]*\}", css):                                   # (a finger: the tabs are 44 px tall at least, on a phone too)
            self.assertRegex(rule, r"min-height: (4[4-9]|[5-9][0-9])px", rule)
        self.assertEqual(len(re.findall(r"\.adm \.chip \{", css)), 2)                            # (twice: wide and phone)
        self.assertTrue(any(re.search(r"min-height: (4[4-9]|[5-9][0-9])px", r) for r in re.findall(r"\.adm \.btn \{[^}]*\}", css)))        # (the login link is a finger too)
        script = read("web", "admin_page.js")
        self.assertEqual(sorted(set(re.findall(r"""['"](/[A-Za-z][^'"\s]*)""", script))), ["/admin/api/maps/", "/admin/api/maps?state=", "/admin/api/me", "/admin/api/next-name?name=", "/front/logo.png", "/hub/login?next=admin", "/picture"])
        self.assertNotRegex(script.replace("http://www.w3.org/2000/svg", ""), r"https?://")          # (the one name that SVG needs is not an address that is loaded)
        for forbidden in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "setTimeout('", 'setTimeout("', "localStorage", "sessionStorage", "document.cookie", "location.href"):
            self.assertNotIn(forbidden, script, forbidden)

    def test_the_registry_holds_the_sections_and_a_later_one_is_one_more_entry(self):
        script = read("web", "admin_page.js")
        self.assertIn("{ id: 'maps', title: 'Maps players sent', render: renderMaps }", script)
        self.assertIn("section.render(host);", script)                              # (the shell knows no section by name: it calls the one that is chosen)
        shell = script[script.index("function showShell()"):script.index("// ---- the section \"Maps players sent\"")]
        self.assertNotIn("Maps players sent", shell)
        self.assertNotIn("Waiting", shell)

    def test_the_text_of_the_pictures_is_in_the_script(self):
        script = read("web", "admin_page.js")
        for words in ("Maps players sent", "Rename and approve", "Use next name", "Save name and approve",
                      "Name for this map", "Take this map down?", "Players will no longer find it. It can be put back later.", "Put back", "No approved maps yet", "Go to maps waiting",
                      "A name that is already taken is never replaced.", "Declining asks for a short reason that the maker can read. Nothing is deleted.",
                      "Taking a map down does not delete it. Its name stays protected"):
            self.assertIn(words, script)


if __name__ == "__main__":
    unittest.main()
