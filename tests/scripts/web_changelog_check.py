#!/usr/bin/env python3
"""The changelog page's pages in a REAL browser (opt-in; see tests/scripts/test_web_changelog.sh).

Needs a running web page (the web image of this tree, or tools/web_without_docker.py, or any folder that holds the generated changelog.html with /front/ beside it), a Chromium-based browser and
Python 3. The check opens /changelog.html in a throwaway headless browser, as a desktop (1280 wide) and as a phone (390 wide, touch, mobile metrics), and reads what the page does:

  * the first page shows ten releases (five on the phone) and no others, the tabs name the pages by their versions, and the list of versions is folded;
  * the address of a release (with dots, as the owner writes it, or the id with dashes) opens the page that holds it and scrolls the release into view, when the page is loaded fresh;
  * a click on a tab, and on Older at the bottom, shows the next page, the address follows, the focus stays in the bar, and the browser's Back and Forward buttons move between the pages;
  * a phone: no page of the list is wider than the screen (no horizontal scroll), whatever page is shown, and a tab is at least 44 px tall;
  * "Jump to a version" opens the old list, and a link in it opens the page of its release;
  * without JavaScript (the browser told not to run scripts) the page is the whole list: every release shows, no tabs, the list of versions open, no horizontal scroll on the phone.
What a headless browser cannot show: a screen reader's reading of the tabs and of the "Page 2 of 5" status, and the look in Safari and Firefox (this runs Chromium).

Exit status: 0 every check passed; 1 a check failed, or the browser or the page broke down; 3 the environment is not there (no browser, nothing at the page's address); 2 a bad command line.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from web_aspect_check import Browser, NotReachable, Tab                      # noqa: E402
from web_hidden_check import find_browser                                    # noqa: E402

PROFILES = {
    "desktop": {"label": "a desktop, 1280 wide", "size": (1280, 900), "dpr": 1.0, "mobile": False, "per_page": 10},
    "tablet": {"label": "a window 800 wide (no phone rules, narrow tabs panel)", "size": (800, 900), "dpr": 1.0, "mobile": False, "per_page": 10},
    "phone": {"label": "a phone, 390 wide (touch)", "size": (390, 844), "dpr": 2.0, "mobile": True, "per_page": 5},
}
PAGE_STATE = """(function () {
  var secs = Array.prototype.slice.call(document.querySelectorAll('main > section'));
  var shown = secs.filter(function (s) { return s.getBoundingClientRect().height > 0; }).map(function (s) { return s.id; });
  var tabs = Array.prototype.slice.call(document.querySelectorAll('.pager.tabs .tab'));
  var foot = document.querySelector('.pager.foot');
  var d = document.getElementById('versions');
  return JSON.stringify({
    all: secs.map(function (s) { return s.id; }), shown: shown,
    tabs: tabs.map(function (t) { return t.textContent; }), current: tabs.map(function (t, i) { return t.getAttribute('aria-current') === 'page' ? i : -1; }).filter(function (i) { return i >= 0; }),
    tabHeights: tabs.map(function (t) { return Math.round(t.getBoundingClientRect().height); }),
    status: foot ? foot.querySelector('.where').textContent : null, folded: d ? !d.open : null,
    overflow: document.documentElement.scrollWidth > document.documentElement.clientWidth, hash: location.hash,
    framed: (function () {
      var probe = document.createElement('i'); probe.style.color = 'var(--gold)'; document.body.appendChild(probe);
      var gold = getComputedStyle(probe).color; document.body.removeChild(probe);
      return secs.filter(function (s) { return getComputedStyle(s).borderTopColor === gold; }).map(function (s) { return s.id; });
    })(),
    active: document.activeElement ? document.activeElement.className : '', activeInBar: !!(document.activeElement && document.activeElement.closest && document.activeElement.closest('.pager'))
  });
})()"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--web", required=True, help="the site, e.g. http://127.0.0.1:19980/")
    ap.add_argument("--browser", default=os.environ.get("CHROME", ""), help="a Chromium-based browser (default: look for one)")
    ap.add_argument("--profile", default="all", help="desktop, tablet, phone or all (default)")
    args = ap.parse_args()
    if args.profile != "all" and args.profile not in PROFILES:
        print("unknown profile %r (desktop, tablet, phone or all)" % args.profile)
        return 2
    url = args.web.rstrip("/") + "/changelog.html"
    failures = []
    count = [0]

    def check(ok, what):
        count[0] += 1
        print("  %s: %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures.append(what)

    path = find_browser(args.browser)
    if not path:
        print("  SKIP: no Chromium-based browser found (give one with --browser or CHROME)")
        return 3

    def load(tab, target):
        """a fresh load of the address (a different page first, so that a hash-only change is not a same-page jump)"""
        tab.call("Page.navigate", {"url": "about:blank"})
        time.sleep(0.2)
        navigation = tab.call("Page.navigate", {"url": target})
        if navigation.get("errorText"):
            raise NotReachable("nothing answers at %s (%s)" % (target, navigation["errorText"]))
        deadline = time.time() + 30
        while time.time() < deadline:
            time.sleep(0.2)
            try:
                if tab.ev("document.readyState === 'complete' && !!document.querySelector('main > section')"):
                    time.sleep(0.4)
                    return
            except (RuntimeError, TimeoutError):
                pass
        raise TimeoutError("the page did not load at " + target)

    def state(tab):
        return json.loads(tab.ev(PAGE_STATE))

    def centre(tab, selector, index=0):
        return tab.ev("(function () { var e = document.querySelectorAll(%s)[%d]; e.scrollIntoView({block: 'center'}); var r = e.getBoundingClientRect(); return JSON.stringify([r.left + r.width / 2, r.top + r.height / 2]); })()" % (json.dumps(selector), index))

    def click(tab, selector, index=0):
        x, y = json.loads(centre(tab, selector, index))
        time.sleep(0.1)
        tab.click(x, y)
        time.sleep(0.5)

    def run_profile(name, profile, browser):
        print("[web changelog] %s: %s" % (name, profile["label"]))
        per = profile["per_page"]
        tab = Tab(browser)
        try:
            width, height = profile["size"]
            tab.emulate(width, height, profile["dpr"], mobile=profile["mobile"], touch=profile["mobile"])
            load(tab, url)
            st = state(tab)
            n = len(st["all"])
            pages = max(1, round(n / per + 1e-9))
            check(n >= 2 * per, "the page holds %d releases (more than two pages of %d)" % (n, per))
            check(st["shown"] == st["all"][:per] or (pages == 1), "the first page shows the first %d releases and no others (%d shown)" % (per, len(st["shown"])))
            check(len(st["tabs"]) == pages and st["current"] == [0], "%d tabs, the first is the current page (%s)" % (pages, st["tabs"][:2]))
            check(st["tabs"][0].startswith(st["all"][0].replace("-", ".")) and "to " in st["tabs"][0], "a tab is named by its versions: %r" % st["tabs"][0])
            check(st["folded"] is True and st["status"] == "Page 1 of %d" % pages, "the list of versions is folded and the bar says %r" % st["status"])
            check(not st["overflow"], "no horizontal scroll on the first page")
            if profile["mobile"]:
                check(min(st["tabHeights"]) >= 44, "every tab is at least 44 px tall (the smallest is %d)" % min(st["tabHeights"]))

            # a click on the second tab, on Older, and the browser's Back and Forward
            click(tab, ".pager.tabs .tab", 1)
            second = state(tab)
            check(second["shown"] == st["all"][per:2 * per] and second["current"] == [1], "a click on the second tab shows the second page (%d releases)" % len(second["shown"]))
            check(second["hash"] == "#" + st["all"][per], "the address is the id of the page's first release (%s)" % second["hash"])
            check(second["activeInBar"], "the focus is in the bar after the click (%s)" % second["active"])
            check(second["framed"] == [], "a click on a tab frames nothing: the first release of the new page is not framed (%s)" % second["framed"])
            click(tab, ".pager.foot .next")
            third = state(tab)
            check(third["shown"] == st["all"][2 * per:3 * per] and third["current"] == [2] and third["status"] == "Page 3 of %d" % pages, "Older at the bottom shows the third page (%s)" % third["status"])
            check(third["activeInBar"], "the focus is in the bar after Older (%s)" % third["active"])
            check(third["framed"] == [], "Older frames nothing either (%s)" % third["framed"])
            bar_in_view = tab.ev("(function () { var r = document.getElementById('pages-top').getBoundingClientRect(); return r.top >= -1 && r.top < innerHeight; })()")
            check(bar_in_view, "after Older at the bottom the tabs are at the top of the screen: the new page starts in view")
            tab.ev("history.back()")
            time.sleep(0.5)
            check(state(tab)["shown"] == st["all"][per:2 * per], "Back shows the second page again")
            tab.ev("history.back()")
            time.sleep(0.5)
            check(state(tab)["shown"] == st["all"][:per], "Back again shows the first page")
            tab.ev("history.forward()")
            time.sleep(0.5)
            check(state(tab)["shown"] == st["all"][per:2 * per], "Forward shows the second page")


            # the bars and the tabs in a grid: wrapped, inside their panels, one width for every tab (a short last row does not stretch)
            geometry = json.loads(tab.ev("""(function () {
              var top = document.getElementById('pages-top'), foot = document.querySelector('.pager.foot'), box = top.getBoundingClientRect(), tabs = Array.prototype.slice.call(top.children);
              var widths = tabs.map(function (t) { return Math.round(t.getBoundingClientRect().width); }), rows = {};
              tabs.forEach(function (t) { rows[t.offsetTop] = (rows[t.offsetTop] || 0) + 1; });                        // (offsetTop: the chosen tab is moved by a transform)
              var inside = function (e, p) { var r = e.getBoundingClientRect(), q = p.getBoundingClientRect(); return r.left >= q.left - 1 && r.right <= q.right + 1; };
              var fb = foot.getBoundingClientRect();
              return JSON.stringify({ widths: widths, rows: Object.keys(rows).map(Number).sort(function (a, b) { return a - b; }).map(function (k) { return rows[k]; }),
                tabsInside: tabs.every(function (t) { return inside(t, top); }), footInside: Array.prototype.every.call(foot.children, function (c) { return inside(c, foot); }),
                topScroll: top.scrollWidth <= top.clientWidth, footScroll: foot.scrollWidth <= foot.clientWidth });
            })()"""))
            check(max(geometry["widths"]) - min(geometry["widths"]) <= 1, "every tab has the same width, also in a short last row (widths %s, tabs per row %s)" % (sorted(set(geometry["widths"])), geometry["rows"]))
            check(geometry["tabsInside"] and geometry["topScroll"] and geometry["footInside"] and geometry["footScroll"], "the tabs and the Newer / Older bar stay inside their panels, with no scrollbar of their own")
            if profile["mobile"]:
                check(geometry["rows"][0] == 3 and len(geometry["rows"]) >= 2, "the tabs wrap in rows of three on a phone (%s)" % geometry["rows"])
            elif profile["size"][0] < 900:
                check(len(geometry["rows"]) >= 2, "the tabs wrap in a window 800 wide (%s)" % geometry["rows"])

            # no page of the list is wider than the screen
            wide = []
            for page in range(pages):
                tab.ev("location.hash = %s" % json.dumps("#" + st["all"][min(page * per, n - 1)]))
                time.sleep(0.3)
                if state(tab)["overflow"]:
                    wide.append(page + 1)
            check(not wide, "no page is wider than the screen (pages that are: %s)" % (wide or "none"))

            # a fresh load on the address of a release: dots, as written, and dashes, as the id is
            target = st["all"][per + 3]
            for form, text in ((target.replace("-", "."), "dots"), (target, "dashes")):
                load(tab, url + "#" + form)
                landed = state(tab)
                inside = tab.ev("(function () { var r = document.getElementById(%s).getBoundingClientRect(); return r.top >= -1 && r.top < innerHeight; })()" % json.dumps(target))
                check(landed["shown"] == st["all"][per:2 * per] and inside, "a fresh load on #%s (%s) shows page 2 and has the release in view" % (form, text))
            for form, text in ((target.replace("-", "."), "dotted"), (target, "dashed")):
                load(tab, url + "#" + form)
                check(state(tab)["framed"] == [target], "a fresh load on the %s address #%s frames that release in gold, and no other (%s)" % (text, form, state(tab)["framed"]))
            first_of_third = st["all"][2 * per]
            for form, text in ((first_of_third.replace("-", "."), "dotted"), (first_of_third, "dashed")):
                load(tab, url + "#" + form)
                check(state(tab)["framed"] == [first_of_third], "... and so does the %s address of the FIRST release of a page, #%s (%s)" % (text, form, state(tab)["framed"]))
            load(tab, url)
            check(state(tab)["framed"] == [], "a fresh load with no address frames nothing")
            load(tab, url + "#" + first_of_third)
            inside = tab.ev("(function () { var r = document.getElementById(%s).getBoundingClientRect(); return r.top >= -1 && r.top < innerHeight; })()" % json.dumps(first_of_third))
            check(state(tab)["current"] == [2] and inside, "an address on the first release of a page (#%s) shows page 3 and has the release in view" % first_of_third)
            load(tab, url + "#nothing-here")
            check(state(tab)["shown"] == st["all"][:per], "an address that names no release is the first page")

            # the list of versions
            load(tab, url)
            click(tab, "#versions summary")
            check(state(tab)["folded"] is False, "'Jump to a version' opens the list")
            old = st["all"][3 * per + 1] if n > 3 * per + 1 else st["all"][-1]
            click(tab, '#versions a[href="#%s"]' % old)
            jumped = state(tab)
            check(old in jumped["shown"] and jumped["hash"] == "#" + old, "a link of the list opens the page of its release (%s)" % old)
            check(jumped["framed"] == [old], "... and frames that release (%s)" % jumped["framed"])
            wrapped = json.loads(tab.ev("""(function () { var nav = document.querySelector('#versions nav'), tops = {};
              Array.prototype.forEach.call(nav.children, function (a) { tops[Math.round(a.getBoundingClientRect().top)] = true; });
              return JSON.stringify({ rows: Object.keys(tops).length, scroll: nav.scrollWidth <= nav.clientWidth }); })()"""))
            check(wrapped["rows"] >= 2 and wrapped["scroll"], "the list of versions wraps into rows and has no sideways scroll (%d rows)" % wrapped["rows"])

            # without JavaScript: the whole list
            tab.call("Emulation.setScriptExecutionDisabled", {"value": True})
            load_nojs = [False]
            try:
                tab.call("Page.navigate", {"url": "about:blank"})
                time.sleep(0.2)
                tab.call("Page.navigate", {"url": url})
                time.sleep(1.5)
                bare = json.loads(tab.ev("JSON.stringify({sections: document.querySelectorAll('main > section').length, shown: Array.prototype.filter.call(document.querySelectorAll('main > section'), function (s) { return s.getBoundingClientRect().height > 0; }).length, tabs: document.querySelectorAll('.pager').length, open: document.getElementById('versions').open, overflow: document.documentElement.scrollWidth > document.documentElement.clientWidth})"))
                load_nojs[0] = True
            finally:
                tab.call("Emulation.setScriptExecutionDisabled", {"value": False})
            check(load_nojs[0] and bare["sections"] == n and bare["shown"] == n and bare["tabs"] == 0 and bare["open"] is True and not bare["overflow"],
                  "without JavaScript every release shows (%d of %d), there is no bar, the list of versions is open, no horizontal scroll" % (bare["shown"], n))
        finally:
            tab.close()

    browser = None
    try:
        browser = Browser(path)
        for name in (sorted(PROFILES) if args.profile == "all" else [args.profile]):
            run_profile(name, PROFILES[name], browser)
    except NotReachable as e:
        if not count[0]:
            print("  SKIP: %s" % e)
            return 3
        print("  FAIL: %s" % e)
        return 1
    except (RuntimeError, TimeoutError, ConnectionError, OSError, ValueError, KeyError, IndexError) as e:
        print("  FAIL: the browser or the page broke down during the check: %s: %s" % (type(e).__name__, e))
        return 1
    finally:
        if browser is not None:
            browser.close()
    print("[web changelog] %d checks, %d failed" % (count[0], len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
