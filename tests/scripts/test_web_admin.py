#!/usr/bin/env python3
"""The owner's page, web/admin.html with web/admin_page.js and web/admin.css (run by ./run_tests.sh --fast and by the CI; docs/MAP_SUBMISSIONS.md "The owner's page").

  - the page's own code is RUN (node, when it is installed): what it shows to nobody, to a stranger and to the owner (the approved pictures: the logon, "This page is for the owner only", the four tabs, the rows,
    the rename box, the decline box, the question before a take-down, the empty states), what each button sends with the CSRF token, a session that ends, and that no text of a person becomes markup
    (tests/scripts/web_admin_check.js); the check is shown able to fail: broken copies of the script are refused by it;
  - the page is not reachable from anywhere: no other file of web/ names the address or the file, there is no link in the page for the owner or a stranger but the logon's, and the script has no way to make
    markup from text (no innerHTML, no document.write, no eval) and no address of another site.
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
            "a stranger gets the owner's page": ("if (!me.owner) return showNotOwner();\n                return showShell();", "return showShell();"),
            "a page that is off says who is signed in": ("return me ? showLogon() : showNotOwner();", "return showLogon();"),
            "Take down asks no question": ("add(acts, button('Take down', 'big bad', function () { st.dialog = m; draw(); }));", "add(acts, button('Take down', 'big bad', function () { act(m.id, 'take-down', {}); }));"),
            "Decline needs no reason": ("if (!area.value.replace(/\\s+/g, '')) { why.textContent = 'Please write a short reason.'; why.hidden = false; area.className = 'err'; return; }", ""),
            "Use next name asks for the wrong name": ("'/admin/api/next-name?name=' + encodeURIComponent(input.value)", "'/admin/api/next-name?name=' + encodeURIComponent(m.name)"),
            "Save sends no name": ("act(m.id, 'approve', { name: input.value }, function (r) {", "act(m.id, 'approve', {}, function (r) {"),
            "a taken name does not open the box": ("if (res.json && res.json.error === 'name_taken') { st.open = { id: m.id, kind: 'rename', suggestion: res.json.suggestion || '' }; m.name_taken = true; draw(); return true; }", ""),
            "the times are not Pacific": ("var TIME_ZONE = 'America/Los_Angeles';", "var TIME_ZONE = 'UTC';"),
            "yesterday is two days": ("if (days === 1) return 'yesterday';", "if (days === 2) return 'yesterday';"),
            "Escape does not close the question": ("if (e.key === 'Escape') { st.dialog = null; draw(); }", ""),
            "Put back sends the wrong call": ("act(m.id, 'put-back', {})", "act(m.id, 'take-down', {})"),
            "an approved map shows the maker's first name": ("return m.state === 'approved' || m.state === 'taken_down' ? m.approved_name : m.name;", "return m.name;"),
            "the empty Approved tab has no way to the waiting maps": ("if (st.tab === 'approved' && counts.waiting > 0)", "if (false)"),
            "an expired session is not noticed": ("if (r.status === 404) return start();\n                    if (done && done(r)) return;", "if (done && done(r)) return;"),
            "a list that fails shows nothing and says nothing": ("if (r.status !== 200) { toast('The list could not be loaded. Reload the page.'); return null; }", "if (r.status !== 200) { st.data = { counts: {}, maps: [] }; draw(); return null; }"),
            "the list is not asked again after a change": ("if (r.status === 200) { st.dialog = null; load(); return; }", "if (r.status === 200) { st.dialog = null; return; }"),
            "a link leads out of the owner's page": ("add(app, header(section.title), add(wrap, host));", "add(app, header(section.title), add(wrap, host), el('a'));"),
            "the sign-out is a link, not a post": ("f.method = 'post';", "f.method = 'get';"),
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
        script = read("web", "admin_page.js")
        self.assertEqual(sorted(set(re.findall(r"""['"](/[A-Za-z][^'"\s]*)""", script))), ["/admin/api/maps/", "/admin/api/maps?state=", "/admin/api/me", "/admin/api/next-name?name=", "/admin/login", "/admin/logout", "/front/logo.png", "/picture"])
        self.assertNotRegex(script, r"https?://")
        for forbidden in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "setTimeout('", 'setTimeout("', "localStorage", "sessionStorage", "document.cookie", "location.href"):
            self.assertNotIn(forbidden, script, forbidden)

    def test_the_registry_holds_one_section_and_a_later_one_is_one_more_entry(self):
        script = read("web", "admin_page.js")
        self.assertIn("var SECTIONS = [{ id: 'maps', title: 'Maps players sent', render: renderMaps }];", script)
        self.assertIn("section.render(host, signed);", script)                      # (the shell knows no section by name: it calls the one that is chosen)
        shell = script[script.index("function showShell()"):script.index("// ---- the section \"Maps players sent\"")]
        self.assertNotIn("Maps players sent", shell)
        self.assertNotIn("Waiting", shell)

    def test_the_text_of_the_pictures_is_in_the_script(self):
        script = read("web", "admin_page.js")
        for words in ("Owner logon", "Only the owner can open this page", "Sign in with Discord", "This page is for the owner only", "Maps players sent", "Rename and approve", "Use next name", "Save name and approve",
                      "Name for this map", "Take this map down?", "Players will no longer find it. It can be put back later.", "Put back", "No approved maps yet", "Go to maps waiting",
                      "A name that is already taken is never replaced.", "Declining asks for a short reason that the maker can read. Nothing is deleted.",
                      "Taking a map down does not delete it. Its name stays protected"):
            self.assertIn(words, script)


if __name__ == "__main__":
    unittest.main()
