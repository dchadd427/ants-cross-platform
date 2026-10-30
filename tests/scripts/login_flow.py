"""One browser going through the hub's Discord login, for the tests (tests/scripts/test_hub_login.py, test_nginx_hub_login.py): a cookie jar, the three steps (the hub's /hub/login, the fake
Discord's consent, the hub's /hub/callback) and the calls that a page of the site makes (me, logout, forget). Shares no code with hub/.

`port` is where the site is served (the hub itself, or a real nginx in front of it); `address` is sent as X-Client-Address when the hub is asked directly (nginx sets its own).
"""
import re
import urllib.parse

import hub_client as hc


class Visitor:
    def __init__(self, port, fake, address=None, origin=hc.SITE):
        self.port, self.fake, self.address, self.origin = port, fake, address, origin
        self.jar = hc.Jar()
        self.state = self.code = None

    def call(self, method, path, headers=None, cookies=True, site="same-origin", origin="default", port=None):
        """A request of this browser: its cookies, and the headers that a page of the site's own sends (`site` is Sec-Fetch-Site, None leaves it out; `origin` the Origin, None leaves it out)."""
        pairs = []
        if self.address:
            pairs.append(("X-Client-Address", self.address))
        if cookies and self.jar.header():
            pairs.append(("Cookie", self.jar.header()))
        if site is not None:
            pairs.append(("Sec-Fetch-Site", site))
        if (origin == "default" and method == "POST") or (origin not in ("default", None)):
            pairs.append(("Origin", self.origin if origin == "default" else origin))
        pairs += list(headers or [])
        r = hc.request(port or self.port, method, path, pairs)
        self.jar.update(r)
        return r

    def start(self, next=None, **kw):
        """GET /hub/login (from the front page's button by default). Returns the answer; the state is the one in Discord's address."""
        r = self.call("GET", "/hub/login" + ("?next=" + next if next is not None else ""), **kw)
        if r.status == 302:
            self.state = dict(urllib.parse.parse_qsl(urllib.parse.urlsplit(r.get("location")).query)).get("state")
        return r

    def at_discord(self, location):
        """What Discord does with the address that the hub sent the browser to: the way back (address, code), after a consent page if the fake shows one."""
        parts = urllib.parse.urlsplit(location)
        r = hc.request(self.fake.port, "GET", parts.path + "?" + parts.query, host="127.0.0.1")
        if r.status == 200:
            back = re.search(r'href="([^"]+)"', r.text()).group(1).replace("&amp;", "&")
        else:
            assert r.status == 302, (r.status, r.text())
            back = r.get("location")
        self.code = dict(urllib.parse.parse_qsl(urllib.parse.urlsplit(back).query)).get("code")
        return back

    def callback(self, back, **kw):
        """The browser arrives at the hub's callback from Discord (a navigation from another site: Sec-Fetch-Site cross-site, no Origin)."""
        parts = urllib.parse.urlsplit(back)
        kw.setdefault("site", "cross-site")
        return self.call("GET", parts.path + "?" + parts.query, **kw)

    def login(self, next=None):
        """The whole round trip. Returns the answer of the callback."""
        r = self.start(next)
        assert r.status == 302, (r.status, r.text())
        return self.callback(self.at_discord(r.get("location")))

    def session(self):
        return self.jar.cookies.get("__Host-ants_hub")
