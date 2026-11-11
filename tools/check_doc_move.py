#!/usr/bin/env python3
"""Proof that a split of a document is a pure move: no text is lost, changed or trimmed. Pure python 3 standard library.

usage: check_doc_move.py BASE [--root DIR] [--doc docs/X.md]

BASE is the commit (or branch) before the split; the checkout (--root, default this tool's repository) holds the split. The document that was split is the one .md file that shrank the
most since BASE (or --doc); its parts are the .md files that did not exist at BASE. It checks:
  (a) every non-blank line of the old document is in the new set (the old document, now the index, and the parts) as many times as it was, with every link target blanked out (`](x)` is
      `]()`, so a changed target is not a changed line); the lines that the new set adds (a title, an intro, the Parts table) are printed, for review;
  (b) every other .md file changed since BASE only in its link targets;
  (c) every link of a .md file that resolved at BASE resolves now (to a heading with the same text, when it names one), counting the old document and its parts as one page, and every
      link to a heading in the checkout names a real heading (check_hygiene.py, the anchor check).
It does not look at tests: a test that reads a moved text gets a path change, which a person reviews (docs/WORKFLOW.md). Exit 0 when (a), (b) and (c) hold, 1 when not (one line each),
2 for a usage error.
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_hygiene as ch  # noqa: E402

TARGET = re.compile(r"\]\([^)]*\)")


def normalize(line):
    """A line with every link target blanked: `[a](b.md#c)` is `[a]()`."""
    return TARGET.sub("]()", line)


def git(root, *args):
    done = subprocess.run(["git", "-C", root] + list(args), capture_output=True)
    if done.returncode != 0:
        raise SystemExit("check_doc_move: git %s failed: %s" % (" ".join(args), done.stderr.decode("utf-8", "replace").strip()))
    return done.stdout


def base_files(root, base):
    return {p for p in git(root, "ls-tree", "-r", "--name-only", "-z", base).decode("utf-8", "replace").split("\0") if p}


def now_files(root):
    out = git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard").decode("utf-8", "replace").split("\0")
    return sorted(p for p in out if p and os.path.isfile(os.path.join(root, p)))


def text_at_base(root, base, rel):
    return git(root, "show", "%s:%s" % (base, rel)).decode("utf-8", "replace")


def text_now(root, rel):
    return ch.read_bytes(root, rel).decode("utf-8", "replace")


def lines_of(text):
    return [normalize(l) for l in text.splitlines() if l.strip()]


def find_split(root, base, files, old_files, doc):
    """(the old document, its parts, the other changed .md files)."""
    md = [p for p in files if p.endswith(".md")]
    parts = [p for p in md if p not in old_files]
    changed = [p for p in md if p in old_files and text_now(root, p) != text_at_base(root, base, p)]
    if doc:
        if doc not in changed:
            raise SystemExit("check_doc_move: %s did not change since %s" % (doc, base))
    else:
        if not changed:
            raise SystemExit("check_doc_move: no .md file changed since %s" % base)
        doc = max(changed, key=lambda p: sum((collections.Counter(lines_of(text_at_base(root, base, p))) - collections.Counter(lines_of(text_now(root, p)))).values()))
    return doc, parts, [p for p in changed if p != doc]


def check_lines(root, base, doc, parts, report):
    """(a)"""
    old = collections.Counter(lines_of(text_at_base(root, base, doc)))
    new = collections.Counter()
    for p in [doc] + parts:
        new.update(lines_of(text_now(root, p)))
    lost = old - new
    added = new - old
    problems = ["(a) %d line(s) of %s are not in the new set as often as before, for example: %s" % (sum(lost.values()), doc, sorted(lost)[0][:160])] if lost else []
    if report is not None:
        report.append("(a) the new set adds %d line(s) (titles, intros, the Parts table: review them):" % sum(added.values()))
        report.extend("      + " + l[:200] for l in sorted(added))
    return problems


def check_others(root, base, others):
    """(b)"""
    out = []
    for p in others:
        if lines_of(text_at_base(root, base, p)) != lines_of(text_now(root, p)):
            out.append("(b) %s changed in more than its link targets" % p)
    return out


def heading_text(text):
    """{anchor: heading text} of a markdown text, with the numbers of a repeated one (as check_hygiene.headings_of)."""
    seen, out = {}, {}
    for _, line in ch.prose_lines(text):
        m = ch.ATX_HEADING.match(line)
        if m:
            slug = ch.slug_of(m.group(1))
            count = seen.get(slug, 0)
            seen[slug] = count + 1
            out[slug if count == 0 else "%s-%d" % (slug, count)] = m.group(1).strip()
    return out


def resolved_links(read, files, page_of):
    """Counter of (source page, target page, heading text or None) of the relative links to a tracked file of every .md file; a page of `group` is the one page "(split)"."""
    fileset = set(files)
    headings = {}
    found = collections.Counter()
    for rel in files:
        if not rel.endswith(".md"):
            continue
        here = os.path.dirname(rel)
        for _, line in ch.prose_lines(read(rel)):
            line = ch.INLINE_CODE.sub("", line)
            for target in ch.LINK.findall(line) + ch.REFDEF.findall(line):
                if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
                    continue
                name, _, anchor = target.partition("#")
                path = rel if not name else os.path.normpath(os.path.join(here, name.split("?")[0])).replace(os.sep, "/")
                if path not in fileset:
                    continue
                text = None
                anchor = urllib.parse.unquote(anchor.split("?")[0]).lower()
                if anchor and path.endswith(".md"):
                    if path not in headings:
                        headings[path] = heading_text(read(path))
                    text = headings[path].get(anchor)
                    if text is None:
                        continue                      # (a link that named no heading at BASE is not one that must still resolve: the anchor check judges the new ones)
                found[(page_of(rel), page_of(path), text)] += 1
    return found


def check_links(root, base, old_files, now, doc, parts):
    """(c)"""
    group = set([doc] + parts)
    def page_of(p):
        return "(split)" if p in group else p
    before = resolved_links(lambda p: text_at_base(root, base, p), sorted(old_files), page_of)
    after = resolved_links(lambda p: text_now(root, p), now, page_of)
    out = []
    for (src, dst, text), n in sorted(((k, v) for k, v in (before - after).items()), key=str):
        out.append("(c) %d link(s) from %s to %s%s no longer resolve" % (n, src, dst, (' (heading "%s")' % text) if text else ""))
    out.extend("(c) " + f for f in ch.check_anchors(root, now))
    return out


def main(argv):
    parser = argparse.ArgumentParser(description="Proof that a split of a document is a pure move (see the top of this file).")
    parser.add_argument("base", help="the commit before the split")
    parser.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."), help="the repository (default: this tool's)")
    parser.add_argument("--doc", help="the document that was split (default: the .md file that lost the most lines)")
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit as e:
        return 2 if e.code else 0
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print("check_doc_move: %s is not a folder" % root, file=sys.stderr)
        return 2
    try:
        old_files = base_files(root, args.base)
        now = now_files(root)
        doc, parts, others = find_split(root, args.base, now, old_files, args.doc)
        report = []
        problems = check_lines(root, args.base, doc, parts, report) + check_others(root, args.base, others) + check_links(root, args.base, old_files, now, doc, parts)
    except SystemExit as e:
        print(e, file=sys.stderr)
        return 2
    print("split: %s into %d part(s): %s" % (doc, len(parts), ", ".join(parts) or "(none)"), file=sys.stderr)
    print("\n".join(report), file=sys.stderr)
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
