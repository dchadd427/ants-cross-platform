#!/usr/bin/env python3
"""Turns the changelog (Markdown) into a standalone page of the beta site: CHANGELOG.md into /changelog.html.

Usage: changelog_to_html.py IN.md OUT.html [REPO_BLOB_URL]
           [--version vX.Y.Z] [--build-id ID]

  --version, --build-id   shown in the footer of the page ("v0.1.0 - build abc1234"): which build of the site this is

An HTML comment (<!-- ... -->, the template at the top of CHANGELOG.md) is dropped.

The page is in the Classic look of the front page: it links the site's /front/classic.css (the tokens, the buttons, the frame, the footer), preloads the font and shows the logo
from /front/, and adds only the rules of its own panels here. Nothing else is loaded; a long line scrolls inside its panel. The footer names the version and the build.

The page is the whole list of releases (a long page that works without JavaScript); a small inline script shows it in pages (PAGE_SIZE releases, PAGE_SIZE_PHONE on a phone), named by the versions
they hold, and the address of a release (#v0-12-30 or #v0.12.30) opens the page that has it. The script touches nothing it cannot put back: it hides the releases of the other pages and adds the page
bar at the top and the Newer / Older bar at the bottom, so tests/scripts/web_changelog_pages_check.js runs it on the generated page with a small fake of the DOM.

No dependencies (the Emscripten build image only has the standard library). It understands exactly the Markdown subset the changelog
uses: one `#` title, paragraphs, `##` version headings, `- ` bullets with indented continuation lines, `code`, **bold** and [links](url).
Relative links (README.md, docs/...) are pointed at the repository, because those files are not part of the site.
"""
import argparse
import html
import posixpath
import re
import sys

DEFAULT_REPO_BLOB = "https://github.com/dchadd427/ants-cross-platform/blob/main/"

# Releases to a page of the page (the script rounds the count of pages, so that the last page is never shorter than half a page), the same on a phone, and the width up to which the page is a phone's
PAGE_SIZE = 10
PAGE_SIZE_PHONE = 5
PHONE_WIDTH = 700

# The page's own rules (the look itself is /front/classic.css): the panels of the intro, the list of releases and each release; a release's heading, its text, links and code; the page bar
STYLE = """main { margin-top: 26px; }
.panel { margin: 0 0 22px; padding: 16px 22px; overflow-x: auto; overflow-wrap: anywhere; }
.panel > :last-child { margin-bottom: 0; }
.panel p, .panel li { font-size: 15px; line-height: 1.55; }
.panel p { margin: 0 0 10px; }
.panel ul { margin: 0 0 12px; padding-left: 22px; }
.panel li { margin: 0 0 8px; }
.panel li:last-child { margin-bottom: 0; }
.panel li::marker { color: var(--gold); }
.panel strong { font-weight: 500; color: #fff; -webkit-text-stroke: .5px currentColor; }
.panel p > strong:first-child { color: var(--gold); }
.panel a { color: var(--mint); text-underline-offset: 2px; }
.panel a:hover { color: #fff; }
.panel code { color: #d7f3e5; background: #0d1a16; border: 1px solid #1d4a3a; border-radius: 3px; padding: 0 4px; }
details.versions { padding: 0; }
.versions summary { cursor: pointer; padding: 10px 22px; font-size: 15px; color: var(--mint); }
.versions nav { display: flex; flex-wrap: wrap; gap: 4px 18px; padding: 2px 22px 12px; font-size: 14px; line-height: 1.8; }
.versions a.here { color: var(--gold); }
.pager { display: flex; flex-wrap: wrap; align-items: center; gap: 10px; padding: 12px 22px; }
.pager .btn { min-width: 44px; color: var(--cream); text-decoration: none; }
.pager .btn:hover { color: #fff; }
.pager .off { opacity: .4; cursor: default; pointer-events: none; box-shadow: none; }
.pager .where { margin: 0 auto; color: var(--muted); font-size: 14px; line-height: 1.3; text-align: center; }
.pager .tab { flex: 1 1 140px; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 1px; min-height: 52px; padding: 6px 8px; font-size: 16px; line-height: 1.15;
    color: var(--cream); text-decoration: none; background: var(--teal) var(--sheen); border: 2px solid var(--edge); border-radius: 8px; box-shadow: var(--btn-shadow-sm); }
.pager .tab span { font-size: 12.5px; color: #cfe3da; }
.pager .tab:hover { background-color: var(--teal-hi); }
.pager .tab.on { background: var(--pressed); color: var(--pressed-ink); transform: translate(1px, 2px); box-shadow: var(--btn-shadow-chosen); }
.pager .tab.on span { color: #c9b66a; }
section h2 { margin: 0 0 12px; font-size: 20px; font-weight: 500; line-height: 1.7; color: var(--gold); }
section h2 a { color: var(--gold); }
.rel { display: inline-block; margin-right: 12px; padding: 0 12px; line-height: 1.4; background: var(--teal) var(--sheen, none); border: 2px solid var(--edge); border-radius: 8px; box-shadow: var(--btn-shadow-sm); }
section:target { border-color: var(--gold); }
section.first:target { border-color: var(--frame-bevel); }
@media (max-width: @PHONE_WIDTH@px) {
    main { margin-top: 16px; }
    .panel { padding: 14px 14px; }
    .versions summary { padding: 10px 14px; }
    .versions nav { padding: 2px 14px 12px; max-height: 9.5em; overflow-y: auto; }
    .pager { padding: 12px 14px; gap: 8px; }
    .pager .prev, .pager .next { flex: 1 1 40%; min-height: 44px; }
    .pager .prev { order: 1; }
    .pager .next { order: 2; }
    .pager .where { order: 3; flex: 1 1 100%; margin: 0; }
    .pager .tab { flex: 1 1 28%; padding: 6px 4px; font-size: 15px; }
    .pager .tab span { font-size: 11.5px; }
    section h2 { font-size: 18px; }
}""".replace("@PHONE_WIDTH@", str(PHONE_WIDTH))

