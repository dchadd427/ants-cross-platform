#!/usr/bin/env python3
"""The paths of the game server, the site's services and their tests (tools/server_paths.txt), and the tree without them. Pure python 3 standard library.

usage: server_paths.py [--root DIR] --list-server | --list-game | --check | --export DIR

  --list-server   the tracked files that the list names, one path a line
  --list-game     the tracked files that it does not name: the game, its libraries, its clients and the tests that run without the server
  --check         every pattern of the list matches a tracked file; one that matches none is a finding, exit 1 (UNMATCHED_ALLOWED may match none)
  --export DIR    writes HEAD without the listed paths into DIR (which is not there yet or is empty), and the file GAME_ONLY_FROM with the commit's SHA: a checkout of the game alone

The list is one pattern a line: `dir/` is everything under the directory; a pattern with a `*` (or a `?` or a `[`) is a glob by the rules of fnmatch, one path component at a time, so that `*`
matches inside a component and never across a `/`; anything else is one file's path. `#` starts a comment (a whole line or the end of one), blank lines are ignored, there is no negation.
Paths are tracked paths (`git ls-files`), relative to the root and written with `/`.
Exit status: 0 done, 1 a finding of --check, 2 a usage error or a failure (no git repository, no list, a folder for --export that is not empty).
"""
import argparse
import fnmatch
import os
import shutil
import subprocess
import sys
import tarfile

MANIFEST = "tools/server_paths.txt"
STAMP = "GAME_ONLY_FROM"
UNMATCHED_ALLOWED = ("tests/test_app_server/", "docs/SERVER_DESIGN.md")        # (patterns that may match no tracked file)


def parse(text):
    """The patterns in a list's text, in order. A line that cannot be a pattern of this list (a path from the root, written with /; no !, no **, no * in a folder) is an error."""
    patterns = []
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split("/")
        if line.startswith(("/", "!")) or "\\" in line or "**" in line or ".." in parts or "" in parts[:-1] or (line.endswith("/") and "*" in line):
            raise ValueError("%s:%d: %r is not a pattern (a path from the root, with /; no !, no **, no * in a folder)" % (MANIFEST, number, line))
        patterns.append(line)
    return patterns


def load(root):
    """The patterns of ROOT/tools/server_paths.txt."""
    with open(os.path.join(root, *MANIFEST.split("/")), encoding="utf-8") as f:
        return parse(f.read())


def is_glob(pattern):
    return not pattern.endswith("/") and any(c in pattern for c in "*?[")


def glob_names(glob, parts):
    """Does the glob (a pattern's components) name the path (its components)? The same number of components, each matching by the rules of fnmatch."""
    return len(glob) == len(parts) and all(fnmatch.fnmatchcase(a, b) for a, b in zip(parts, glob))


def matcher(patterns):
    """A function that answers for one path whether any of the patterns names it."""
    dirs = tuple(p for p in patterns if p.endswith("/"))
    globs = [p.split("/") for p in patterns if is_glob(p)]
    exact = frozenset(p for p in patterns if not p.endswith("/") and not is_glob(p))

    def match(rel):
        if rel in exact or rel.startswith(dirs):
            return True
        parts = rel.split("/")
        return any(glob_names(g, parts) for g in globs)
    return match


def is_server_path(rel, patterns):
    """True when the patterns name the path REL (relative to the root, with /)."""
    return matcher(patterns)(rel)


def tracked(root):
    """The tracked paths of the checkout at ROOT (`git ls-files`), sorted."""
    done = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True)
    if done.returncode != 0:
        raise RuntimeError("git ls-files failed in %s: %s" % (root, done.stderr.decode("utf-8", "replace").strip()))
    return sorted(os.fsdecode(p) for p in done.stdout.split(b"\0") if p)


def split(root):
    """(game files, server files): the tracked files that the list does not name, and those it does."""
    match = matcher(load(root))
    game, server = [], []
    for rel in tracked(root):
        (server if match(rel) else game).append(rel)
    return game, server


