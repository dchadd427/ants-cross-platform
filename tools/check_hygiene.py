#!/usr/bin/env python3
"""Repository hygiene check: fails (exit 1) when the repository gets worse. Pure python 3 standard library.

usage: check_hygiene.py [--root DIR] [--list]

Exit 0 when everything is in order, 1 when there are findings (one line each on stdout), 2 for a usage error. Warnings go to stderr and do not change the exit status. Under GITHUB_ACTIONS
they are ::warning lines (at most ANNOTATION_LIMIT, the files closest to their limit first, the last one counting the rest; the long lines are one ::notice): GitHub shows them on the pull
request when they reach the log of a step, which tests/scripts/run_python_tests.py does for a group that passes. --list prints every file over its target size.
Run by the tools tier (tests/scripts/test_hygiene.py, so by ./run_tests.sh --tools and by CI). The checks:
  1. sizes (the numbers are all in tools/size_limits.py): a document (docs/**/*.md, root *.md) is measured in bytes, a test or production file in lines. Over 125% of its base a file
     fails; from 90% it warns (documents from 75%, the target): split it by topic. The files of SPLIT_PENDING are over the cap and wait for their split: they warn, and a name that is
     gone or back under the cap fails. A document line over DOC_LINE_WARN_CHARACTERS characters is a warning too;
  2. every docs/**/*.md is reachable from README.md by relative markdown links (a link to a folder reaches its README.md);
  2a. every relative link to a heading (file.md#anchor or #anchor) in a tracked .md file names a real heading of that file (GitHub's slugs; a redirect <a id> does not count);
  3. no tracked text file and no tracked path names a private planning document or repository (PRIVATE_NAME_DIGESTS: kept as digests, spelled nowhere; the files of FORBIDDEN_ALLOWED are looked at for these too), and no tracked text file names a commit trailer or a session link (FORBIDDEN), except the files of FORBIDDEN_ALLOWED and the vendored files of VENDORED_CODE;
  4. (the line limits of code files are check 1);
  5. at most ASSERT_TRUE_DEFINITIONS definitions of the macro ASSERT_TRUE;
  6. every .cpp under src/, tests/ and tools/ is named in a CMakeLists.txt, except CMAKE_EXCEPTIONS;
  7. no tracked path begins with a build folder (build/ or build_*/ at the top: local build output that .gitignore keeps out; it gets in by `git add -f` or a tool that adds everything).
  8. American spelling (AGENTS.md "Spelling"): no tracked text file writes the British spelling of "color" (SPELLING_*: the kept names are allowed by file; the word is built from two halves below, so that this file does not name it);
A page that gets near its limit is split by topic as a pure move (tools/check_doc_move.py), never trimmed or joined to fit.
"""
import argparse
import hashlib
import html
import os
import re
import subprocess
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import size_limits  # noqa: E402  (every size number of the repository)

ASSERT_TRUE_DEFINITIONS = 2  # tests/common/test_assert.hpp and the throwing ASSERT_TRUE of tests/e2e/e2e_framework.hpp: a third one is a new copy to remove

FORBIDDEN = re.compile(r"claude\.ai/code|Co-Authored-By|Claude-Session|project thread|the other thread", re.I)
# The names of private planning documents and repositories are spelled nowhere in a public file, this one included: each is kept as the SHA-256 of its lower-case spelling, with its length (the idea
# of the digests in tests/scripts/test_web_mapmaker.py). The digests keep the names out of a casual search of the repository and nothing more: a name that is easy to guess can be found again from its
# digest, whatever its length. A name is made of letters, digits and "_-." only, so it lies inside one run of those characters (PRIVATE_RUN); a window of each length slides over the runs of a file and
# over the path of each tracked file, and a finding names the file and the line, never the name (a path is printed with the name hidden). An entry has PRIVATE_NAME_MIN_LENGTH characters or more, which
# private_name_matcher() enforces: a shorter one could be found again by trying every spelling.
PRIVATE_NAME_MIN_LENGTH = 10
PRIVATE_NAME_DIGESTS = (
    ("90ba2548bd1781b44437596aa2c4b006083995971e727a8615fb292c961a4936", 19),
    ("09ca0aaa77c748fe98f733209eaefdd67e555418f78a985289203d2d05d7e9fa", 10),
    ("80a4cf760a0fa2bff988c75b611448073519d9f9b9cbbde4410f8846097c7a7a", 12),
)
PRIVATE_RUN = re.compile(r"[a-z0-9_.\-]+")
VENDORED_CODE = {"third_party/sqlite/sqlite3.c", "third_party/sqlite/sqlite3.h"}       # (the files of other people's code that are copied unchanged; our own files beside them are checked)
FORBIDDEN_ALLOWED = {"tools/check_hygiene.py", "tests/scripts/test_hygiene.py"}