# The page's one script: shows the releases in pages. Without it (or when it fails) the page is the whole list. It reads the sections of <main> and the labels of the version list (the details
# element with the id "versions"), hides the sections of the other pages, and puts a bar of page tabs above the list of versions and a Newer / Older bar below the last release. The address is the
# state: #<release id> (a dot in it is a dash: #v0.12.30 is #v0-12-30) is the page that holds the release, no address is the first page, so the browser's own Back and Forward move between pages
# and a pager link is an ordinary link to the first release of its page. Run by node, with a small fake of the DOM, in tests/scripts/web_changelog_pages_check.js.
SCRIPT = r"""(function () {
  var SIZE = @SIZE@, PHONE_SIZE = @PHONE_SIZE@, PHONE_QUERY = '(max-width: @PHONE_WIDTH@px)';
  var main = document.getElementsByTagName('main')[0], versions = document.getElementById('versions');
  var secs = [], index = {}, labels = {}, kids = main.children, chips = versions ? versions.getElementsByTagName('a') : [], i;
  for (i = 0; i < kids.length; i++) if (kids[i].tagName === 'SECTION') { index[kids[i].id] = secs.length; secs.push(kids[i]); }
  for (i = 0; i < chips.length; i++) labels[chips[i].getAttribute('href').slice(1)] = chips[i].textContent;
  var mq = window.matchMedia(PHONE_QUERY), size, pages, cur = 0, top = null, foot = null, tabs = [], prev, next, where, folded = false, clicked = null;
  function make(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text) e.textContent = text;
    return e;
  }
  function pageOf(k) { return Math.min(Math.floor(k / size), pages - 1); }
  function firstOf(p) { return p * size; }
  function lastOf(p) { return p === pages - 1 ? secs.length - 1 : (p + 1) * size - 1; }
  function label(k) { return labels[secs[k].id] || secs[k].id; }
  function endLabel(k) { return /^v\d/.test(label(k)) ? label(k) : 'older'; }
  function link(e, p) { e.setAttribute('href', '#' + secs[firstOf(p)].id); e.removeAttribute('aria-disabled'); e.classList.remove('off'); }
  function drop(e) { if (e && e.parentNode) e.parentNode.removeChild(e); }
  function noted(e) {
    var a = e.target;
    while (a && a.tagName !== 'A') a = a.parentNode;
    clicked = a ? a.getAttribute('href') : null;
  }
  function wanted() {
    var raw = location.hash.slice(1), id;
    if (!raw) return -2;
    try { id = decodeURIComponent(raw).toLowerCase().replace(/\./g, '-'); } catch (e) { return -1; }
    return index.hasOwnProperty(id) ? index[id] : -1;
  }
  function make_bars() {
    var p, a;
    top = make('nav', 'pager tabs panel');
    top.id = 'pages-top';
    top.setAttribute('aria-label', 'Release pages');
    tabs = [];
    for (p = 0; p < pages; p++) {
      a = make('a', 'tab');
      a.setAttribute('href', '#' + secs[firstOf(p)].id);
      a.setAttribute('aria-label', 'Page ' + (p + 1) + ', ' + label(firstOf(p)) + ' to ' + endLabel(lastOf(p)));
      a.appendChild(make('b', '', label(firstOf(p))));
      a.appendChild(make('span', '', 'to ' + endLabel(lastOf(p))));
      top.appendChild(a);
      tabs.push(a);
    }
    top.addEventListener('click', noted);
    main.insertBefore(top, versions || secs[0]);
    foot = make('nav', 'pager foot panel');
    foot.setAttribute('aria-label', 'Newer and older releases');
    prev = make('a', 'btn sm prev', '\u00ab Newer');
    prev.setAttribute('rel', 'prev');
    where = make('span', 'where');
    where.setAttribute('role', 'status');
    next = make('a', 'btn sm next', 'Older \u00bb');
    next.setAttribute('rel', 'next');
    foot.appendChild(prev);
    foot.appendChild(where);
    foot.appendChild(next);
    foot.addEventListener('click', noted);
    main.appendChild(foot);
  }
  function show(p) {
    var k, a, id;
    cur = p;
    for (k = 0; k < secs.length; k++) {
      secs[k].hidden = pageOf(k) !== p;
      secs[k].classList.toggle('first', k === firstOf(pageOf(k)));
    }
    for (k = 0; k < tabs.length; k++) {
      tabs[k].classList.toggle('on', k === p);
      if (k === p) tabs[k].setAttribute('aria-current', 'page'); else tabs[k].removeAttribute('aria-current');
    }
    for (k = 0; k < chips.length; k++) {
      id = chips[k].getAttribute('href').slice(1);
      chips[k].classList.toggle('here', index.hasOwnProperty(id) && pageOf(index[id]) === p);
    }
    if (p > 0) link(prev, p - 1); else { prev.removeAttribute('href'); prev.setAttribute('aria-disabled', 'true'); prev.classList.add('off'); }
    if (p < pages - 1) link(next, p + 1); else { next.removeAttribute('href'); next.setAttribute('aria-disabled', 'true'); next.classList.add('off'); }
    where.textContent = 'Page ' + (p + 1) + ' of ' + pages;
  }
  function apply(keep) {
    var k;
    size = mq.matches ? PHONE_SIZE : SIZE;
    pages = Math.max(1, Math.round(secs.length / size));
    drop(top);
    drop(foot);
    top = foot = null;
    if (pages < 2) {
      for (k = 0; k < secs.length; k++) { secs[k].hidden = false; secs[k].classList.remove('first'); }
      return;
    }
    make_bars();
    show(pageOf(keep));
    if (versions && !folded) { folded = true; versions.removeAttribute('open'); }
  }
  function reveal(k) { if (k >= 0) (k === firstOf(cur) ? top : secs[k]).scrollIntoView(); }
  function route() {
    var k = wanted(), keep = clicked !== null && clicked === location.hash;
    clicked = null;
    if (!top || k === -1) return;
    show(k < 0 ? 0 : pageOf(k));
    reveal(k);
    if (keep) tabs[cur].focus({ preventScroll: true });
  }
  function resized() { apply(top ? firstOf(cur) : 0); }
  var start = wanted();
  apply(Math.max(0, start));
  if (top) reveal(start);
  window.addEventListener('hashchange', route);
  if (mq.addEventListener) mq.addEventListener('change', resized); else if (mq.addListener) mq.addListener(resized);
})();"""
SCRIPT = SCRIPT.replace("@SIZE@", str(PAGE_SIZE)).replace("@PHONE_SIZE@", str(PAGE_SIZE_PHONE)).replace("@PHONE_WIDTH@", str(PHONE_WIDTH))

PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ants (1998) - Changelog</title>
<link rel="icon" type="image/png" href="/favicon.png">
<link rel="preload" href="/front/LibreFranklin-Medium.ttf" as="font" type="font/ttf" crossorigin>
<link rel="stylesheet" href="/front/classic.css">
<style>
{style}
</style>
</head>
<body>
<div class="screen">
<div class="wrap narrow">
<header class="site-head">
<a class="logo" href="/" aria-label="ants! - the front page"><img src="/front/logo.png" alt="ants!" width="581" height="218"></a>
<div><h1 class="banner gold">{title}</h1>
<nav class="links" aria-label="Pages"><a class="btn sm" href="/">Play</a><a class="btn sm" href="{github}" target="_blank" rel="noopener noreferrer">GitHub</a></nav></div>
</header>
<main>
{intro}
<details class="versions panel" id="versions" open><summary>Jump to a version</summary><nav aria-label="Versions">{nav}</nav></details>
{sections}
</main>
<script>
{script}
</script>
</div>
<footer class="bar"><div class="bar-in">
{versionline}<span class="grow">Generated from {source} when the site image is built, so this page always matches the running build.</span>
<nav aria-label="Footer links"><a href="/">Play</a><a href="{github}" target="_blank" rel="noopener noreferrer">GitHub</a><a href="{github}/issues" target="_blank" rel="noopener noreferrer">Feedback</a></nav>
</div></footer>
</div>
</body>
</html>
"""


def repo_path(url):
    """A relative link of the changelog (a file in the repository's root) as a path from the root: "./docs/BOTS.md" is "docs/BOTS.md" (./ and a leading / are dropped, a #anchor is kept)."""
    path = url.split("#", 1)[0]
    anchor = url[len(path):]
    if path.startswith("/"):
        joined = path.lstrip("/")
    else:
        joined = posixpath.normpath(path) if path else ""
    joined = joined.lstrip("./")
    if path.endswith("/") and joined and not joined.endswith("/"):
        joined += "/"
    return joined + anchor


def inline(text, repo_blob):
    """Escape, then turn `code`, **bold** and [text](url) into HTML. A relative link goes to the file of the repository (repo_blob + its path)."""
    text = html.escape(text, quote=False)
    codes = []

    def keep_code(m):
        codes.append("<code>" + m.group(1) + "</code>")
        return "\x00%d\x00" % (len(codes) - 1)

    text = re.sub(r"`([^`]+)`", keep_code, text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)

    def link(m):
        label, url = m.group(1), m.group(2)
        if not re.match(r"^(https?://|#|mailto:)", url):
            url = repo_blob + repo_path(url)
        return '<a href="%s">%s</a>' % (html.escape(url, quote=True), label)

    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], text)


