#!/usr/bin/env python3
"""Turns a changelog (Markdown) into a standalone page of the beta site: CHANGELOG.md (the short changelog) into /changelog.html and
docs/CHANGELOG_ARCHIVE.md (the detailed history up to v0.1.0) into /changelog_archive.html.

Usage: changelog_to_html.py IN.md OUT.html [REPO_BLOB_URL]
           [--version vX.Y.Z] [--build-id ID]
           [--other-page FILE.html --other-label TEXT]
           [--page-link MD_PATH=PAGE.html ...] [--link-base DIR]

  --version, --build-id   shown in the footer of the page ("v0.1.0 - build abc1234"): which build of the site this is
  --other-page, --other-label
                          a button in the header for the page's counterpart (the short page links to the detailed history and the other way round)
  --page-link             a relative link of the Markdown that names this file (docs/CHANGELOG_ARCHIVE.md) is pointed at that page of the site
                          instead of the repository; repeatable
  --link-base             the folder of the Markdown file inside the repository (docs for docs/CHANGELOG_ARCHIVE.md): a relative link is resolved from it,
                          so ../README.md and BOTS.md both point at the right file of the repository

An HTML comment (<!-- ... -->, the template at the top of CHANGELOG.md) is dropped.

The page is in the Classic look of the front page: it links the site's /front/classic.css (the tokens, the buttons, the frame, the footer), preloads the font and shows the logo
from /front/, and adds only the rules of its own panels here. Nothing else is loaded; a long line scrolls inside its panel. The footer names the version and the build.

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

# The page's own rules (the look itself is /front/classic.css): the panels of the intro, the list of releases and each release; a release's heading, its text, links and code
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
nav.versions { display: flex; flex-wrap: wrap; gap: 4px 18px; padding: 12px 22px; font-size: 14px; line-height: 1.8; }
section h2 { margin: 0 0 12px; font-size: 20px; font-weight: 500; line-height: 1.7; color: var(--gold); }
section h2 a { color: var(--gold); }
.rel { display: inline-block; margin-right: 12px; padding: 0 12px; line-height: 1.4; background: var(--teal) var(--sheen, none); border: 2px solid var(--edge); border-radius: 8px; box-shadow: var(--btn-shadow-sm); }
section:target { border-color: var(--gold); }
@media (max-width: 700px) {
    main { margin-top: 16px; }
    .panel { padding: 14px 14px; }
    nav.versions { padding: 10px 14px; max-height: 9.5em; overflow-y: auto; }
    section h2 { font-size: 18px; }
}"""

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
<nav class="links" aria-label="Pages"><a class="btn sm" href="/">Play</a>{other}<a class="btn sm" href="{github}" target="_blank" rel="noopener noreferrer">GitHub</a></nav></div>
</header>
<main>
{intro}
<nav class="versions panel" aria-label="Versions">{nav}</nav>
{sections}
</main>
</div>
<footer class="bar"><div class="bar-in">
{versionline}<span class="grow">Generated from {source} when the site image is built, so this page always matches the running build.</span>
<nav aria-label="Footer links"><a href="/">Play</a><a href="{github}" target="_blank" rel="noopener noreferrer">GitHub</a><a href="{github}/issues" target="_blank" rel="noopener noreferrer">Feedback</a></nav>
</div></footer>
</div>
</body>
</html>
"""


def repo_path(url, link_base=""):
    """A relative link of the Markdown file as a path from the repository's root ("../README.md" in docs/ is "README.md"; ./ and a leading / are dropped)."""
    path = url.split("#", 1)[0]
    anchor = url[len(path):]
    if path.startswith("/"):
        joined = path.lstrip("/")
    else:
        joined = posixpath.normpath(posixpath.join(link_base, path)) if path else link_base
    joined = joined.lstrip("./")
    if path.endswith("/") and joined and not joined.endswith("/"):
        joined += "/"
    return joined + anchor


def inline(text, repo_blob, page_links=None, link_base=""):
    """Escape, then turn `code`, **bold** and [text](url) into HTML. A relative link whose path is a key of page_links goes to that page of the site."""
    page_links = page_links or {}
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
            target = repo_path(url, link_base)
            url = page_links[target] if target in page_links else repo_blob + target
        return '<a href="%s">%s</a>' % (html.escape(url, quote=True), label)

    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], text)


