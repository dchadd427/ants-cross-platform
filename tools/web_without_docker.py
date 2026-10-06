#!/usr/bin/env python3
"""The web image without Docker: builds the files that the Dockerfile puts in the image and serves them with nginx, so that the browser checks run where no Docker daemon does (a cloud session).

usage: web_without_docker.py [--context DIR] [--work DIR] [--emsdk DIR] [--port N] [--build-arg NAME=VALUE]... [--reuse] [--keep] [--stop] [-- COMMAND [ARG ...]]

    tools/web_without_docker.py -- tests/scripts/test_web_aspect.sh        (a command that reads ANTS_WEB_URL and needs only the page: also test_web_edge.sh, test_web_touch.sh and test_web_home.sh)

It replays the Dockerfile itself (FROM, ARG, ENV, WORKDIR, COPY with the .dockerignore and COPY --from, RUN) on this machine, with the three folders that the Dockerfile writes to (/src,
/usr/share/nginx/html, /etc/nginx) moved under WORK/fs, so the pages are made by the commands of the image's build. The game is compiled by Emscripten (the version of the Dockerfile's
FROM, installed with emsdk when it is not there) and the ports that it needs (SDL2, SDL2_ttf, FreeType, HarfBuzz) are put in Emscripten's cache from git tags and Emscripten's own mirror,
because a session may not download a GitHub archive. Then it writes an nginx.conf around docker/nginx.conf, starts nginx on 127.0.0.1:PORT (default 19980), runs COMMAND with ANTS_WEB_URL
set (and CHROME, when it is not, to a script that starts a browser as the checks need it: Playwright's headless shell, else Chromium, with a desktop's mouse and, as root, --no-sandbox) and stops nginx. It does not run Docker and does not replace CI's `docker build`: that is the gate.
It serves the pages and starts no game server: test_web_home.sh leaves out its parts that play, and test_web_hidden.sh, test_web_prediction.sh and test_web_rejoin.sh, which need one, say SKIP and exit 0 (no pass).

  --context DIR  the tree to build (default: the tree of this script; a git worktree of a branch works)     --work DIR  where everything goes (default: CONTEXT/scratch/web_without_docker)
  --emsdk DIR    the emsdk (default: $EMSDK when it has the right version, else ~/.cache/ants-web/emsdk-VERSION, made there when it is not)     --port N  nginx's port (0: a free one)
  --build-arg    NAME=VALUE for an ARG of the Dockerfile (ANTS_BUILD_ID, ANTS_SITE_LABEL)     --keep  leave nginx running     --stop  stop it
  --reuse        serve what an earlier run built (a run that was cut short is not taken)

The RUN lines of the Dockerfile get the work folder's path as it is, so a path with a space or a character that a shell reads apart is refused: give --work a plain one.

Needs Linux (the Dockerfile's RUN lines use GNU tools), git, bash, cmake, make, python3 and nginx (sudo apt-get update && sudo apt-get install -y nginx), and the network to github.com (git clones)
and storage.googleapis.com (Emscripten's downloads and its mirror). Exit status: COMMAND's, or 0; 1 a step failed; 2 COMMAND could not be started; 3 something that it needs is not
there, or the work folder's name is refused (nothing was built or checked).
"""
import argparse
import fnmatch
import http.client
import io
import json
import os
import posixpath
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_PORT = 19980
START_SECONDS = 10                      # how long nginx gets to answer after it was started
EMSDK_URL = "https://github.com/emscripten-core/emsdk.git"
PORT_MIRROR = "https://storage.googleapis.com/webassembly/emscripten-ports/"
VIRTUAL_ROOTS = ("/src", "/usr/share/nginx/html", "/etc/nginx")      # what the Dockerfile writes to: moved under WORK/fs
BASE_FOLDERS = ("/src", "/usr/share/nginx/html", "/etc/nginx/conf.d")  # what the base images already have (the emsdk image's /src, the nginx image's two)
HARMLESS_PATHS = ("/dev/null", "/bin/sh", "/usr/bin/env")
IGNORED = ("EXPOSE", "HEALTHCHECK", "CMD", "ENTRYPOINT", "LABEL", "MAINTAINER", "STOPSIGNAL", "USER")
MOUSE = "--blink-settings=primaryPointerType=4,availablePointerTypes=4,primaryHoverType=2,availableHoverTypes=2"       # a fine pointer that hovers: a desktop's
PORTS_OF_FLAGS = {("USE_SDL", "2"): "sdl2", ("USE_SDL_TTF", "2"): "sdl2_ttf"}   # -sUSE_SDL=2 -sUSE_SDL_TTF=2 of the CMake files; the other ports come from `deps` of the port files


class ToolError(Exception):
    def __init__(self, message, status=1):
        super().__init__(message)
        self.status = status


def say(text):
    print("[web without docker] " + text, flush=True)


def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def run(command, **kwargs):
    done = subprocess.run(command, **kwargs)
    if done.returncode != 0:
        raise ToolError("%s failed with status %d" % (" ".join(str(c) for c in command[:3]), done.returncode))
    return done


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------- the Dockerfile