def repo_home(repo_blob):
    """The repository's own page from the address that relative links go to (.../blob/main/ becomes ...); any other address stays as it is."""
    return re.sub(r"/blob/[^/]+/?$", "", repo_blob.rstrip("/") + "/").rstrip("/")


def version_line(version, build_id, build_date=""):
    """The footer's pill: "v0.5.0 - build abc1234 (2026-01-02 09:30 PT)" (the same ids as the front page's footer), only the part that is known, nothing when neither is."""
    parts = []
    if version:
        parts.append('<span id="game-version">%s</span>' % html.escape(version))
    if build_id:
        date_part = (' <span id="game-build-date">(%s)</span>' % html.escape(build_date)) if build_date else ""
        parts.append('<span id="game-build">%sbuild <span id="game-build-id">%s</span>%s</span>' % (" - " if version else "", html.escape(build_id), date_part))
    return '<span class="ver" id="game-version-line">%s</span>\n' % "".join(parts) if parts else ""


def slug(heading):
    m = re.match(r"^(v\d+(?:\.\d+)*)", heading)
    base = m.group(1) if m else heading.split("(")[0]
    return re.sub(r"[^a-z0-9]+", "-", base.lower()).strip("-") or "section"


def convert(md, repo_blob, version="", build_id="", build_date="", source="CHANGELOG.md"):
    md = re.sub(r"<!--.*?-->", "", md, flags=re.S)
    title = "Changelog"
    intro, sections, nav = [], [], []
    current = None                       # [id, heading html, blocks]
    paragraph, item = [], None

    def flush_paragraph():
        nonlocal paragraph
        if paragraph:
            block = "<p>" + inline(" ".join(paragraph), repo_blob) + "</p>"
            (current[2] if current else intro).append(block)
            paragraph = []

    def flush_item():
        nonlocal item
        if item is not None:
            target = current[2] if current else intro
            if not target or not target[-1].startswith("<ul>"):
                target.append("<ul>")
            elif target[-1].endswith("</ul>"):
                target[-1] = target[-1][:-len("</ul>")]                  # a bullet after a blank line goes on with the list (it was left outside it, with a stray </ul> at the end)
            target[-1] += "<li>" + inline(" ".join(item), repo_blob) + "</li>"
            item = None

    def close_lists(blocks):
        return [b + "</ul>" if b.startswith("<ul>") and not b.endswith("</ul>") else b for b in blocks]

    for raw in md.splitlines():
        line = raw.rstrip()
        if line.startswith("# "):
            flush_paragraph()
            flush_item()
            title = line[2:].strip()
        elif line.startswith("## "):
            flush_paragraph()
            flush_item()
            if current:
                current[2] = close_lists(current[2])
                sections.append(current)
            heading = line[3:].strip()
            m = re.match(r"^(v\d+(?:\.\d+)*)\s*(.*)$", heading)
            if m:
                rest = re.sub(r"^[-\s]+", "", m.group(2))
                head_html = '<span class="rel">%s</span>%s' % (html.escape(m.group(1)), (" " + inline(rest, repo_blob)) if rest else "")
                short = m.group(1)
            else:
                head_html = inline(heading, repo_blob)
                short = heading.split("(")[0].strip() or heading
            current = [slug(heading), head_html, []]
            nav.append('<a href="#%s">%s</a>' % (current[0], html.escape(short)))
        elif line.startswith("- "):
            flush_paragraph()
            flush_item()
            item = [line[2:].strip()]
        elif line.startswith("  ") and item is not None:
            item.append(line.strip())
        elif not line.strip():
            flush_paragraph()
            flush_item()
            if current and current[2] and current[2][-1].startswith("<ul>") and not current[2][-1].endswith("</ul>"):
                current[2][-1] += "</ul>"
        else:
            flush_item()
            if current is not None and current[2] and current[2][-1].startswith("<ul>") and not current[2][-1].endswith("</ul>"):
                current[2][-1] += "</ul>"
            paragraph.append(line.strip())
    flush_paragraph()
    flush_item()
    if current:
        current[2] = close_lists(current[2])
        sections.append(current)
    intro = close_lists(intro)

    body = "\n".join('<section id="%s" class="panel"><h2>%s</h2>\n%s\n</section>' % (sid, head, "\n".join(blocks)) for sid, head, blocks in sections)
    intro_html = '<div class="intro panel">%s</div>' % "\n".join(intro)
    return PAGE.format(style=STYLE, script=SCRIPT, title=html.escape(title), intro=intro_html, nav=" ".join(nav), sections=body, versionline=version_line(version, build_id, build_date),
                       github=html.escape(repo_home(repo_blob), quote=True), source=html.escape(source))


def main(argv):
    parser = argparse.ArgumentParser(description="Turns a changelog (Markdown) into a standalone page of the site.")
    parser.add_argument("input", help="the Markdown file")
    parser.add_argument("output", help="the HTML file to write")
    parser.add_argument("repo_blob", nargs="?", default=DEFAULT_REPO_BLOB, help="the address that relative links are pointed at")
    parser.add_argument("--version", default="", help="the game's version, shown under the title")
    parser.add_argument("--build-id", default="", help="the build id, shown under the title")
    parser.add_argument("--build-date", default="", help="the build date and time in Pacific time, shown after the build id")
    args = parser.parse_args(argv[1:])
    repo_blob = args.repo_blob
    if not repo_blob.endswith("/"):
        repo_blob += "/"
    with open(args.input, encoding="utf-8") as f:
        md = f.read()
    source = args.input.replace("\\", "/").split("/")[-1]
    out = convert(md, repo_blob, version=args.version, build_id=args.build_id, build_date=args.build_date, source=source)
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
