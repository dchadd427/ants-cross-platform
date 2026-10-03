#!/usr/bin/env python3
"""Turns a changelog (Markdown) into a standalone page of the beta site: CHANGELOG.md (the short changelog) into /changelog.html and
docs/CHANGELOG_ARCHIVE.md (the detailed history up to v0.1.0) into /changelog_archive.html.

Usage: changelog_to_html.py IN.md OUT.html [REPO_BLOB_URL]
           [--version vX.Y.Z] [--build-id ID]
           [--other-page FILE.html --other-label TEXT]
           [--page-link MD_PATH=PAGE.html ...] [--link-base DIR]

  --version, --build-id   shown under the title of the page ("v0.1.0 - build abc1234"): which build of the site this is
  --other-page, --other-label
                          a link in the header to the page's counterpart (the short page links to the detailed history and the other way round)
  --page-link             a relative link of the Markdown that names this file (docs/CHANGELOG_ARCHIVE.md) is pointed at that page of the site
                          instead of the repository; repeatable
  --link-base             the folder of the Markdown file inside the repository (docs for docs/CHANGELOG_ARCHIVE.md): a relative link is resolved from it,
                          so ../README.md and BOTS.md both point at the right file of the repository

An HTML comment (<!-- ... -->, the template at the top of CHANGELOG.md) is dropped.

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

PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ants (1998) - Changelog</title>
<link rel="icon" type="image/png" href="/favicon.png">
<style>
:root {{ --bg:#121513; --panel:#1c231e; --border:#384d3e; --green:#48b870; --gold:#e6b830; --text:#d0e0d4; --dim:#849688; }}
* {{ box-sizing: border-box; }}
body {{ margin:0; background:var(--bg); color:var(--text); font:15px/1.55 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }}
header {{ max-width:900px; margin:0 auto; padding:14px 16px 0; display:flex; align-items:baseline; justify-content:space-between; gap:12px; flex-wrap:wrap; }}
header h1 {{ margin:0; font-size:1.4rem; color:var(--gold); }}
header .buildline {{ color:var(--dim); font-size:.8rem; margin-top:2px; }}
header .links {{ display:flex; gap:16px; flex-wrap:wrap; }}
header a {{ color:var(--green); text-decoration:none; }}
header a:hover, .intro a:hover, section a:hover {{ text-decoration:underline; }}
main {{ max-width:900px; margin:0 auto; padding:8px 16px 40px; }}
.intro {{ color:var(--dim); }}
.intro a, section a {{ color:var(--green); }}
nav.versions {{ margin:14px 0 18px; padding:10px 12px; background:var(--panel); border:1px solid var(--border); border-radius:6px; font-size:.85rem; line-height:1.9; }}
nav.versions a {{ display:inline-block; margin:0 8px 0 0; color:var(--green); text-decoration:none; }}
nav.versions a:hover {{ text-decoration:underline; }}
section {{ margin:0 0 14px; padding:10px 14px 4px; background:var(--panel); border:1px solid var(--border); border-radius:6px; }}
section:target {{ border-color:var(--gold); }}
section h2 {{ margin:2px 0 8px; font-size:1.05rem; color:var(--gold); }}
section h2 .ver {{ color:var(--green); margin-right:6px; }}
section ul {{ margin:0 0 8px; padding-left:20px; }}
section li {{ margin:0 0 6px; }}
section p {{ margin:0 0 8px; }}
code {{ font:0.88em ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; background:#0f1411; border:1px solid #2a3a2f; border-radius:3px; padding:0 4px; }}
footer {{ max-width:900px; margin:0 auto; padding:0 16px 24px; color:var(--dim); font-size:.8rem; }}
</style>
</head>
<body>
<header><div><h1>{title}</h1>{buildline}</div><div class="links">{other}<a href="/">&larr; Back to the game</a></div></header>
<main>
{intro}
<nav class="versions" aria-label="Versions">{nav}</nav>
{sections}
</main>
<footer>Generated from {source} when the site image is built, so this page always matches the running build.</footer>
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
                head_html = '<span class="ver">%s</span>%s' % (html.escape(m.group(1)), (" &middot; " + inline(rest, repo_blob, page_links, link_base)) if rest else "")
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
            if current and current[2] and current[2][-1].startswith("<ul>"):
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

    body = "\n".join('<section id="%s"><h2>%s</h2>\n%s\n</section>' % (sid, head, "\n".join(blocks)) for sid, head, blocks in sections)
    intro_html = '<div class="intro">%s</div>' % "\n".join(intro)
    buildline = ""
    if version or build_id:
        text = " - ".join(part for part in (version, ("build " + build_id) if build_id else "") if part)
        buildline = '<div class="buildline">%s</div>' % html.escape(text)
    other = ""
    if other_page:
        other = '<a href="%s">%s</a>' % (html.escape(other_page, quote=True), html.escape(other_label or other_page))
    return PAGE.format(title=html.escape(title), intro=intro_html, nav=" ".join(nav), sections=body, buildline=buildline, other=other, source=html.escape(source))


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
