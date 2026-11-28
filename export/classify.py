#!/usr/bin/env python3
"""Checks that every tracked file is on exactly ONE of the two lists of the export (export/allowlist.txt: public, export/private.txt: private), and prints the ones that are not.

    export/classify.py [--repo DIR] [--allow FILE] [--private FILE] [--list public|private] [--filter-repo-paths] [--dead] [--quiet]

  default        print the unclassified files (on neither list) and the conflicts (on both), then a count; exit code 1 when there is any, else 0.
  --list public  print every file that goes to the public repository (one path per line, after the overlay rule below): the input of the export.
  --list private print every file that stays private.
  --filter-repo-paths  print the allow-list as `regex:` lines for `git filter-repo --paths-from-file` (the first export). The regexes are the very ones that this script matches with, so
                 the history is cut by the same rule as the check. The overlay lines are left out: the overlay is added by the sync commit.
  --dead         also print the patterns that match no file (a list should not keep patterns that nothing needs; the ones of files that a later change brings are meant).
  --repo DIR     the tree to check (default: the repository that holds this script). Its files are `git ls-files` of that folder; a folder that is not a git checkout is walked.

Pattern syntax (the same in both lists; a line is a pattern, '#' starts a comment, blank lines are ignored):
  dir/**         everything below dir at any depth        name*, dir/pre*.ext   '*' is any run of characters but '/' ; '?' is one character but '/'
  a/b/c.txt      exactly that path                          !pattern             removes what the lines BEFORE it of the SAME list matched (last line that matches wins)
There are no character classes: '[' and ']' are plain characters, so a file name that has them (Community-Maps has a few odd names) cannot surprise a pattern.
The files under export/overlay/ are exported without that prefix (export/overlay/README.md becomes README.md): --list public prints them under their public names.
Only tracked files count (the index, as check_hygiene.py does): a new file is checked once it is added.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OVERLAY = 'export/overlay/'


def glob_to_regex(pat):
    out = []
    i = 0
    while i < len(pat):
        c = pat[i]
        if c == '*':
            if pat.startswith('**', i):
                out.append('.*')
                i += 2
                continue
            out.append('[^/]*')
        elif c == '?':
            out.append('[^/]')
        else:
            out.append(re.escape(c))
        i += 1
    return re.compile('^' + ''.join(out) + '$')


def read_list(path):
    items = []   # (negated, pattern, regex)
    with open(path, encoding='utf-8') as f:
        for raw in f:
            line = raw.rstrip('\n')
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            neg = line.startswith('!')
            pat = line[1:] if neg else line
            pat = pat.strip()
            items.append((neg, pat, glob_to_regex(pat)))
    return items


def in_list(items, path, hits=None):
    """True when the last pattern that matches `path` is not negated."""
    state = False
    for k, (neg, pat, rx) in enumerate(items):
        if rx.match(path):
            state = not neg
            if hits is not None:
                hits[k] += 1
    return state


def tracked_files(repo):
    try:
        raw = subprocess.check_output(['git', '-C', repo, 'ls-files', '-z'], stderr=subprocess.DEVNULL)
        return [p for p in raw.decode('utf-8', 'surrogateescape').split('\0') if p]
    except (subprocess.CalledProcessError, FileNotFoundError):
        found = []
        for root, dirs, names in os.walk(repo):
            dirs[:] = [d for d in dirs if d != '.git']
            for n in names:
                found.append(os.path.relpath(os.path.join(root, n), repo).replace(os.sep, '/'))
        return sorted(found)


def classify(files, allow, private, count_hits=False):
    ha = [0] * len(allow) if count_hits else None
    hp = [0] * len(private) if count_hits else None
    pub, priv, none, both = [], [], [], []
    for f in files:
        a = in_list(allow, f, ha)
        p = in_list(private, f, hp)
        if a and p:
            both.append(f)
        elif a:
            pub.append(f)
        elif p:
            priv.append(f)
        else:
            none.append(f)
    return pub, priv, none, both, ha, hp


def public_name(path):
    return path[len(OVERLAY):] if path.startswith(OVERLAY) else path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo', default=os.path.dirname(HERE))
    ap.add_argument('--allow', default=os.path.join(HERE, 'allowlist.txt'))
    ap.add_argument('--private', default=os.path.join(HERE, 'private.txt'))
    ap.add_argument('--list', choices=['public', 'private'])
    ap.add_argument('--filter-repo-paths', action='store_true')
    ap.add_argument('--dead', action='store_true')
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args(argv)
    allow, private = read_list(args.allow), read_list(args.private)
    if args.filter_repo_paths:
        for neg, pat, rx in allow:
            if not neg and not pat.startswith('export/'):
                print('regex:' + rx.pattern)
        return 0
    files = tracked_files(args.repo)
    pub, priv, none, both, ha, hp = classify(files, allow, private, count_hits=args.dead)
    if args.list == 'public':
        for f in pub:
            print(public_name(f))
        return 0 if not none and not both else 1
    if args.list == 'private':
        for f in priv:
            print(f)
        return 0 if not none and not both else 1
    if not args.quiet:
        for f in none:
            print('UNCLASSIFIED  ' + f)
        for f in both:
            print('ON BOTH LISTS ' + f)
    if args.dead:
        for (neg, pat, _), n in zip(allow, ha):
            if n == 0 and not neg:
                print('DEAD (allowlist)  ' + pat)
        for (neg, pat, _), n in zip(private, hp):
            if n == 0 and not neg:
                print('DEAD (private)    ' + pat)
    print('files %d: public %d, private %d, unclassified %d, on both lists %d' % (len(files), len(pub), len(priv), len(none), len(both)))
    if (none or both) and not args.quiet:
        print('Put each file on exactly one list (export/README.md, "Which list a file goes on").')
    return 1 if none or both else 0


if __name__ == '__main__':
    sys.exit(main())
