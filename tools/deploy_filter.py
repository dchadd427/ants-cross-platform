#!/usr/bin/env python3
"""Does a push change what the site serves or runs? The deploy job of .github/workflows/ci.yml asks this before it calls the deploy webhook.

usage: deploy_filter.py (--base REV --head REV | --files) [--root DIR] [--compose FILE] [--github-output FILE] [--explain]

  --base REV --head REV   the changed files are those of `git diff --name-only --no-renames REV REV` (a revision that cannot be compared: deploy, to be safe)
  --files                 the changed files, one per line, on standard input (tests, a dry run: `git diff --name-only A B | tools/deploy_filter.py --files`)
  --compose FILE          the stack file of the site that is deployed (default docker-compose.stack.yml; docker-compose.staging.yml for the staging stack)
  --github-output FILE    append deploy=true or deploy=false (and reason=...) there, as a workflow step's output
  --explain               print the verdict of every changed file

A change counts when it can alter an image or the stack: a file that a Dockerfile COPYs from the build context and that .dockerignore lets through, the
Dockerfiles, .dockerignore and the stack file. Both come from the real files (docs/WORKFLOW.md lists the result): documents (README.md, STATUS.md, AGENTS.md, docs/
except docs/CHANGELOG_ARCHIVE.md), .github/, tests/ and the tools that no image runs change nothing that a player or the server sees, so they do not redeploy the site (a
redeploy restarts it and ends the matches that are running). Exceptions, files that are copied and still change nothing: see COPIED_UNUSED below.
Prints "deploy: ..." or "skip: ..." and always exits 0 (a wrong call is exit 2).
"""
import argparse
import json
import os
import re
import subprocess
import sys

# Copied into a build context and still not in any image. tools/: the server image copies the folder because CMakeLists.txt names the sources of the tools that it does
# not build; the web image copies the one script that makes the changelog pages. .gitattributes and .gitignore: `COPY VERSION .git*` copies them so that the copy works
# when the context has no .git folder.
COPIED_UNUSED = (
    ("tools/", "tools/changelog_to_html.py", "the server image copies tools/ only for CMake; none of it is built there"),
    (".gitattributes", None, "copied only so that COPY VERSION .git* works without a .git folder"),
    (".gitignore", None, "copied only so that COPY VERSION .git* works without a .git folder"),
)
DEFINITIONS = ("Dockerfile", "Dockerfile.server", ".dockerignore")


def translate(pattern):
    """A .dockerignore pattern or a COPY source with wildcards as a regular expression (the rules of Go's filepath.Match as Docker uses them: * and ? stay inside a
    folder name, ** crosses folders, a backslash quotes)."""
    pattern = re.sub(r"^(\./|/)+", "", pattern)
    out = ["^"]
    i = 0
    while i < len(pattern):
        c = pattern[i]
        if c == "*":
            if pattern[i + 1:i + 2] == "*":
                i += 1
                if pattern[i + 1:i + 2] == "/":
                    out.append("(.*/)?")
                    i += 1
                else:
                    out.append(".*")
            else:
                out.append("[^/]*")
        elif c == "?":
            out.append("[^/]")
        elif c == "\\" and i + 1 < len(pattern):
            i += 1
            out.append(re.escape(pattern[i]))
        elif c == "[":
            end = pattern.find("]", i + 2)
            if end < 0:
                out.append(re.escape(c))
            else:
                body = pattern[i + 1:end]
                out.append("[" + ("^" if body.startswith("^") else "") + body.lstrip("^").replace("\\", "\\\\") + "]")
                i = end
        else:
            out.append(re.escape(c))
        i += 1
    out.append("$")
    return re.compile("".join(out))


def parents(path):
    parts = path.split("/")
    return ["/".join(parts[:n]) for n in range(1, len(parts))]


class DockerIgnore:
    """What .dockerignore keeps out of the build context: the patterns in order, a `!` pattern lets a path back in, the last pattern that matches wins; a pattern
    that matches a folder keeps out everything below it (the rules of Docker's own matcher)."""

    def __init__(self, text):
        self.rules = []
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            negate = line.startswith("!")
            self.rules.append((negate, translate(line[1:].strip() if negate else line)))

    def ignored(self, path):
        ignored = False
        for negate, regex in self.rules:
            if negate != ignored:                       # a `!` rule only matters while the path is out, a plain rule only while it is in
                continue
            if regex.match(path) or any(regex.match(p) for p in parents(path)):
                ignored = not negate
        return ignored