def repo_home(repo_blob):
    """The repository's own page from the address that relative links go to (.../blob/main/ becomes ...); any other address stays as it is."""
    return re.sub(r"/blob/[^/]+/?$", "", repo_blob.rstrip("/") + "/").rstrip("/")


def version_line(version, build_id):
    """The footer's pill: "v0.5.0 - build abc1234" (the same ids as the front page's footer), only the part that is known, nothing when neither is."""
    parts = []
    if version:
        parts.append('<span id="game-version">%s</span>' % html.escape(version))
    if build_id:
        parts.append('<span id="game-build">%sbuild <span id="game-build-id">%s</span></span>' % (" - " if version else "", html.escape(build_id)))
    return '<span class="ver" id="game-version-line">%s</span>\n' % "".join(parts) if parts else ""


def slug(heading):
    m = re.match(r"^(v\d+(?:\.\d+)*)", heading)
    base = m.group(1) if m else heading.split("(")[0]
    return re.sub(r"[^a-z0-9]+", "-", base.lower()).strip("-") or "section"


def convert(md, repo_blob, version="", build_id="", other_page="", other_label="", source="CHANGELOG.md", page_links=None, link_base=""):
    page_links = page_links or {}
    md = re.sub(r"<!--.*?-->", "", md, flags=re.S)
    title = "Changelog"
    intro, sections, nav = [], [], []
    current = None                       # [id, heading html, blocks]
    paragraph, item = [], None

    def flush_paragraph():
        nonlocal paragraph
        if paragraph:
            block = "<p>" + inline(" ".join(paragraph), repo_blob, page_links, link_base) + "</p>"
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
            target[-1] += "<li>" + inline(" ".join(item), repo_blob, page_links, link_base) + "</li>"
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
                head_html = '<span class="rel">%s</span>%s' % (html.escape(m.group(1)), (" " + inline(rest, repo_blob, page_links, link_base)) if rest else "")
                short = m.group(1)
            else:
                head_html = inline(heading, repo_blob, page_links, link_base)
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
    other = ""
    if other_page:
        other = '<a class="btn sm" href="%s">%s</a>' % (html.escape(other_page, quote=True), html.escape(other_label or other_page))
    return PAGE.format(style=STYLE, title=html.escape(title), intro=intro_html, nav=" ".join(nav), sections=body, versionline=version_line(version, build_id), other=other,
                       github=html.escape(repo_home(repo_blob), quote=True), source=html.escape(source))


def main(argv):
    parser = argparse.ArgumentParser(description="Turns a changelog (Markdown) into a standalone page of the site.")
    parser.add_argument("input", help="the Markdown file")
    parser.add_argument("output", help="the HTML file to write")
    parser.add_argument("repo_blob", nargs="?", default=DEFAULT_REPO_BLOB, help="the address that relative links are pointed at")
    parser.add_argument("--version", default="", help="the game's version, shown under the title")
    parser.add_argument("--build-id", default="", help="the build id, shown under the title")
    parser.add_argument("--other-page", default="", help="the counterpart page (a link in the header)")
    parser.add_argument("--other-label", default="", help="the text of that link")
    parser.add_argument("--link-base", default="", help="the folder of the Markdown file in the repository (docs)")
    parser.add_argument("--page-link", action="append", default=[], metavar="MD_PATH=PAGE", help="a relative link to this file goes to this page of the site")
    args = parser.parse_args(argv[1:])
    repo_blob = args.repo_blob
    if not repo_blob.endswith("/"):
        repo_blob += "/"
    page_links = {}
    for item in args.page_link:
        if "=" not in item:
            sys.stderr.write("--page-link needs MD_PATH=PAGE, got %r\n" % item)
            return 2
        key, value = item.split("=", 1)
        page_links[repo_path(key)] = value
    with open(args.input, encoding="utf-8") as f:
        md = f.read()
    source = args.input.replace("\\", "/").split("/")[-1]
    out = convert(md, repo_blob, version=args.version, build_id=args.build_id, other_page=args.other_page, other_label=args.other_label, source=source, page_links=page_links, link_base=args.link_base.strip('/'))
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