CMAKE_EXCEPTIONS = set()

TEXT_SUFFIXES_SKIPPED = {".lvl"}


def tracked_files(root):
    """Relative paths (with /) of the tracked files: git's list when root is a checkout, else every file below root."""
    if os.path.exists(os.path.join(root, ".git")):
        done = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True)
        if done.returncode == 0:
            return sorted(p for p in done.stdout.decode("utf-8", "replace").split("\0") if p and os.path.isfile(os.path.join(root, p)))
    found = []
    for base, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d != ".git"]
        for n in names:
            found.append(os.path.relpath(os.path.join(base, n), root).replace(os.sep, "/"))
    return sorted(found)


def read_bytes(root, rel):
    with open(os.path.join(root, rel), "rb") as f:
        return f.read()


def line_count(data):
    return data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)


def is_doc(rel):
    return rel.endswith(".md") and (rel.startswith("docs/") or "/" not in rel)


def levels(base):
    """The sizes of one base (a number of bytes or lines): the target, the split line and the safety cap, as whole numbers (percents are integers, so that no float rounds)."""
    return {"target": base * size_limits.TARGET_PERCENT // 100, "split": base * size_limits.SPLIT_PERCENT // 100, "cap": base * size_limits.CAP_PERCENT // 100}


def measure(root, rel):
    """(kind, unit, size, base) of a measured file, or None: a document in bytes, a test or production file in lines."""
    if is_doc(rel):
        return "document", "B", os.path.getsize(os.path.join(root, rel)), size_limits.DOC_BASE_BYTES
    kind, base = kind_of(rel)
    if kind:
        return kind, "lines", line_count(read_bytes(root, rel)), base
    return None


def percent(size, base):
    return int(size * 100 / base + 0.5)


def num(n):
    return format(n, ",")


def check_sizes(root, files, pending=None):
    """(errors, warnings): the sizes of every document and code file against tools/size_limits.py. A warning is (file, text, kind, percent), kind "split" (from the split line),
    "target" (a document over its target) or "pending" (over the cap and waiting in SPLIT_PENDING)."""
    pending = size_limits.SPLIT_PENDING if pending is None else pending
    errors, warnings = [], []
    fileset = set(files)
    for rel in files:
        got = measure(root, rel)
        if not got:
            continue
        kind, unit, size, base = got
        at = levels(base)
        what = "%s %s, %d%% of %s" % (num(size), unit, percent(size, base), num(base))
        pct = percent(size, base)
        now = "split it by topic now (target %s)" % num(at["target"])
        later = "over its target %s: split it by topic before it reaches %s" % (num(at["target"]), num(at["split"]))
        if rel in pending:
            if size <= at["cap"]:
                errors.append("%s: %s %s is no longer over the safety cap %s: remove it from SPLIT_PENDING (tools/size_limits.py)" % (rel, num(size), unit, num(at["cap"])))
            else:
                warnings.append((rel, "%s: %s (over the safety cap %s: it waits in SPLIT_PENDING)" % (what, now, num(at["cap"])), "pending", pct))
        elif size > at["cap"]:
            errors.append("%s: %s %s is above the safety cap %s (split it by topic%s)" % (rel, num(size), unit, num(at["cap"]), " as a pure move: tools/check_doc_move.py" if kind == "document" else ""))
        elif size >= at["split"]:
            warnings.append((rel, "%s: %s" % (what, now), "split", pct))
        elif kind == "document" and size > at["target"]:
            warnings.append((rel, "%s: %s" % (what, later), "target", pct))
    for rel in sorted(pending):
        if rel not in fileset:
            errors.append("%s: in SPLIT_PENDING but not a tracked file: remove it from the set (tools/size_limits.py)" % rel)
        elif not measure(root, rel):
            errors.append("%s: in SPLIT_PENDING but neither a document nor a code file that is measured: remove it from the set" % rel)
    return errors, warnings


def check_long_lines(root, files, limit=None):
    """[(file, number of lines, characters of the longest, its line number)] of the documents with lines over `limit` characters (agent readers may cut them)."""
    limit = size_limits.DOC_LINE_WARN_CHARACTERS if limit is None else limit
    out = []
    for rel in files:
        if not is_doc(rel):
            continue
        long = [(len(line), n) for n, line in enumerate(read_bytes(root, rel).decode("utf-8", "replace").splitlines(), 1) if len(line) > limit]
        if long:
            top = max(long)
            out.append((rel, len(long), top[0], top[1]))
    return out


def long_line_warning(found, limit=None):
    """The one notice for all the documents of check_long_lines (a notice for each would crowd out the size warnings), or None."""
    limit = size_limits.DOC_LINE_WARN_CHARACTERS if limit is None else limit
    if not found:
        return None
    rel, _, size, n = max(found, key=lambda f: f[2])
    return (rel, "%d line(s) in %d document(s) are over %s characters (the longest has %s, %s line %d): a reader may cut them; reflow in a change that moves no text (--list names them)"
            % (sum(f[1] for f in found), len(found), num(limit), num(size), rel, n), "long", 0)


LINK = re.compile(r"\]\(\s*<?([^)\s>]+)")
REFDEF = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*<?(\S+?)>?(?:\s|$)", re.M)


def links_of(root, rel, filesset):
    here = os.path.dirname(rel)
    found = set()
    for _, line in prose_lines(read_bytes(root, rel).decode("utf-8", "replace")):      # (links as the page shows them: not in code, not in a comment)
        line = INLINE_CODE.sub("", line)
        for target in LINK.findall(line) + REFDEF.findall(line):
            if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or target.startswith("#"):
                continue
            target = target.split("#")[0].split("?")[0]
            if not target:
                continue
            path = os.path.normpath(os.path.join(here, target)).replace(os.sep, "/")
            if path in filesset:
                found.add(path)
            elif path + "/README.md" in filesset:
                found.add(path + "/README.md")
    return found


def check_reachable(root, files, start="README.md"):
    filesset = set(files)
    if start not in filesset:
        return ["%s: missing (the start of the link check)" % start]
    seen = {start}
    todo = [start]
    while todo:
        cur = todo.pop()
        if not cur.endswith(".md"):
            continue
        for nxt in sorted(links_of(root, cur, filesset)):
            if nxt not in seen:
                seen.add(nxt)
                todo.append(nxt)
    out = []
    for rel in files:
        if rel.endswith(".md") and rel.startswith("docs/") and rel not in seen:
            out.append("%s: not reachable from README.md by relative links (link it from README.md or from a page README.md links)" % rel)
    return out


FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")
ATX_HEADING = re.compile(r"^ {0,3}#{1,6}[ \t]+(.*?)(?:[ \t]+#+)?[ \t]*$")
INLINE_CODE = re.compile(r"(`+)(.+?)\1")
HTML_COMMENT = re.compile(r"<!--.*?-->")


def prose_lines(text):
    """(number, line) of the lines of a markdown text that a reader sees as text: not in a fenced code block (a fence closes with its own mark, as long, and nothing after it) and not in
    an HTML comment (the part of a line outside the comment stays)."""
    fence, comment = None, False
    for n, line in enumerate(text.lstrip("\ufeff").splitlines(), 1):
        if comment:
            if "-->" not in line:
                continue
            comment, line = False, line.split("-->", 1)[1]
        m = FENCE.match(line)
        if fence:
            if m and m.group(1)[0] == fence[0] and len(m.group(1)) >= len(fence) and not m.group(2).strip():
                fence = None
            continue
        if m and not (m.group(1)[0] == "`" and "`" in m.group(2)):
            fence = m.group(1)
            continue
        line = HTML_COMMENT.sub("", line)
        if "<!--" in line:
            line, comment = line.split("<!--", 1)[0], True
        yield n, line


def slug_of(heading):
    """GitHub's anchor of a heading's text, before the -1, -2 of a repeated one: the rendered text, lower case, with punctuation removed and each space a hyphen."""
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", html.unescape(heading))      # a link or an image is its text; an entity is the character
    text = re.sub(r"\[([^\]]*)\]\[[^\]]*\]", r"\1", text)

    def markup(piece):                                                 # (outside code: an HTML tag goes, `*` goes, a pair of underscores that marks emphasis goes; a lone one stays)
        return re.sub(r"(?<!\w)(_{1,3})(?=\S)(.+?)(?<=\S)\1(?!\w)", r"\2", re.sub(r"<[^>]*>", "", piece).replace("*", ""))
    pieces, at = [], 0
    for m in INLINE_CODE.finditer(text):                               # (the text of a code span is kept as written)
        pieces += [markup(text[at:m.start()]), m.group(2).strip()]
        at = m.end()
    pieces.append(markup(text[at:]))
    return re.sub(r"[^\w\- ]", "", "".join(pieces).strip().lower()).replace(" ", "-")


def headings_of(text):
    """The anchors of the headings of a markdown text (a repeated one gets -1, -2 ...). A redirect <a id> is not one."""
    seen, out = {}, set()
    for _, line in prose_lines(text):
        m = ATX_HEADING.match(line)
        if m:
            slug = slug_of(m.group(1))
            count = seen.get(slug, 0)
            seen[slug] = count + 1
            out.add(slug if count == 0 else "%s-%d" % (slug, count))
    return out


def check_anchors(root, files):
    """Every relative link `file.md#anchor` or `#anchor` in a tracked .md file names a real heading of that file."""
    fileset = set(files)
    anchors = {}
    def anchors_of(rel):
        if rel not in anchors:
            anchors[rel] = headings_of(read_bytes(root, rel).decode("utf-8", "replace"))
        return anchors[rel]
    out = []
    for rel in files:
        if not rel.endswith(".md"):
            continue
        here = os.path.dirname(rel)
        for n, line in prose_lines(read_bytes(root, rel).decode("utf-8", "replace")):
            line = INLINE_CODE.sub("", line)
            for target in LINK.findall(line) + REFDEF.findall(line):
                if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or target.startswith("?"):
                    continue
                name, _, anchor = target.partition("#")
                anchor = urllib.parse.unquote(anchor.split("?")[0]).lower()
                path = rel if not name else os.path.normpath(os.path.join(here, name.split("?")[0])).replace(os.sep, "/")
                if path.endswith(".md") and path not in fileset:
                    out.append("%s:%d: the link to %s names a .md file that is not tracked (check the path and its case)" % (rel, n, path))
                    continue
                if not anchor or path not in fileset or not path.endswith(".md"):
                    continue
                if anchor not in anchors_of(path):
                    out.append("%s:%d: the link to %s#%s names no heading (a redirect <a id> does not count; check the heading's slug)" % (rel, n, path, anchor))
    return out


def looks_binary(data):
    return b"\0" in data[:8192]


def private_name_matcher(digests=PRIVATE_NAME_DIGESTS):
    """holds(run): does a run of lower-case name characters hold a window with the digest and the length of an entry? Remembers its answers (a repository has few distinct runs).
    holds.spans(text): the (start, end) offsets of those windows in a lower-case text (to hide a name in a path before the path is printed)."""
    wanted = {}
    for digest, length in digests:
        if length < PRIVATE_NAME_MIN_LENGTH:
            raise ValueError("an entry of %d characters: at least %d are needed (a shorter one could be found again by trying every spelling)" % (length, PRIVATE_NAME_MIN_LENGTH))
        wanted.setdefault(length, set()).add(digest)
    shortest = min(wanted) if wanted else 0
    memo = {}

    def holds(run):
        if len(run) < shortest:
            return False
        hit = memo.get(run)
        if hit is None:
            hit = any(hashlib.sha256(run[i:i + n].encode("utf-8")).hexdigest() in found for n, found in wanted.items() for i in range(len(run) - n + 1))
            memo[run] = hit
        return hit

    def spans(text):
        found_at = []
        for m in PRIVATE_RUN.finditer(text):
            for n, found in wanted.items():
                for i in range(len(m.group(0)) - n + 1):
                    if hashlib.sha256(m.group(0)[i:i + n].encode("utf-8")).hexdigest() in found:
                        found_at.append((m.start() + i, m.start() + i + n))
        return found_at
    holds.spans = spans
    return holds


def check_forbidden(root, files, allowed=FORBIDDEN_ALLOWED, digests=PRIVATE_NAME_DIGESTS):
    out = []
    holds = private_name_matcher(digests)
    for rel in files:
        low = rel.lower()
        if any(holds(run) for run in PRIVATE_RUN.findall(low)):                  # (the name of a tracked file counts too: a private file is kept out by its name as much as by its text)
            shown = list(rel if len(low) == len(rel) else low)
            for start, end in holds.spans(low):
                shown[start:end] = "*" * (end - start)
            out.append("%s: the path names a private planning document or repository (PRIVATE_NAME_DIGESTS; the name is hidden here)" % "".join(shown))
        if rel.startswith("previews/") or "/previews/" in rel or rel in VENDORED_CODE:       # (vendored code is not ours: SQLite's own comments say "the other thread")
            continue
        if os.path.splitext(rel)[1].lower() in TEXT_SUFFIXES_SKIPPED:
            continue
        data = read_bytes(root, rel)
        if looks_binary(data):
            continue
        text = data.decode("utf-8", "replace")
        private = any(holds(run) for run in set(PRIVATE_RUN.findall(text.lower())))      # (once for the whole file; the lines are looked at one by one only when there is a hit)
        for n, line in enumerate(text.splitlines(), 1):
            m = None if rel in allowed else FORBIDDEN.search(line)                       # (the allow-list is for the files that must spell the trailers: the names are looked for in them too)
            if m:
                out.append("%s:%d: names '%s' (private planning, trailer or session text does not belong in a public file)" % (rel, n, m.group(0)))
            if private and any(holds(run) for run in PRIVATE_RUN.findall(line.lower())):
                out.append("%s:%d: names a private planning document or repository (PRIVATE_NAME_DIGESTS; the name is not repeated here)" % (rel, n))
    return out


def kind_of(rel):
    ext = os.path.splitext(rel)[1]
    if rel.startswith("tests/") and ext in (".cpp", ".hpp", ".js", ".py"):
        return "test", size_limits.TEST_BASE_LINES
    if rel.startswith(("src/", "include/", "web/", "tools/")) and ext in (".cpp", ".hpp", ".js", ".html", ".py"):
        return "production", size_limits.PRODUCTION_BASE_LINES
    return None, None


ASSERT_DEF = re.compile(r"^\s*#\s*define\s+ASSERT_TRUE\b", re.M)


def assert_true_count(root, files):
    total = 0
    for rel in files:
        if rel.startswith(("tests/", "src/", "include/", "tools/")) and os.path.splitext(rel)[1] in (".cpp", ".hpp", ".h", ".inc", ".js", ".py"):
            data = read_bytes(root, rel)
            if not looks_binary(data):
                total += len(ASSERT_DEF.findall(data.decode("utf-8", "replace")))
    return total


def check_assert_true(root, files, allowed=ASSERT_TRUE_DEFINITIONS):
    n = assert_true_count(root, files)
    if n > allowed:
        return ["ASSERT_TRUE: %d definitions, at most %d (include tests/common/test_assert.hpp, define no new copy)" % (n, allowed)]
    if n < allowed:
        return ["ASSERT_TRUE: %d definitions, fewer than the constant %d: lower the constant" % (n, allowed)]
    return []


def check_cmake(root, files, exceptions=CMAKE_EXCEPTIONS):
    cmake = "".join(read_bytes(root, r).decode("utf-8", "replace") for r in files if os.path.basename(r) == "CMakeLists.txt" or r.endswith(".cmake"))
    out = []
    for rel in files:
        if rel.endswith(".cpp") and rel.startswith(("src/", "tests/", "tools/")) and rel not in exceptions:
            if not re.search(r"(?<![\w.-])" + re.escape(os.path.basename(rel)) + r"(?![\w.-])", cmake):
                out.append("%s: not named in any CMakeLists.txt (add it to the build or to CMAKE_EXCEPTIONS)" % rel)
    return out


BUILD_FOLDER = re.compile(r"^(build(?:_[^/]*)?)/")      # build/ and build_*/ at the top of the repository (build_web.sh is a file, not a folder)


def check_build_folders(files):
    """One finding for each build folder that holds a tracked file (a compiler's or CMake's output, thousands of files: not one line for each)."""
    tracked = {}
    for rel in files:
        m = BUILD_FOLDER.match(rel)
        if m:
            tracked.setdefault(m.group(1), []).append(rel)
    return ["%s/: %d tracked file(s), for example %s: a build folder is local output and never committed (git rm -r --cached %s, and keep it in .gitignore)"
            % (folder, len(names), names[0], folder) for folder, names in sorted(tracked.items())]


BRITISH = "col" + "our"       # (the word the project does not write; split so that this file passes its own check)
SPELLING_WORD = re.compile(BRITISH, re.I)
SPELLING_SKIPPED = (
    "Community-Maps/",         # the players' maps and their README: never edited here
    "docs/legacy/",            # extracts of the original program, kept as the original wrote them
    "third_party/",            # other people's code
)
_NOTICE = re.escape("This map cannot be played with these %ss." % BRITISH)    # the server's notice, which the web lobby matches word for word (protocol 21)
# The British forms that still exist, each by place so that `x = q.<word>` anywhere else is found: the older spelling is accepted and written for one release (docs/REPLAYS.md, "The match history")
_QUOTED = r"""["'`]%s["'`]""" % BRITISH      # the key "<word>" of a seat in a match record (written next to color, read by the pages when color is missing), and the older parameter's name in code and prose
_PARAM = r"\b%s=" % BRITISH                  # the query parameter <word>= of GET /history (still accepted), in the server, its tests and the notes
_PROP = r"\.%s\b" % BRITISH                 # the same key read as a property
_OBJKEY = r"\b%s:" % BRITISH                 # ... or written as an object key (a test's older record)
SPELLING_KEPT = {              # file -> patterns that may keep the British word on a line
    # the wire notice: the server's text, the lobby page that compares with it, the tests that pin it
    "include/ants_net/protocol.hpp": [_NOTICE], "web/front/lobby_rules.js": [_NOTICE], "tests/scripts/test_web_lobby_rules.py": [_NOTICE],
    "tests/scripts/web_lobby_rules_check.js": [_NOTICE], "tests/test_net/test_lockstep_protocols.cpp": [_NOTICE],
    # the older spelling of the history's color: the alias in the query, the deprecated key of a record, the pages' fallback, the note, and the tests of each
    "src/ants_server/control_history.cpp": [_QUOTED, _PARAM], "src/ants_server/history_record.cpp": [_QUOTED], "include/ants_server/control.hpp": [_PARAM],
    "docker/nginx.conf": [_PARAM], "web/history_page.js": [_QUOTED, _PARAM, _PROP], "docs/REPLAYS.md": [_QUOTED, _PARAM],
    "tests/test_server/test_history_record.cpp": [_QUOTED], "tests/test_server/test_history_store.cpp": [_QUOTED, _PARAM],
    "tests/scripts/test_nginx_history.py": [_QUOTED, _PARAM], "tests/scripts/web_history_check.js": [_PARAM, _OBJKEY],
    "tests/scripts/web_matches_check.py": [_QUOTED], "tests/scripts/test_web_matches.py": [_PROP, _QUOTED],
}


def check_spelling_table(root, files, kept=None):
    """A kept pattern that no line of its file uses is a finding: the table stays as short as the older spelling's remaining places."""
    kept = SPELLING_KEPT if kept is None else kept
    out = []
    for rel, patterns in sorted(kept.items()):
        if rel not in files:
            out.append("%s: in the spelling table but not tracked (remove it from the table)" % rel)
            continue
        lines = read_bytes(root, rel).decode("utf-8", "replace").splitlines()
        for pat in patterns:
            if not any(re.search(pat, line) for line in lines):
                out.append("%s: the kept spelling pattern %s is used by no line (remove it from SPELLING_KEPT)" % (rel, pat))
    return out


def check_spelling(root, files, skipped=SPELLING_SKIPPED, kept=None):
    kept = SPELLING_KEPT if kept is None else kept
    out = []
    for rel in files:
        if rel.startswith(skipped) or os.path.splitext(rel)[1].lower() in TEXT_SUFFIXES_SKIPPED:
            continue
        if SPELLING_WORD.search(rel):
            out.append("%s: the file name has the British spelling (write the American one)" % rel)
        data = read_bytes(root, rel)
        if looks_binary(data):
            continue
        allowed = re.compile("|".join(kept[rel])) if kept.get(rel) else None      # (case-sensitive: only the lower-case forms are kept)
        for n, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
            if SPELLING_WORD.search(allowed.sub("", line) if allowed else line):
                out.append("%s:%d: British spelling (AGENTS.md, Spelling: American English; write the American spelling of color)" % (rel, n))
    return out


def run_all(root):
    """(errors, warnings) of every check; a warning is (file, text, kind, percent)."""
    files = tracked_files(root)
    size_errors, warnings = check_sizes(root, files)
    errors = (size_errors + check_reachable(root, files) + check_anchors(root, files) + check_forbidden(root, files) + check_assert_true(root, files) + check_cmake(root, files)
              + check_build_folders(files) + check_spelling(root, files) + check_spelling_table(root, files))
    long = long_line_warning(check_long_lines(root, files))
    return errors, warnings + ([long] if long else [])


WARNING_ORDER = {"split": 0, "target": 1, "pending": 2, "long": 3}      # (what a reader of the pull request should see first)


def warning_lines(warnings, github=None):
    """The lines that print the warnings, the files closest to their limit first. Plain: `warning: FILE: TEXT` for each, and `notice: ...` for the long lines. Under GITHUB_ACTIONS they are
    annotations of the pull request, and GitHub shows only ANNOTATION_LIMIT warnings of a step: so at most that many are printed, the last one saying how many more there are (`--list`
    has them all); the long lines are a ::notice, which has a count of its own."""
    github = bool(os.environ.get("GITHUB_ACTIONS")) if github is None else github
    ordered = sorted(warnings, key=lambda w: (WARNING_ORDER[w[2]], -w[3], w[0]))
    sized = [w for w in ordered if w[2] != "long"]
    notices = [w for w in ordered if w[2] == "long"]
    if not github:
        return ["warning: %s: %s" % (w[0], w[1]) for w in sized] + ["notice: %s: %s" % (w[0], w[1]) for w in notices]
    room = size_limits.ANNOTATION_LIMIT
    shown = sized if len(sized) <= room else sized[:room - 1]
    out = ["::warning file=%s::%s" % (w[0], w[1]) for w in shown]
    if len(shown) < len(sized):
        out.append("::warning::%d more file(s) are over their target or split line: python3 tools/check_hygiene.py --list shows them all" % (len(sized) - len(shown)))
    return out + ["::notice file=%s::%s" % (w[0], w[1]) for w in notices]


def list_numbers(root):
    files = tracked_files(root)
    over = []
    for rel in files:
        got = measure(root, rel)
        if got and got[2] > levels(got[3])["target"]:
            over.append((percent(got[2], got[3]), got, rel))
    lines = ["files over their target (documents %s bytes, production %s lines, tests %s lines; the target is %d%%, the split line %d%%, the safety cap %d%%; largest first):"
             % (num(size_limits.DOC_BASE_BYTES), num(size_limits.PRODUCTION_BASE_LINES), num(size_limits.TEST_BASE_LINES), size_limits.TARGET_PERCENT, size_limits.SPLIT_PERCENT, size_limits.CAP_PERCENT)]
    for pct, (kind, unit, size, base), rel in sorted(over, reverse=True):
        at = levels(base)
        state = "over the cap, waiting in SPLIT_PENDING" if rel in size_limits.SPLIT_PENDING else "over the cap" if size > at["cap"] else "split it now" if size >= at["split"] else "over the target"
        lines.append("  %9s %-5s %5d%%  %s  (%s)" % (num(size), unit, pct, rel, state))
    long = check_long_lines(root, files)
    if long:
        lines.append("documents with lines over %s characters (a reader may cut them):" % num(size_limits.DOC_LINE_WARN_CHARACTERS))
        lines += ["  %3d line(s), the longest %s characters (line %d)  %s" % (n, num(size), at, rel) for rel, n, size, at in sorted(long, key=lambda f: -f[2])]
    lines.append("ASSERT_TRUE definitions: %d (constant %d)" % (assert_true_count(root, files), ASSERT_TRUE_DEFINITIONS))
    return lines


def main(argv):
    parser = argparse.ArgumentParser(description="Repository hygiene check (see the top of this file).")
    parser.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."), help="the repository (default: this tool's)")
    parser.add_argument("--list", action="store_true", help="print every file over its target size, and exit")
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit as e:
        return 2 if e.code else 0
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print("check_hygiene: %s is not a folder" % root, file=sys.stderr)
        return 2
    if args.list:
        print("\n".join(list_numbers(root)))
        return 0
    findings, warnings = run_all(root)
    for line in warning_lines(warnings):
        print(line, file=sys.stderr)
    for f in findings:
        print(f)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