def copy_sources(dockerfile_text):
    """The sources of every COPY (and ADD) that reads the build context (not `--from=` another stage, not a URL)."""
    joined = re.sub(r"\\\r?\n", " ", dockerfile_text)
    sources = []
    for line in joined.splitlines():
        m = re.match(r"\s*(COPY|ADD)\s+(.*)$", line, re.I)
        if not m:
            continue
        rest = m.group(2).strip()
        if rest.startswith("["):
            try:
                tokens = json.loads(rest)
            except ValueError:
                raise ValueError("cannot read this COPY line: " + line.strip())
        else:
            tokens = rest.split()
        flags = [t for t in tokens if t.startswith("--")]
        tokens = [t for t in tokens if not t.startswith("--")]
        if any(f.startswith("--from") for f in flags):
            continue
        if any(t.startswith("<<") for t in tokens):
            raise ValueError("a heredoc COPY is not supported: " + line.strip())
        sources += [t for t in tokens[:-1] if "://" not in t]
    return sources


class Classifier:
    def __init__(self, root, compose):
        self.compose = compose
        self.ignore = DockerIgnore(self.read(root, ".dockerignore"))
        self.sources = []
        for dockerfile in ("Dockerfile", "Dockerfile.server"):
            self.sources += [(s, self.matcher(s)) for s in copy_sources(self.read(root, dockerfile))]

    @staticmethod
    def read(root, name):
        with open(os.path.join(root, name), encoding="utf-8") as f:
            return f.read()

    @staticmethod
    def matcher(source):
        source = re.sub(r"^(\./|/)+", "", source)
        if source in ("", ".", "./"):
            return lambda path: True
        if re.search(r"[*?\[]", source):
            regex = translate(source)
            return lambda path: bool(regex.match(path)) or any(regex.match(p) for p in parents(path))
        folder = source.rstrip("/")
        return lambda path: path == folder or path.startswith(folder + "/")

    def verdict(self, path):
        """(counts, why)"""
        path = path.replace("\\", "/").lstrip("./") if path.startswith("./") else path.replace("\\", "/")
        if path in DEFINITIONS:
            return True, "the definition of the images"
        if path == self.compose:
            return True, "the stack file of the site"
        if self.ignore.ignored(path):
            return False, "kept out of the images by .dockerignore"
        for folder, keep, why in COPIED_UNUSED:
            inside = path == folder or (folder.endswith("/") and path.startswith(folder))
            if inside and path != keep:
                return False, why
        for source, matches in self.sources:
            if matches(path):
                return True, "copied into an image (COPY %s)" % source
        return False, "no image copies it"


def changed_files(args, root):
    if args.files:
        return [line.strip() for line in sys.stdin.read().splitlines() if line.strip()], None
    done = subprocess.run(["git", "-C", root, "diff", "--name-only", "--no-renames", args.base, args.head], capture_output=True, text=True)
    if done.returncode != 0:
        return None, "the changes between %s and %s cannot be listed (%s)" % (args.base[:12], args.head[:12], (done.stderr.strip().splitlines() or ["git failed"])[-1][:100])
    return [line for line in done.stdout.splitlines() if line.strip()], None


def main(argv):
    parser = argparse.ArgumentParser(description="Does a push change what the site serves or runs? (see the top of this file)")
    parser.add_argument("--base", help="the revision the push started from")
    parser.add_argument("--head", help="the revision the push ended at")
    parser.add_argument("--files", action="store_true", help="read the changed files from standard input")
    parser.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."), help="the repository's root folder (default: this script's parent)")
    parser.add_argument("--compose", default="docker-compose.stack.yml", help="the stack file of the deployed site (default: docker-compose.stack.yml)")
    parser.add_argument("--github-output", help="a file to append deploy=true|false to")
    parser.add_argument("--explain", action="store_true", help="print the verdict of every changed file")
    args = parser.parse_args(argv[1:])
    if not args.files and not (args.base and args.head):
        print("deploy_filter: give --base and --head, or --files", file=sys.stderr)
        return 2
    root = os.path.abspath(args.root)

    reason = None
    deploy = None
    try:
        classifier = Classifier(root, args.compose)
        files, problem = changed_files(args, root)
    except (OSError, ValueError) as e:
        classifier, files, problem = None, None, "the images' files cannot be read (%s)" % e
    if problem:
        deploy, reason = True, "deploy: %s, so the push is deployed to be safe" % problem
    elif not files:
        deploy, reason = False, "skip: no file changed"
    else:
        verdicts = [(f, *classifier.verdict(f)) for f in files]
        if args.explain:
            for f, counts, why in verdicts:
                print("  %-7s %s  (%s)" % ("DEPLOY" if counts else "skip", f, why))
        counting = [v for v in verdicts if v[1]]
        if counting:
            first = counting[0]
            deploy, reason = True, "deploy: %s changed (%s)%s" % (first[0], first[2], (" and %d more file(s) count" % (len(counting) - 1)) if len(counting) > 1 else "")
        else:
            deploy, reason = False, "skip: only files that no image contains changed (%d file(s): documents, .github, tests, tools)" % len(files)
    print(reason)
    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8", newline="\n") as f:
            f.write("deploy=%s\nreason=%s\n" % ("true" if deploy else "false", reason.replace("\n", " ")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