class Step:
    def __init__(self, line, stage, op, text):
        self.line, self.stage, self.op, self.text = line, stage, op, text
        self.flags = {}
        words = text.split()
        while self.op in ("COPY", "FROM") and words and words[0].startswith("--"):
            name, _, value = words.pop(0)[2:].partition("=")
            self.flags[name] = value
        self.words = words


def logical_lines(text):
    """[(line number, instruction text)]: the continued lines joined, the comment lines (inside an instruction too) and the blank ones dropped, as Docker reads a Dockerfile."""
    found, current, start = [], "", 0
    for number, raw in enumerate(text.splitlines(), 1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if not current:
            start = number
        piece = raw.rstrip()
        if piece.endswith("\\"):
            current += piece[:-1]
            continue
        found.append((start, (current + piece).strip()))
        current = ""
    if current:
        found.append((start, current.strip()))
    return found


def json_array(text):
    try:
        return isinstance(json.loads(text), list)
    except ValueError:
        return False


def parse_dockerfile(text):
    """[Step] of a Dockerfile. What this tool cannot do (ADD, ONBUILD, SHELL, the exec forms, RUN --mount, heredocs, COPY from an image) is an error, never a step that is left out."""
    steps, stage = [], -1
    for line, instruction in logical_lines(text):
        match = re.match(r"([A-Za-z]+)\s*(.*)$", instruction, re.S)
        op = match.group(1).upper()
        rest = match.group(2)
        if op == "FROM":
            stage += 1
        if op in IGNORED:
            continue
        if op not in ("FROM", "ARG", "ENV", "WORKDIR", "COPY", "RUN"):
            raise ToolError("Dockerfile line %d: %s is not supported by this tool (tools/web_without_docker.py: add it, with a test, or build with Docker)" % (line, op))
        if json_array(rest) or "<<" in rest or (op == "RUN" and rest.startswith("--")):
            raise ToolError("Dockerfile line %d: this form of %s is not supported by this tool" % (line, op))
        steps.append(Step(line, stage, op, rest))
    if not steps or steps[0].op != "FROM":
        raise ToolError("the Dockerfile does not start with FROM")
    return steps


def emscripten_version(steps):
    """The Emscripten version of the Dockerfile's `FROM emscripten/emsdk:X.Y.Z`."""
    for step in steps:
        if step.op == "FROM":
            match = re.fullmatch(r"emscripten/emsdk:(\d+\.\d+\.\d+)", step.words[0] if step.words else "")
            if match:
                return match.group(1)
    raise ToolError("no stage of the Dockerfile is `FROM emscripten/emsdk:X.Y.Z`: this tool does not know how to get its compiler")


# ----------------------------------------------------------------------------------------------------------------------------------------------------------- the .dockerignore

class DockerIgnore:
    """The matching of .dockerignore as Docker does it: `*` and `?` stay within a name, `**` crosses folders, the last pattern that matches wins, `!` brings back, and a pattern that matches a
    folder takes its content with it."""

    def __init__(self, text=""):
        self.patterns = []                  # (exclusion, cleaned pattern, compiled regular expression)
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            exclusion = line.startswith("!")
            if exclusion:
                line = line[1:].strip()
            line = posixpath.normpath(line).lstrip("/")
            if line and line != ".":
                self.patterns.append((exclusion, line, re.compile(self.regex(line))))

    @staticmethod
    def regex(pattern):
        out, i = "^", 0
        while i < len(pattern):
            ch = pattern[i]
            i += 1
            if ch == "*":
                if pattern[i:i + 1] == "*":
                    i += 1
                    if pattern[i:i + 1] == "/":
                        i += 1
                    out += ".*" if i >= len(pattern) else "(.*/)?"
                else:
                    out += "[^/]*"
            elif ch == "?":
                out += "[^/]"
            elif ch in ".$()+{}|^":
                out += "\\" + ch
            elif ch == "\\" and i < len(pattern):
                out += "\\" + pattern[i]
                i += 1
            else:
                out += ch
        return out + "$"

    def excluded(self, path):
        """True when the context path (relative, with /) is not part of the build context."""
        matched = False
        parents = path.split("/")[:-1]
        for exclusion, _, regex in self.patterns:
            if exclusion != matched:
                continue
            if regex.match(path) or any(regex.match("/".join(parents[:i + 1])) for i in range(len(parents))):
                matched = not exclusion
        return matched

    def may_contain_included(self, directory):
        """False when nothing below this folder can be in the context (so that .git's objects are not walked): the folder itself is not left out, or a `!` pattern brings back something in it."""
        if not self.excluded(directory):
            return True
        return any(exclusion and (text.startswith(directory + "/") or any(c in text for c in "*?[")) for exclusion, text, _ in self.patterns)


def expand(base, pattern):
    """The paths (relative to base, with /) that a COPY source names: a path, or a pattern whose * ? [ ] match inside one name (dot files too, as Docker's)."""
    found = [""]
    for part in pattern.split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            raise ToolError("COPY source %s leaves the folder" % pattern)
        following = []
        for prefix in found:
            folder = os.path.join(base, prefix) if prefix else base
            if any(c in part for c in "*?["):
                try:
                    names = sorted(os.listdir(folder))
                except OSError:
                    continue
                following += [posixpath.join(prefix, n) for n in names if fnmatch.fnmatchcase(n, part)]
            elif os.path.lexists(os.path.join(folder, part)):
                following.append(posixpath.join(prefix, part))
        found = following
    return [p for p in found if p]


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- the replay

class Replay:
    """The steps of a Dockerfile, done on this machine. The context is the folder that `docker build` would send (minus .dockerignore); /src, /usr/share/nginx/html and /etc/nginx are
    WORK/fs/src and so on, in the paths of COPY and in the text of RUN; env is what RUN sees besides the ARG and ENV of the Dockerfile."""

    def __init__(self, steps, context, fs, env, build_args=None, output=None):
        self.steps, self.context, self.fs, self.env, self.output = steps, context, fs, env, output      # (output: where a RUN's own output goes: this one's by default)
        self.build_args = dict(build_args or {})
        ignore = os.path.join(context, ".dockerignore")
        self.ignore = DockerIgnore(read_text(ignore) if os.path.isfile(ignore) else "")
        self.stages = {}
        self.declared = set()
        self.workdir, self.args, self.envs = "/", {}, {}

    def virtual_ok(self, virtual):
        return any(virtual == root or virtual.startswith(root + "/") for root in VIRTUAL_ROOTS)

    def real(self, virtual):
        virtual = posixpath.normpath(virtual)
        if not self.virtual_ok(virtual):
            raise ToolError("%s is outside the folders that this tool maps (%s): add the folder to VIRTUAL_ROOTS" % (virtual, ", ".join(VIRTUAL_ROOTS)))
        return os.path.join(self.fs, virtual.lstrip("/"))

    def substitute(self, text):
        values = dict(self.args)
        values.update(self.envs)
        return re.sub(r"\$\{(\w+)\}|\$(\w+)", lambda m: values.get(m.group(1) or m.group(2), ""), text)

    def remap(self, command):
        pattern = r"(?<![\w.$/\\-])(" + "|".join(re.escape(r) for r in VIRTUAL_ROOTS) + r")(?=[/\s\"'`;&|)]|$)"
        return re.sub(pattern, lambda m: os.path.join(self.fs, m.group(1).lstrip("/")), command)

    def unmapped(self, command):
        """Absolute paths of a RUN (as the Dockerfile has it) that this tool does not move: a command that writes there writes to this machine, so it is an error."""
        found = re.findall(r"(?<![^\s\"'=(:;|&`])(/[\w.@%+-]+(?:/[\w.@%+*-]+)*)", command)
        return [p for p in found if p not in HARMLESS_PATHS and not self.virtual_ok(p)]

    def play(self):
        for folder in BASE_FOLDERS:
            os.makedirs(self.real(folder), exist_ok=True)
        for step in self.steps:
            getattr(self, "do_" + step.op.lower())(step)
        for name in sorted(set(self.build_args) - self.declared):
            say("warning: --build-arg %s is not an ARG of the Dockerfile" % name)

    def do_from(self, step):
        name = step.words[2] if len(step.words) >= 3 and step.words[1].upper() == "AS" else str(step.stage)
        self.stages[name] = self.stages[str(step.stage)] = step.stage
        self.workdir, self.args, self.envs = "/", {}, {}
        say("FROM %s (stage %s)" % (step.words[0], name))

    @staticmethod
    def shell_words(step):
        """The words of an ARG or ENV with their quotes and backslashes taken out, as Docker reads them: `ARG NAME=""` has an empty value and `ENV NAME="a b"` is one word."""
        try:
            return shlex.split(step.text)
        except ValueError as e:
            raise ToolError("Dockerfile line %d: %s %s: %s" % (step.line, step.op, step.text, e))

    def do_arg(self, step):
        for word in self.shell_words(step):
            name, has_value, value = word.partition("=")
            self.declared.add(name)
            self.args[name] = self.build_args.get(name, value if has_value else "")

    def do_env(self, step):
        words = self.shell_words(step)
        if words and "=" not in words[0]:
            self.envs[words[0]] = self.substitute(" ".join(words[1:]))
            return
        for word in words:
            name, _, value = word.partition("=")
            self.envs[name] = self.substitute(value)

    def do_workdir(self, step):
        self.workdir = posixpath.normpath(posixpath.join(self.workdir, self.substitute(step.text.strip())))
        os.makedirs(self.real(self.workdir), exist_ok=True)

    def do_run(self, step):
        outside = self.unmapped(step.text)
        if outside:
            raise ToolError("Dockerfile line %d: RUN uses %s, which this tool does not move (the command would run on this machine's folder): add the folder to VIRTUAL_ROOTS or the path to HARMLESS_PATHS, with a test, or build with Docker" % (step.line, outside[0]))
        command = self.remap(step.text)
        say("RUN " + re.sub(r"\s+", " ", command)[:110] + (" ..." if len(command) > 110 else ""))
        env = dict(self.env)
        env.update(self.args)
        env.update(self.envs)
        cwd = self.real(self.workdir) if self.workdir != "/" else self.fs
        os.makedirs(cwd, exist_ok=True)
        done = subprocess.run([shutil.which("sh") or "/bin/sh", "-c", command], cwd=cwd, env=env, stdout=self.output, stderr=self.output)
        if done.returncode != 0:
            raise ToolError("Dockerfile line %d: RUN failed with status %d" % (step.line, done.returncode))

    def in_context(self, path):
        """Whether a context path that COPY names is there: a file that the .dockerignore does not leave out, a folder that has something that it does not leave out."""
        if os.path.isdir(os.path.join(self.context, path)):
            return self.ignore.may_contain_included(path)
        return not self.ignore.excluded(path)

    def do_copy(self, step):
        words = [self.substitute(w) for w in step.words]
        if len(words) < 2:
            raise ToolError("Dockerfile line %d: COPY needs a source and a destination" % step.line)
        sources, dest = words[:-1], words[-1]
        for flag in step.flags:
            if flag != "from":
                raise ToolError("Dockerfile line %d: COPY --%s is not supported by this tool (it would be left out): add it, with a test, or build with Docker" % (step.line, flag))
        origin = step.flags.get("from")
        if origin is not None and origin not in self.stages:
            raise ToolError("Dockerfile line %d: COPY --from=%s is not an earlier stage (an image is not supported)" % (step.line, origin))
        dest_virtual = dest if posixpath.isabs(dest) else posixpath.join(self.workdir, dest)
        found = []                                      # (a source's path, the folder that it is relative to)
        for source in sources:
            if origin is None:
                base, matches = self.context, [m for m in expand(self.context, source) if self.in_context(m)]
            else:
                virtual = source if posixpath.isabs(source) else "/" + source
                if not self.virtual_ok(posixpath.normpath(virtual)):
                    raise ToolError("Dockerfile line %d: COPY --from=%s %s is outside the folders that this tool maps" % (step.line, origin, source))
                base, matches = self.fs, expand(self.fs, virtual.lstrip("/"))
            if not matches:
                raise ToolError("Dockerfile line %d: COPY %s: nothing of it is in %s" % (step.line, source, "the build context (or .dockerignore leaves it out)" if origin is None else "the stage"))
            found += [(m, base) for m in matches]
        target = self.real(dest_virtual)
        into_folder = dest.endswith("/") or len(found) > 1 or os.path.isdir(target)
        for path, base in found:
            source = os.path.join(base, path)
            if os.path.isdir(source) and not os.path.islink(source):
                self.copy_folder(path, base, target, origin is None)
                continue
            where = os.path.join(target, posixpath.basename(path)) if into_folder else target
            os.makedirs(os.path.dirname(where), exist_ok=True)
            self.copy_file(source, where)

    @staticmethod
    def copy_file(source, where):
        if os.path.lexists(where):
            os.remove(where)
        if os.path.islink(source):
            os.symlink(os.readlink(source), where)
        else:
            shutil.copy2(source, where)

    def copy_folder(self, path, base, target, filtered):
        """The content of a folder into target (as COPY of a folder does), leaving out what the .dockerignore leaves out of the context."""
        os.makedirs(target, exist_ok=True)
        top = os.path.join(base, path)
        for root, folders, files in os.walk(top):
            inside = os.path.relpath(root, top).replace(os.sep, "/")
            relative = path if inside == "." else posixpath.join(path, inside)
            there = target if inside == "." else os.path.join(target, inside)
            keep = []
            for name in sorted(folders):
                if os.path.islink(os.path.join(root, name)):
                    files.append(name)
                elif not filtered or self.ignore.may_contain_included(posixpath.join(relative, name)):
                    keep.append(name)
                    os.makedirs(os.path.join(there, name), exist_ok=True)
            folders[:] = keep
            for name in sorted(files):
                if not filtered or not self.ignore.excluded(posixpath.join(relative, name)):
                    self.copy_file(os.path.join(root, name), os.path.join(there, name))


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------- Emscripten

def installed_emscripten_version(emsdk):
    try:
        return read_text(os.path.join(emsdk, "upstream", "emscripten", "emscripten-version.txt")).strip().strip('"')
    except OSError:
        return None


def emsdk_complete(emsdk):
    """Whether `emsdk install` and `emsdk activate` ran to the end: Emscripten's version file is unpacked early, `upstream/.emsdk_version` is the last file of the install and `.emscripten` is
    what activate writes. An emsdk that a cut-short run left half way has the first only."""
    return all(os.path.isfile(os.path.join(emsdk, *path)) for path in (("upstream", "emscripten", "emscripten-version.txt"), ("upstream", ".emsdk_version"), (".emscripten",)))


def ensure_emsdk(version, emsdk):
    have = installed_emscripten_version(emsdk)
    if have == version and emsdk_complete(emsdk):
        return
    if have is not None and have != version:
        raise ToolError("%s holds Emscripten %s and the Dockerfile wants %s: give --emsdk another folder (this tool does not change an emsdk that it did not make)" % (emsdk, have, version))
    if not os.path.exists(os.path.join(emsdk, "emsdk")):
        if os.path.isdir(emsdk) and os.listdir(emsdk):
            raise ToolError("%s is not empty and holds no emsdk: give --emsdk another folder" % emsdk)
        say("getting emsdk %s" % version)
        os.makedirs(os.path.dirname(emsdk), exist_ok=True)
        run(["git", "-c", "advice.detachedHead=false", "clone", "--quiet", "--depth", "1", "--branch", version, EMSDK_URL, emsdk])
    say("installing Emscripten %s (a few minutes the first time)" % version)
    run([os.path.join(emsdk, "emsdk"), "install", version])
    run([os.path.join(emsdk, "emsdk"), "activate", version])


def emsdk_environment(emsdk):
    """This environment plus what `source emsdk_env.sh` makes (PATH with emcc and its node, EMSDK, ...)."""
    if not os.path.isfile(os.path.join(emsdk, "emsdk_env.sh")):
        raise ToolError("%s has no emsdk_env.sh: it is no emsdk" % emsdk)
    done = subprocess.run(["bash", "-c", 'source "$1/emsdk_env.sh" > /dev/null 2>&1; env -0', "bash", emsdk], capture_output=True)
    if done.returncode != 0:
        raise ToolError("emsdk_env.sh of %s did not run" % emsdk)
    env = dict(os.environ)
    for item in done.stdout.decode("utf-8", "replace").split("\0"):
        name, has_value, value = item.partition("=")
        if has_value and name:
            env[name] = value
    if not shutil.which("emcc", path=env.get("PATH")):
        raise ToolError("emsdk_env.sh of %s puts no emcc on the PATH: its install is not complete (run its `emsdk install` and `emsdk activate`, or remove the folder and run this again)" % emsdk)
    return env


def port_file(emsdk, name):
    return os.path.join(emsdk, "upstream", "emscripten", "tools", "ports", name + ".py")


def port_info(emsdk, name):
    """(the address Emscripten downloads the port from, the ports that it needs) from the port's file in the installed Emscripten."""
    try:
        text = read_text(port_file(emsdk, name))
    except OSError:
        raise ToolError("Emscripten has no port %s (%s)" % (name, port_file(emsdk, name)))
    constants = dict(re.findall(r"^(TAG|VERSION)\s*=\s*'([^']*)'", text, re.M))
    match = re.search(r"fetch_project\(\s*'%s'\s*,\s*f?'([^']+)'" % re.escape(name), text)
    if not match:
        raise ToolError("the port file of %s has no fetch_project('%s', 'address') that this tool can read" % (name, name))
    try:
        url = match.group(1).format(**constants)
    except KeyError as e:
        raise ToolError("the address of port %s uses %s, which this tool does not read" % (name, e))
    deps = re.search(r"^deps\s*=\s*\[([^\]]*)\]", text, re.M)
    return url, re.findall(r"'(\w+)'", deps.group(1)) if deps else []


def ports_to_get(context, emsdk):
    """[(port, address)] of every port that the context's CMake files ask Emscripten for (-sUSE_SDL=2 ...) and of what those need."""
    texts = []
    for path in ("CMakeLists.txt", os.path.join("src", "ants_app", "CMakeLists.txt")):
        try:
            texts.append(read_text(os.path.join(context, path)))
        except OSError:
            pass
    queue = []
    for flag, value in sorted(set(re.findall(r"-sUSE_([A-Z0-9_]+)=(\d+)", "\n".join(texts)))):
        port = PORTS_OF_FLAGS.get(("USE_" + flag, value))
        if port:
            queue.append(port)
        else:
            say("warning: CMake asks for -sUSE_%s=%s, which this tool has no port for: Emscripten downloads it itself" % (flag, value))
    found = {}
    while queue:
        name = queue.pop(0)
        if name not in found:
            found[name], needs = port_info(emsdk, name)
            queue += needs
    return sorted(found.items())


def source_of(url):
    """How to get what Emscripten would download from url without a GitHub archive: ("git", repository, tag, folder of the archive) or ("mirror", address of Emscripten's copy)."""
    archive = re.fullmatch(r"https://github\.com/([\w.-]+)/([\w.-]+)/archive/([\w.-]+)\.zip", url)
    if archive:
        owner, repository, tag = archive.groups()
        return "git", "https://github.com/%s/%s.git" % (owner, repository), tag, "%s-%s" % (repository, tag)
    release = re.fullmatch(r"https://github\.com/[\w.-]+/[\w.-]+/releases/download/[\w.-]+/([\w.-]+)\.tar\.xz", url)
    if release:
        return "mirror", PORT_MIRROR + release.group(1) + ".tar.gz"
    raise ToolError("no way to get %s without Emscripten's own download: add one to source_of() in tools/web_without_docker.py" % url)


def extract(data, destination, mode):
    with tarfile.open(fileobj=io.BytesIO(data), mode=mode) as tar:
        if hasattr(tarfile, "data_filter"):
            tar.extractall(destination, filter="data")
        else:
            tar.extractall(destination)


def fetch_port(name, url, ports):
    """Puts a port where Emscripten looks before it downloads: ports/NAME/<folder>, and the marker .emscripten_url with the address (Emscripten takes a port as it is when the marker names
    its address). False when it was there."""
    target = os.path.join(ports, name)
    marker = os.path.join(target, ".emscripten_url")
    try:
        if read_text(marker).strip() == url:
            return False
    except OSError:
        pass
    source = source_of(url)
    shutil.rmtree(target, ignore_errors=True)
    os.makedirs(target)
    if source[0] == "git":
        _, repository, tag, folder = source
        with tempfile.TemporaryDirectory() as scratch:
            clone = os.path.join(scratch, "clone")
            run(["git", "-c", "advice.detachedHead=false", "clone", "--quiet", "--depth", "1", "--branch", tag, repository, clone], env=dict(os.environ, GIT_TERMINAL_PROMPT="0"))
            tar = subprocess.run(["git", "-C", clone, "archive", "--format=tar", "--prefix=%s/" % folder, "HEAD"], capture_output=True)
            if tar.returncode != 0:
                raise ToolError("git archive of %s failed" % repository)
            extract(tar.stdout, target, "r:")
    else:
        try:
            with urllib.request.urlopen(source[1], timeout=120) as answer:
                data = answer.read()
            extract(data, target, "r:gz")
        except (OSError, http.client.HTTPException, tarfile.TarError) as e:       # (an answer that is cut off, or is no archive)
            raise ToolError("%s: %s" % (source[1], e))
    with open(marker, "w", encoding="utf-8") as f:
        f.write(url + "\n")
    return True


def prepare_ports(context, emsdk, env):
    ports = os.path.join(env.get("EM_CACHE") or os.path.join(emsdk, "upstream", "emscripten", "cache"), "ports")
    os.makedirs(ports, exist_ok=True)
    for name, url in ports_to_get(context, emsdk):
        if fetch_port(name, url, ports):
            say("port %s: put in Emscripten's cache from %s" % (name, "git" if source_of(url)[0] == "git" else "Emscripten's mirror"))
        else:
            say("port %s: already in Emscripten's cache" % name)


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- nginx

def site_config(conf, fs, cache, port):
    """docker/nginx.conf for a machine without the image's folders: its paths under fs, the cache folder, and `listen` on 127.0.0.1:PORT."""
    html, stats = os.path.join(fs, "usr", "share", "nginx", "html"), os.path.join(cache, "ants_stats")
    text = conf.replace("/usr/share/nginx/html", html).replace("/var/cache/nginx/ants_stats", stats)
    text, moved = re.subn(r"^([ \t]*)listen[ \t]+80;[ \t]*$", r"\1listen 127.0.0.1:%d;" % port, text, flags=re.M)
    text = re.sub(r"^[ \t]*listen[ \t]+\[::\]:80;[ \t]*\n", "", text, flags=re.M)
    if moved != 1:
        raise ToolError("docker/nginx.conf has not exactly one `listen 80;` line: the tool cannot move it to the port")
    left = re.search(r"/(?:usr/share/nginx|var/cache/nginx|var/log/nginx|var/run)\S*", re.sub(r"#.*", "", text).replace(html, "").replace(stats, ""))
    if left:
        raise ToolError("docker/nginx.conf uses %s, which this tool does not move: add it to site_config()" % left.group(0))
    return text


def mime_types():
    for path in ("/etc/nginx/mime.types", "/usr/local/etc/nginx/mime.types", "/opt/homebrew/etc/nginx/mime.types", "/usr/local/nginx/conf/mime.types"):
        if os.path.isfile(path):
            return path
    raise ToolError("nginx's mime.types is not in the usual places (is nginx installed? sudo apt-get update && sudo apt-get install -y nginx)", 3)


def main_config(prefix, site, mime, as_root):
    """The nginx.conf around the site's file (the stock file of the nginx image: it includes conf.d/*.conf), with every path that nginx writes to under prefix."""
    return "\n".join([
        "user root;" if as_root else "",
        "worker_processes 1;",
        "pid %s;" % os.path.join(prefix, "nginx.pid"),
        "error_log %s notice;" % os.path.join(prefix, "error.log"),
        "events { worker_connections 256; }",
        "http {",
        "    include %s;" % mime,
        "    default_type application/octet-stream;",
        "    access_log %s;" % os.path.join(prefix, "access.log"),
        "    sendfile on;",
    ] + ["    %s_temp_path %s;" % (kind, os.path.join(prefix, "tmp", kind)) for kind in ("client_body", "proxy", "fastcgi", "uwsgi", "scgi")] + [
        "    include %s;" % site,
        "}",
        ""])


def takes_error_log_switch(nginx):
    """Whether `nginx -e FILE` (the error log until the configuration is read) exists: nginx 1.19.5 and later have it, the 1.18 of Ubuntu 22.04 and Debian 11 calls it an invalid option."""
    try:
        done = subprocess.run([nginx, "-h"], capture_output=True, text=True, errors="replace")
    except OSError:
        return False
    return re.search(r"^\s*-e\s+filename", done.stdout + done.stderr, re.M) is not None


class Nginx:
    def __init__(self, work, port):
        self.work = work
        self.prefix = os.path.join(work, "nginx")
        self.conf = os.path.join(self.prefix, "nginx.conf")
        self.site = os.path.join(self.prefix, "site.conf")
        self.pid_file = os.path.join(self.prefix, "nginx.pid")
        self.port = port
        self.error_log_switch = None        # whether this nginx has -e: asked with its first command

    def command(self, *extra):
        nginx = shutil.which("nginx") or "nginx"
        if self.error_log_switch is None:
            self.error_log_switch = takes_error_log_switch(nginx)
        log = ["-e", os.path.join(self.prefix, "error.log")] if self.error_log_switch else []
        return [nginx, "-p", self.prefix + "/"] + log + ["-c", self.conf] + list(extra)

    def configure(self):
        fs = os.path.join(self.work, "fs")
        for sub in ("cache/ants_stats", "tmp/client_body", "tmp/proxy", "tmp/fastcgi", "tmp/uwsgi", "tmp/scgi"):
            os.makedirs(os.path.join(self.prefix, sub), exist_ok=True)
        with open(self.site, "w", encoding="utf-8") as f:
            f.write(site_config(read_text(os.path.join(fs, "etc", "nginx", "conf.d", "default.conf")), fs, os.path.join(self.prefix, "cache"), self.port))
        with open(self.conf, "w", encoding="utf-8") as f:
            f.write(main_config(self.prefix, self.site, mime_types(), hasattr(os, "geteuid") and os.geteuid() == 0))

    def running(self):
        """Whether the master process of this nginx is alive. A pid file can outlive its process (a killed run, a restart of the machine) and its number then belongs to some other process, so
        the process has to hold this configuration file in its command line, as nginx's master process does (/proc: Linux)."""
        try:
            pid = int(read_text(self.pid_file).strip())
            with open("/proc/%d/cmdline" % pid, "rb") as f:
                return os.fsencode(self.conf) in f.read()
        except (OSError, ValueError):
            return False

    def start(self):
        if self.running():
            self.stop()
        self.configure()
        tested = subprocess.run(self.command("-t"), capture_output=True, text=True)
        if tested.returncode != 0:
            raise ToolError("nginx does not accept its configuration:\n" + tested.stderr)
        run(self.command())
        url = "http://127.0.0.1:%d/" % self.port
        last = "no answer"
        try:
            deadline = time.monotonic() + START_SECONDS
            while time.monotonic() < deadline:
                try:
                    with urllib.request.urlopen(url + "lobby.html", timeout=2) as answer:
                        if answer.status == 200:
                            return url
                        last = "HTTP %d" % answer.status
                except urllib.error.HTTPError as e:
                    last = "HTTP %d" % e.code
                except (urllib.error.URLError, OSError) as e:
                    last = str(e)
                time.sleep(0.1)
            raise ToolError("nginx started and does not answer at %slobby.html (last try: %s; see %s)" % (url, last, os.path.join(self.prefix, "error.log")))
        except BaseException:               # (a failure, Ctrl-C, SIGTERM: nginx has daemonized and would keep the port)
            self.stop()
            raise

    def stop(self):
        if self.running():
            subprocess.run(self.command("-s", "stop"), capture_output=True)
            for _ in range(50):
                if not self.running():
                    break
                time.sleep(0.1)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ the rest

def find_chromium():
    """A browser for the checks: Playwright's headless shell first (Chromium's new headless mode stops giving a page the mouse once the page has left fullscreen, which fails the aspect
    check's fullscreen part), then a Chromium or a Chrome."""
    base = os.environ.get("PLAYWRIGHT_BROWSERS_PATH") or "/opt/pw-browsers"
    entries = sorted(os.listdir(base), reverse=True) if os.path.isdir(base) else []
    for prefix, name in (("chromium_headless_shell-", "headless_shell"), ("chromium-", "chrome")):
        for entry in entries:
            path = os.path.join(base, entry, "chrome-linux", name)
            if entry.startswith(prefix) and os.access(path, os.X_OK):
                return path
    for name in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge"):
        if shutil.which(name):
            return shutil.which(name)
    return None


def browser_environment(work, env):
    """CHROME for the checks, when it is not set: the browser that find_chromium() finds, through a script that gives it a desktop's mouse (a headless browser on a machine with no mouse has no
    pointer that hovers, and the page hides what is for a mouse) and, as root, --no-sandbox (Chromium does not start as root without it)."""
    if env.get("CHROME"):
        return env
    browser = find_chromium()
    if browser is None:
        return env
    flags = ["--disable-dev-shm-usage", MOUSE] + (["--no-sandbox"] if hasattr(os, "geteuid") and os.geteuid() == 0 else [])
    wrapper = os.path.join(work, "chrome-for-the-checks")
    os.makedirs(work, exist_ok=True)
    with open(wrapper, "w", encoding="utf-8") as f:
        f.write("#!/bin/sh\nexec '%s' %s \"$@\"\n" % (browser, " ".join(flags)))
    os.chmod(wrapper, 0o755)
    return dict(env, CHROME=wrapper)


def built_marker(work):
    """The file that the last step of a build writes: a tree that a cut-short run left half way has none, and --reuse does not take it."""
    return os.path.join(work, "built")


def build(context, work, emsdk, build_args):
    for tool in ("git", "bash", "cmake", "make", "python3"):
        if not shutil.which(tool):
            raise ToolError("%s is not installed (sudo apt-get update && sudo apt-get install -y %s)" % (tool, tool), 3)
    mime_types()                            # (nginx's file is looked for now, not after minutes of build)
    steps = parse_dockerfile(read_text(os.path.join(context, "Dockerfile")))
    version = emscripten_version(steps)
    if not emsdk:
        home = os.environ.get("EMSDK")
        emsdk = home if home and installed_emscripten_version(home) == version else os.path.join(os.path.expanduser("~"), ".cache", "ants-web", "emsdk-" + version)
    ensure_emsdk(version, os.path.abspath(emsdk))
    env = emsdk_environment(os.path.abspath(emsdk))
    prepare_ports(context, os.path.abspath(emsdk), env)
    fs = os.path.join(work, "fs")
    if os.path.lexists(built_marker(work)):
        os.remove(built_marker(work))
    shutil.rmtree(fs, ignore_errors=True)
    os.makedirs(fs)
    Replay(steps, context, fs, env, build_args).play()
    html = os.path.join(fs, "usr", "share", "nginx", "html")
    if not os.path.isfile(os.path.join(html, "index.wasm")):
        raise ToolError("the replay ended without %s" % os.path.join(html, "index.wasm"))
    with open(built_marker(work), "w", encoding="utf-8") as f:
        f.write("built from %s\n" % context)
    say("built: %s" % html)


def main(argv):
    arguments = list(argv[1:])
    command = []
    if "--" in arguments:
        cut = arguments.index("--")
        arguments, command = arguments[:cut], arguments[cut + 1:]
    parser = argparse.ArgumentParser(description="The web image without Docker (see the top of this file).", usage="%(prog)s [options] [-- COMMAND [ARG ...]]")
    parser.add_argument("--context", default=REPO, help="the tree to build (default: the tree of this script)")
    parser.add_argument("--work", help="where everything goes (default: CONTEXT/scratch/web_without_docker)")
    parser.add_argument("--emsdk", help="the emsdk folder (default: $EMSDK when it has the right version, else ~/.cache/ants-web/emsdk-VERSION)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="nginx's port, 0 for a free one (default: %(default)s)")
    parser.add_argument("--build-arg", action="append", default=[], metavar="NAME=VALUE", help="a value for an ARG of the Dockerfile")
    parser.add_argument("--reuse", action="store_true", help="serve what an earlier run built")
    parser.add_argument("--keep", action="store_true", help="leave nginx running")
    parser.add_argument("--stop", action="store_true", help="stop the nginx of --work and end")
    args = parser.parse_args(arguments)
    build_args = {}
    for item in args.build_arg:
        name, has_value, value = item.partition("=")
        if not has_value or not name:
            parser.error("--build-arg wants NAME=VALUE: %s" % item)
        build_args[name] = value
    if os.name == "nt" or sys.platform == "darwin":
        print("web_without_docker: this runs on Linux (the Dockerfile's RUN lines use GNU tools); use `docker build` here", file=sys.stderr)
        return 3
    context = os.path.abspath(args.context)
    work = os.path.abspath(args.work or os.path.join(context, "scratch", "web_without_docker"))
    if shlex.quote(work) != work:
        print("web_without_docker: the work folder %s has a character that a shell reads apart (a space, a quote, $ ...) and the RUN lines of the Dockerfile get its path as it is: give --work a plain path" % work,
              file=sys.stderr)
        return 3
    try:
        nginx = Nginx(work, args.port if args.port else free_port())
        if args.stop:
            nginx.stop()
            return 0
        if not shutil.which("nginx"):
            raise ToolError("nginx is not installed (sudo apt-get update && sudo apt-get install -y nginx)", 3)
        if not args.reuse:
            build(context, work, args.emsdk, build_args)
        elif not os.path.isfile(built_marker(work)):
            raise ToolError("--reuse: nothing was built in %s (or a build there was cut short)" % work)
        url = nginx.start()
        say("serving %s (nginx's logs are in %s)" % (url, nginx.prefix))
        status = 0
        try:
            if command:
                try:
                    status = subprocess.run(command, env=browser_environment(work, dict(os.environ, ANTS_WEB_URL=url))).returncode
                except OSError as e:
                    raise ToolError("cannot run %s: %s" % (command[0], e), 2)
            elif not args.keep:
                say("nothing to run: give a command after -- (or --keep to leave the server running)")
        finally:
            if not args.keep:
                nginx.stop()
        return status
    except ToolError as e:
        print("web_without_docker: %s" % e, file=sys.stderr)
        return e.status
    except (OSError, http.client.HTTPException, tarfile.TarError) as e:         # (a missing file, a full disk, a download that is cut off: a message, not a traceback)
        print("web_without_docker: %s" % e, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    sys.exit(main(sys.argv))
