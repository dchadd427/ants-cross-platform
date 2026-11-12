#!/usr/bin/env python3
"""Proof that a split of a document is a pure move: no text is lost, changed, moved to another section or trimmed. Pure python 3 standard library.

usage: check_doc_move.py BASE [--root DIR] [--doc docs/X.md]

BASE is the commit (or branch) before the split; the checkout (--root, default this tool's repository) holds the split. The document that was split is the one .md file that lost the
most lines since BASE (or --doc); its parts are the .md files that did not exist at BASE. It checks:
  (a) the old document is cut into sections at its headings (outside code). Each section's heading and body, in order, blank lines and all, must be in the new set (the old document,
      now the index, and the parts) as one unbroken run, under its own heading, in one file. So a changed word, a swapped or moved line, a section left in two places, a line moved
      into the next section, a blank line removed or added, all fail. A link target is not read as written but as what it means: the page of the split and the section it names
      (the heading and a fingerprint of its first line, so `#limits` and `#limits-1` differ, and two swapped targets differ); an outside address, or a target that is no tracked file, as
      written. A line of the old document that the new set holds again (a section kept in two places) fails. The other new lines (a title, an intro, the Parts table) are printed, for
      review;
  (b) every other .md file changed since BASE only in its link targets, read the same way, line for line and blank line for blank line, and none was deleted;
  (c) every link of a .md file that resolved at BASE resolves now (to a heading with the same text, when it names one), counting the old document and its parts as one page, and every
      link to a heading in the checkout names a real heading (check_hygiene.py, the anchor check).
It does not look at tests: a test that reads a moved text gets a path change, which a person reviews (AGENTS.md, rule 10). Exit 0 when (a), (b) and (c) hold, 1 when not (one line each),
2 for a usage error.
"""
import argparse
import bisect
import collections
import hashlib
import os
import re
import subprocess
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_hygiene as ch  # noqa: E402

TARGET = re.compile(r"\]\(([^)]*)\)")
REFDEF_LINE = re.compile(r"^(\s{0,3}\[[^\]]+\]:\s*)(\S+)(.*)$")
HTML_ATTRIBUTE = re.compile(r'((?:href|src)=")([^"]*)(")')
FINGERPRINT_LINES = 1
OUTSIDE = re.compile(r"^(?:[a-zA-Z][a-zA-Z0-9+.-]*:|//)")


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


def plain_lines(text):
    return [l for l in text.splitlines() if l.strip()]


def has_text(line):
    """Does a line say anything (a letter or a digit)? `|---|---|` and `---` do not."""
    return any(c.isalnum() for c in line)


def trimmed(lines):
    """A block without the blank lines at its two ends."""
    a, b = 0, len(lines)
    while a < b and not lines[a].strip():
        a += 1
    while b > a and not lines[b - 1].strip():
        b -= 1
    return lines[a:b]


class Side:
    """One state of the repository (BASE, or the checkout): its files, the pages of the split as one page, and what a link in a line means."""

    def __init__(self, read, files, group):
        self.read, self.fileset, self.group = read, set(files), set(group)
        self.cache = {}

    def page(self, path):
        return "(split)" if path in self.group else path

    def raw(self, rel):
        """(lines, the numbers of the lines that a reader sees as text, the 0-based numbers of the heading lines): headings and links count outside code and comments only."""
        key = ("raw", rel)
        if key not in self.cache:
            text = self.read(rel)
            lines = text.lstrip("\ufeff").splitlines()
            prose = {n for n, _ in ch.prose_lines(text)}
            self.cache[key] = (lines, prose, [i for i, l in enumerate(lines) if i + 1 in prose and ch.ATX_HEADING.match(l)])
        return self.cache[key]

    def parsed(self, rel):
        """(lines, lines with every link target read as what it means, the 0-based numbers of the heading lines)."""
        if rel not in self.cache:
            lines, prose, heads = self.raw(rel)
            self.cache[rel] = (lines, [self.line(rel, l) if i + 1 in prose else l for i, l in enumerate(lines)], heads)
        return self.cache[rel]

    def sections(self, rel):
        """{anchor: (heading text, fingerprint of its section)} of a .md file: the section is the body down to the next heading, its link targets blanked."""
        key = ("sections", rel)
        if key not in self.cache:
            lines, _, heads = self.raw(rel)
            seen, out = {}, {}
            for k, h in enumerate(heads):
                end = heads[k + 1] if k + 1 < len(heads) else len(lines)
                body = [TARGET.sub("]()", l) for l in trimmed(lines[h + 1:end]) if l.strip()][:FINGERPRINT_LINES]      # (the first line: an index may add a Parts table below a section that stays)
                text = ch.ATX_HEADING.match(lines[h]).group(1)
                slug = ch.slug_of(text)
                count = seen.get(slug, 0)
                seen[slug] = count + 1
                out[slug if count == 0 else "%s-%d" % (slug, count)] = (text, hashlib.sha1("\n".join(body).encode("utf-8")).hexdigest()[:12])
            self.cache[key] = out
        return self.cache[key]

    def meaning(self, rel, written):
        """What a link target means: an outside address or a target that is no tracked file as written, else the page, and the section (heading text and fingerprint of its first line) when it names one."""
        parts = written.split(None, 1)
        target = parts[0].strip("<>") if parts else ""
        if not target or OUTSIDE.match(target):
            return written
        name, _, anchor = target.partition("#")
        name = name.split("?")[0]
        path = rel if not name else os.path.normpath(os.path.join(os.path.dirname(rel), name)).replace(os.sep, "/")
        if path not in self.fileset:
            return written
        what = "file"
        if anchor:
            anchor = urllib.parse.unquote(anchor.split("?")[0]).lower()
            if path.endswith(".md"):
                what = "section %r" % (self.sections(path).get(anchor) or ("dead anchor", anchor),)
            else:
                what = "anchor " + anchor
        return "<%s %s%s>" % (self.page(path), what, (" " + parts[1]) if len(parts) > 1 else "")

    def line(self, rel, line):
        m = REFDEF_LINE.match(line)
        if m:
            return m.group(1) + self.meaning(rel, m.group(2)) + m.group(3)
        out, at = [], 0
        for code in list(ch.INLINE_CODE.finditer(line)) + [None]:
            end = code.start() if code else len(line)
            piece = line[at:end]
            piece = TARGET.sub(lambda m: "](" + self.meaning(rel, m.group(1)) + ")", piece)
            piece = HTML_ATTRIBUTE.sub(lambda m: m.group(1) + self.meaning(rel, m.group(2)) + m.group(3), piece)
            out.append(piece)
            if code:
                out.append(code.group(0))
                at = code.end()
        return "".join(out)


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
        def lost(p):
            return sum((collections.Counter(plain_lines(text_at_base(root, base, p))) - collections.Counter(plain_lines(text_now(root, p)))).values())
        doc = max(changed, key=lost)
    return doc, parts, [p for p in changed if p != doc]


