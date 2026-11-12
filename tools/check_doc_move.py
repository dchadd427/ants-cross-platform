#!/usr/bin/env python3
"""Proof that a split of a document is a pure move: no text is lost, changed, moved to another section, trimmed or added. Pure python 3 standard library.

usage: check_doc_move.py BASE [--root DIR] [--doc docs/X.md]

BASE is the commit (or branch) before the split; the checkout (--root, default this tool's repository) holds the split. The document that was split is the one .md file that lost the
most lines since BASE (or --doc); its parts are the .md files that did not exist at BASE. It checks:
  (a) the old document is cut into sections at its headings (outside code). Each section's heading and body, in order, blank lines and all, must be in the new set (the old document,
      now the index, and the parts) as one unbroken run, under its own heading, in one file. So a changed word, a swapped or moved line, a section left in two places, a line moved
      into the next section, a blank line removed or added, all fail. The sections of a file must stand in their old order, and a section under a `##` or deeper heading must have
      the same parent heading as before (a `###` moved to another `##`, or a part reversed, fails); the text before the first heading stays before every heading. A link target is
      not read as written but as what it means: the page of the split and the section it names (its heading and which section of the old document it is, so `#limits` and `#limits-1`
      differ even when their text is alike); an outside address, or a target that is no tracked file, as written. A line of the old document that the new set holds again fails.
      Every other new line must be one of the few a split needs, and anything else is an error:
        - a title: one `# ` heading line at the top of a part (before its first moved heading below the title);
        - a navigation line: at most 200 characters, holding a link to a tracked .md file, at the top of a part or of the index, or in the index after a moved body (a "moved to" line);
        - in the index only: a table of where the sections went (a header row, a rule row, then rows that each hold a link to a tracked .md file), with at most one `## ` heading
          directly above it.
      No new line may be a fence mark, hold a comment mark (`<!--`, `-->`) or a `<details` or `<summary` tag, or stand between a heading and its body, or after a moved body in a part.
      The new lines are printed with their line numbers, for a last look;
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
NAVIGATION_LENGTH = 200
HEADING_LEVEL = re.compile(r"^ {0,3}(#{1,6})")
MARKS = re.compile(r"<!--|-->|</?details|</?summary", re.I)
TABLE_RULE = re.compile(r"^\s*\|[\s:|-]*-[\s:|-]*$")
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
    """One state of the repository (BASE, or the checkout): its files, the pages of the split as one page, and what a link in a line means. `ident` ({page: {anchor: (heading text,
    which section)}}) says which section of the old document a heading of the split is; without it a section of the split is told by its first line."""

    def __init__(self, read, files, group, ident=None):
        self.read, self.fileset, self.group, self.ident = read, set(files), set(group), ident or {}
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

    def anchors(self, rel):
        """[(anchor, 0-based line of the heading, heading text)] of a .md file, in order (a repeated heading gets -1, -2 ...)."""
        key = ("anchors", rel)
        if key not in self.cache:
            lines, _, heads = self.raw(rel)
            seen, out = {}, []
            for h in heads:
                text = ch.ATX_HEADING.match(lines[h]).group(1)
                slug = ch.slug_of(text)
                count = seen.get(slug, 0)
                seen[slug] = count + 1
                out.append((slug if count == 0 else "%s-%d" % (slug, count), h, text))
            self.cache[key] = out
        return self.cache[key]

    def sections(self, rel):
        """{anchor: (heading text, fingerprint of its section)} of a .md file: the section is the body down to the next heading, its link targets blanked. A page of the split is told
        by the first line of the section until `ident` says better (an index may add a Parts table below a section that stays); any other page by the whole section."""
        key = ("sections", rel)
        if key not in self.cache:
            lines, _, heads = self.raw(rel)
            limit = FINGERPRINT_LINES if rel in self.group else None
            out = {}
            for k, (anchor, h, text) in enumerate(self.anchors(rel)):
                end = heads[k + 1] if k + 1 < len(heads) else len(lines)
                body = [TARGET.sub("]()", l) for l in trimmed(lines[h + 1:end]) if l.strip()][:limit]
                out[anchor] = (text, hashlib.sha1("\n".join(body).encode("utf-8")).hexdigest()[:12])
            self.cache[key] = out
        return self.cache[key]

    def meaning(self, rel, written):
        """What a link target means: an outside address or a target that is no tracked file as written, else the page, and the section when it names one (heading text and which section)."""
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
                known = self.ident[path] if path in self.ident else self.sections(path)
                what = "section %r" % (known.get(anchor) or ("dead anchor", anchor),)
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


def level_of(line):
    return len(HEADING_LEVEL.match(line).group(1))


def sections_of(lines, heads):
    """[(heading line or None, body lines, first line number, 0-based heading line or None)] of an old document: the text before its first heading, then each heading with its body."""
    cuts = list(heads) + [len(lines)]
    before = trimmed(lines[:cuts[0]])
    out = [(None, before, 1, None)] if before else []
    for k, h in enumerate(heads):
        out.append((lines[h], trimmed(lines[h + 1:cuts[k + 1]]), h + 1, h))
    return out


class Placed:
    """Where each section of the old document landed in the new set: {section number: (file, 0-based heading line or None, first body line, end of the body)}."""

    def __init__(self, old, new, doc, parts):
        self.files = [doc] + parts
        self.old_lines, self.old_heads = old.parsed(doc)[1], old.parsed(doc)[2]
        self.sections = sections_of(self.old_lines, self.old_heads)
        self.new_lines = {f: new.parsed(f)[1] for f in self.files}
        self.new_heads = {f: new.parsed(f)[2] for f in self.files}
        self.used = {f: [False] * len(self.new_lines[f]) for f in self.files}
        self.where = {}
        self.problems = []
        firsts = {f: collections.defaultdict(list) for f in self.files}
        for f in self.files:
            for i, l in enumerate(self.new_lines[f]):
                firsts[f][l].append(i)
        self.firsts = firsts
        for number, (heading, body, line_number, _) in enumerate(self.sections):
            got = self.place(heading, body)
            if got:
                self.where[number] = got
            else:
                self.problems.append("(a) %s: the section %s (line %d, %d line(s)) is not in the new set as one unbroken run under its own heading: a line was changed, swapped, moved to another "
                                     "section or kept in two places, or a blank line was added or removed%s" % (doc, ('"%s"' % heading.strip()) if heading else "before the first heading",
                                                                                                                   line_number, len(body), self.hint(heading, body)))

    def place(self, heading, body):
        for f in self.files:
            lines, heads, used = self.new_lines[f], self.new_heads[f], self.used[f]
            if not body:
                for h in heads:
                    if lines[h] == heading and not used[h]:
                        used[h] = True
                        return (f, h, h + 1, h + 1)
                continue
            for i in self.firsts[f].get(body[0], []):
                if lines[i:i + len(body)] != body or any(used[i:i + len(body)]):
                    continue
                k = bisect.bisect_left(heads, i) - 1
                h = None
                if heading is None:
                    if k >= 0:                                 # (the text before the first heading stays before every heading)
                        continue
                else:
                    if k < 0 or lines[heads[k]] != heading or used[heads[k]]:
                        continue
                    h = heads[k]
                    used[h] = True
                for j in range(i, i + len(body)):
                    used[j] = True
                return (f, h, i, i + len(body))
        return None

    def hint(self, heading, body):
        """' (the first difference: old line / new line)' for the section whose heading is found, else ''."""
        for f in self.files:
            for h in self.new_heads[f]:
                if heading is not None and self.new_lines[f][h] == heading:
                    after = trimmed(self.new_lines[f][h + 1:h + 1 + len(body) + 40])
                    for k, l in enumerate(body):
                        if k >= len(after) or after[k] != l:
                            return " (the first difference, old / new: %r / %r)" % (l[:120], after[k][:120] if k < len(after) else "(end)")
        return ""


def new_ident(new, placed):
    """{file: {anchor: (heading text, which section of the old document)}} of the split, from where the sections landed."""
    which = {(w[0], w[1]): number for number, w in placed.where.items() if w[1] is not None}
    return {f: {anchor: (text, which.get((f, h), "new line %d of %s" % (h + 1, f))) for anchor, h, text in new.anchors(f)} for f in placed.files}


def old_ident(old, doc, placed):
    which = {s[3]: number for number, s in enumerate(placed.sections) if s[3] is not None}
    return {doc: {anchor: (text, which[h]) for anchor, h, text in old.anchors(doc)}}


def check_structure(doc, placed, new):
    """The sections of a file in their old order, and each under the parent heading it had."""
    problems = []
    names = lambda n: '"%s"' % placed.sections[n][0].strip() if placed.sections[n][0] else "the text before the first heading"
    for f in placed.files:
        stand = sorted((n, w[1] if w[1] is not None else w[2]) for n, w in placed.where.items() if w[0] == f)
        for (a, at), (b, bt) in zip(stand, stand[1:]):
            if bt < at:
                problems.append("(a) %s: the section %s stands before the section %s, which came before it in %s" % (f, names(b), names(a), doc))
                break
    for n, (heading, _, _, _) in enumerate(placed.sections):
        if heading is None or n not in placed.where:
            continue
        mine = level_of(heading)
        parent = next((t for t in range(n - 1, -1, -1) if placed.sections[t][0] and level_of(placed.sections[t][0]) < mine), None)
        if parent is None or level_of(placed.sections[parent][0]) == 1 or parent not in placed.where:
            continue                                           # (a section under the page title may go to any file)
        f, h = placed.where[n][0], placed.where[n][1]
        lines = new.raw(f)[0]
        above = next((x for x in reversed(placed.new_heads[f]) if x < h and level_of(lines[x]) < mine), None)
        if (f, above) != (placed.where[parent][0], placed.where[parent][1]):
            problems.append("(a) %s: the section %s is not under its parent %s as before (it is in %s, under %s)" % (
                doc, names(n), names(parent), f, ('"%s"' % lines[above].strip()) if above is not None else "no heading"))
    return problems


def is_navigation(new, f, line):
    """A short line with a link to a tracked .md file."""
    if len(line) > NAVIGATION_LENGTH:
        return False
    for target in TARGET.findall(ch.INLINE_CODE.sub("", line)):
        parts = target.split(None, 1)
        target = parts[0].strip("<>") if parts else ""
        name = target.partition("#")[0].split("?")[0]
        if not name:
            continue
        path = os.path.normpath(os.path.join(os.path.dirname(f), name)).replace(os.sep, "/")
        if path in new.fileset and path.endswith(".md"):
            return True
    return False


def check_added(doc, placed, new):
    """(problems, [(file, line number, line)] of the new lines that a split may add)."""
    problems, listed = [], []
    for f in placed.files:
        lines, _, heads = new.raw(f)
        used = placed.used[f]
        is_index = f == doc
        heading_lines = set(heads)
        used_heads = {w[1] for w in placed.where.values() if w[0] == f and w[1] is not None}
        top_ends = next((h for h in heads if h in used_heads and level_of(lines[h]) >= 2), len(lines))
        nonblank = [i for i, l in enumerate(lines) if l.strip()]
        runs, run = [], []
        for pos, i in enumerate(nonblank):
            if used[i]:
                if run:
                    runs.append(run)
                    run = []
            else:
                run.append((pos, i))
        if run:
            runs.append(run)
        titles = 0

        def bad(i, why):
            problems.append("(a) %s:%d: a line was added that a split does not need (%s): %s" % (f, i + 1, why, lines[i].strip()[:160]))
        for run in runs:
            first = run[0][1]
            before = nonblank[run[0][0] - 1] if run[0][0] else None
            in_top = run[-1][1] < top_ends
            marked = [i for _, i in run if ch.FENCE.match(lines[i]) or MARKS.search(lines[i])]
            if marked:
                for i in marked:
                    bad(i, "a fence, comment or details mark")
                continue
            if not in_top and not is_index:
                bad(first, "inside a part, below its title and navigation lines: between a heading and its body, after a moved body, or a new section")
                continue
            if before is not None and before in used_heads and not in_top:
                bad(first, "between a heading and its body")
                continue
            at = 0
            while at < len(run):
                i = run[at][1]
                line = lines[i]
                if i in heading_lines:
                    level = level_of(line)
                    ahead = lines[run[at + 1][1]].lstrip().startswith("|") if at + 1 < len(run) else False
                    if level == 1 and in_top and titles == 0:
                        titles += 1
                    elif level == 2 and is_index and ahead:
                        pass
                    else:
                        bad(i, "a heading that is not the title of a part or the name of the index table")
                    listed.append((f, i + 1, line))
                    at += 1
                    continue
                if line.lstrip().startswith("|"):
                    end = at
                    while end < len(run) and lines[run[end][1]].lstrip().startswith("|"):
                        end += 1
                    rows = [lines[run[k][1]] for k in range(at, end)]
                    if not is_index:
                        bad(i, "a table in a part")
                    elif len(rows) < 3 or not TABLE_RULE.match(rows[1]) or not all(is_navigation(new, f, r) for r in rows[2:]):
                        bad(i, "a table that is not a header, a rule and rows that each link to a page")
                    listed.extend((f, run[k][1] + 1, lines[run[k][1]]) for k in range(at, end))
                    at = end
                    continue
                if not is_navigation(new, f, line):
                    bad(i, "text that is not a title, a navigation line or the index table")
                listed.append((f, i + 1, line))
                at += 1
    return problems, listed


def check_lines(old, new, doc, parts, report, old_args, new_args):
    """(a): (problems, the two sides as pass two read them). `old_args`, `new_args`: what to make the two sides again with, once pass one has said which section is which."""
    placed = Placed(old, new, doc, parts)
    if not placed.problems:                                    # (pass two: the links of the split read with the section they name, not its first line)
        old, new = Side(*old_args, ident=old_ident(old, doc, placed)), Side(*new_args, ident=new_ident(new, placed))
        placed = Placed(old, new, doc, parts)
    problems = list(placed.problems)
    old_lines = placed.old_lines
    spoken = {l for l in old_lines if l.strip() and has_text(l)}
    extra = [(f, i, l) for f in placed.files for i, l in enumerate(placed.new_lines[f]) if not placed.used[f][i] and l.strip()]
    again = [(f, i, l) for f, i, l in extra if has_text(l) and l in spoken] if not problems else []      # (when a section did not fit, its lines are left over: that is the same finding)
    if again:
        problems.append("(a) %d line(s) of %s are in the new set more than once (a section kept in two places?), for example in %s: %s" % (len(again), doc, again[0][0], again[0][2][:160]))
    listed = []
    if not problems:
        problems.extend(check_structure(doc, placed, new))
        added, listed = check_added(doc, placed, new)
        problems.extend(added)
    if report is not None:
        report.append("(a) the new set adds %d line(s) (titles, navigation, the table of the index: look at them):" % len(listed))
        report.extend("      + %s:%d: %s" % (f, n, l.strip()[:200]) for f, n, l in listed)
    return problems, old, new


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
        old_args = (lambda p: text_at_base(root, args.base, p), sorted(old_files), [doc])
        new_args = (lambda p: text_now(root, p), now, [doc] + parts)
        old_side, new_side = Side(*old_args), Side(*new_args)
        deleted = ["(b) %s was deleted (the old path of a split document stays, as its index)" % p for p in sorted(old_files) if p.endswith(".md") and p not in set(now)]
        in_lines, old_side, new_side = check_lines(old_side, new_side, doc, parts, report, old_args, new_args)
        problems = deleted + in_lines + check_others(old_side, new_side, others) + check_links(root, args.base, old_files, now, doc, parts)
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
