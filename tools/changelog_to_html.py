#!/usr/bin/env python3
"""Turns CHANGELOG.md into the standalone page served at /changelog.html on the beta site.

Usage: changelog_to_html.py CHANGELOG.md OUT.html [REPO_BLOB_URL]

No dependencies (the Emscripten build image only has the standard library). It understands exactly the Markdown subset the changelog
uses: one `#` title, paragraphs, `##` version headings, `- ` bullets with indented continuation lines, `code`, **bold** and [links](url).
Relative links (README.md, docs/...) are pointed at the repository, because those files are not part of the site.
"""
import html
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
<header><h1>{title}</h1><a href="/">&larr; Back to the game</a></header>
<main>
{intro}
<nav class="versions" aria-label="Versions">{nav}</nav>
{sections}
</main>
<footer>Generated from CHANGELOG.md when the site image is built, so this page always matches the running build.</footer>
</body>
</html>
"""


def inline(text, repo_blob):
    """Escape, then turn `code`, **bold** and [text](url) into HTML."""
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
            url = repo_blob + url.lstrip("./")
        return '<a href="%s">%s</a>' % (html.escape(url, quote=True), label)

    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], text)


def slug(heading):
    m = re.match(r"^(v\d+(?:\.\d+)*)", heading)
    base = m.group(1) if m else heading.split("(")[0]
    return re.sub(r"[^a-z0-9]+", "-", base.lower()).strip("-") or "section"


def convert(md, repo_blob):
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
                head_html = '<span class="ver">%s</span>%s' % (html.escape(m.group(1)), (" &middot; " + inline(rest, repo_blob)) if rest else "")
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
    return PAGE.format(title=html.escape(title), intro=intro_html, nav=" ".join(nav), sections=body)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    repo_blob = argv[3] if len(argv) > 3 else DEFAULT_REPO_BLOB
    if not repo_blob.endswith("/"):
        repo_blob += "/"
    with open(argv[1], encoding="utf-8") as f:
        md = f.read()
    out = convert(md, repo_blob)
    with open(argv[2], "w", encoding="utf-8") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