def sections_of(lines, heads):
    """[(heading line or None, body lines, first line number)] of an old document: the text before its first heading, then each heading with its body."""
    cuts = list(heads) + [len(lines)]
    out = [(None, trimmed(lines[:cuts[0]]), 1)] if cuts[0] else []
    for k, h in enumerate(heads):
        out.append((lines[h], trimmed(lines[h + 1:cuts[k + 1]]), h + 1))
    return out


def check_lines(old, new, doc, parts, report):
    """(a)"""
    _, old_lines, old_heads = old.parsed(doc)
    files = [doc] + parts
    new_lines = {f: new.parsed(f)[1] for f in files}
    new_heads = {f: new.parsed(f)[2] for f in files}
    used = {f: [False] * len(new_lines[f]) for f in files}
    firsts = {f: collections.defaultdict(list) for f in files}
    for f in files:
        for i, l in enumerate(new_lines[f]):
            firsts[f][l].append(i)
    problems = []

    def place(heading, body):
        for f in files:
            if not body:
                for h in new_heads[f]:
                    if new_lines[f][h] == heading and not used[f][h]:
                        used[f][h] = True
                        return True
                continue
            for i in firsts[f].get(body[0], []):
                if new_lines[f][i:i + len(body)] != body or any(used[f][i:i + len(body)]):
                    continue
                k = bisect.bisect_left(new_heads[f], i) - 1
                if heading is not None:
                    if k < 0 or new_lines[f][new_heads[f][k]] != heading or used[f][new_heads[f][k]]:
                        continue
                    used[f][new_heads[f][k]] = True
                for j in range(i, i + len(body)):
                    used[f][j] = True
                return True
        return False
    def hint(heading, body):
        """' (the first difference: old line / new line)' for the section whose heading is found, else ''."""
        for f in files:
            for h in new_heads[f]:
                if heading is not None and new_lines[f][h] == heading:
                    after = trimmed(new_lines[f][h + 1:h + 1 + len(body) + 40])
                    for k, l in enumerate(body):
                        if k >= len(after) or after[k] != l:
                            return " (the first difference, old / new: %r / %r)" % (l[:120], after[k][:120] if k < len(after) else "(end)")
        return ""
    for heading, body, number in sections_of(old_lines, old_heads):
        if not place(heading, body):
            problems.append("(a) %s: the section %s (line %d, %d line(s)) is not in the new set as one unbroken run under its own heading: a line was changed, swapped, moved to another "
                            "section or kept in two places, or a blank line was added or removed%s" % (doc, ('"%s"' % heading.strip()) if heading else "before the first heading", number, len(body), hint(heading, body)))
    spoken = {l for l in old_lines if l.strip() and has_text(l)}
    extra = [(f, l) for f in files for i, l in enumerate(new_lines[f]) if not used[f][i] and l.strip()]
    again = [(f, l) for f, l in extra if has_text(l) and l in spoken] if not problems else []      # (when a section did not fit, its lines are left over: that is the same finding)
    if again:
        problems.append("(a) %d line(s) of %s are in the new set more than once (a section kept in two places?), for example in %s: %s" % (len(again), doc, again[0][0], again[0][1][:160]))
    if report is not None:
        report.append("(a) the new set adds %d line(s) (titles, intros, the Parts table: review them):" % len([1 for _, l in extra if (_, l) not in again]))
        report.extend("      + " + l[:200] for f, l in extra if (f, l) not in again)
    return problems


def check_others(old, new, others):
    """(b)"""
    out = []
    for p in others:
        a, b = old.parsed(p)[1], new.parsed(p)[1]
        if a != b:
            n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
            out.append("(b) %s changed in more than the meaning of its links (first difference: line %d)" % (p, n + 1))
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
        old_side = Side(lambda p: text_at_base(root, args.base, p), sorted(old_files), [doc])
        new_side = Side(lambda p: text_now(root, p), now, [doc] + parts)
        deleted = ["(b) %s was deleted (the old path of a split document stays, as its index)" % p for p in sorted(old_files) if p.endswith(".md") and p not in set(now)]
        problems = (deleted + check_lines(old_side, new_side, doc, parts, report) + check_others(old_side, new_side, others)
                    + check_links(root, args.base, old_files, now, doc, parts))
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