def dead_patterns(root):
    """The patterns that match no tracked file, except those of UNMATCHED_ALLOWED. One pass over the files: each one makes its own path, the folders it is in and the globs that name it alive."""
    patterns = load(root)
    globs = {p: p.split("/") for p in patterns if is_glob(p)}
    alive = set()
    for rel in tracked(root):
        parts = rel.split("/")
        alive.add(rel)
        alive.update("/".join(parts[:i]) + "/" for i in range(1, len(parts)))
        alive.update(p for p, g in globs.items() if p not in alive and glob_names(g, parts))
    return [p for p in patterns if p not in alive and p not in UNMATCHED_ALLOWED]


def export(root, dest):
    """Writes HEAD of the checkout at ROOT without the listed paths into DEST, keeping the executable bit, and DEST/GAME_ONLY_FROM with the SHA of HEAD. Returns the number of files written."""
    if os.path.exists(dest) and (not os.path.isdir(dest) or os.listdir(dest)):
        raise ValueError("%s exists and is not an empty folder" % dest)
    match = matcher(load(root))
    head = subprocess.run(["git", "-C", root, "rev-parse", "--verify", "HEAD"], capture_output=True, text=True)
    if head.returncode != 0:
        raise RuntimeError("git rev-parse HEAD failed in %s: %s" % (root, head.stderr.strip()))
    os.makedirs(dest, exist_ok=True)
    archive = subprocess.Popen(["git", "-C", root, "archive", "--format=tar", "HEAD"], stdout=subprocess.PIPE)
    written = 0
    try:
        with tarfile.open(fileobj=archive.stdout, mode="r|") as tar:
            for member in tar:
                parts = member.name.split("/")
                if member.isdir() or match(member.name):
                    continue
                if not member.isfile() or member.name.startswith("/") or ".." in parts:
                    raise RuntimeError("%s: git archive gave a path or a kind of file that is not handled (a link?)" % member.name)
                target = os.path.join(dest, *parts)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, "wb") as out:
                    shutil.copyfileobj(tar.extractfile(member), out)
                os.chmod(target, 0o755 if member.mode & 0o111 else 0o644)
                written += 1
        archive.stdout.read()                   # (what follows the end of the archive: git writes its last block before it ends, and its status is then its own)
    except BaseException as e:
        archive.kill()
        archive.wait()
        if isinstance(e, tarfile.TarError):
            raise RuntimeError("git archive failed in %s (%s)" % (root, e)) from e
        raise
    finally:
        archive.stdout.close()
    if archive.wait() != 0:
        raise RuntimeError("git archive failed in %s" % root)
    with open(os.path.join(dest, STAMP), "w", encoding="utf-8") as f:
        f.write(head.stdout.strip() + "\n")
    return written


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."), help="the checkout (default: this tool's)")
    what = parser.add_mutually_exclusive_group(required=True)
    what.add_argument("--list-server", action="store_true", help="print the tracked files that the list names")
    what.add_argument("--list-game", action="store_true", help="print the tracked files that the list does not name")
    what.add_argument("--check", action="store_true", help="every pattern matches a tracked file")
    what.add_argument("--export", metavar="DIR", help="write HEAD without the listed paths into DIR")
    args = parser.parse_args(argv[1:])
    root = os.path.abspath(args.root)
    try:
        if args.check:
            dead = dead_patterns(root)
            for p in dead:
                print("%s: %r matches no tracked file (fix its spelling or remove the line)" % (MANIFEST, p))
            if not dead:
                game, server = split(root)
                print("server_paths: every pattern matches (%d paths listed, %d not)" % (len(server), len(game)))
            return 1 if dead else 0
        if args.export:
            print("server_paths: %d files written to %s (%s: the commit)" % (export(root, args.export), args.export, STAMP))
            return 0
        game, server = split(root)
        sys.stdout.write("".join(rel + "\n" for rel in (server if args.list_server else game)))
        return 0
    except (OSError, ValueError, RuntimeError) as e:
        print("server_paths: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
