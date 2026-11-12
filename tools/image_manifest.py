#!/usr/bin/env python3
"""The files of a Docker image, and the difference between two images (the helper of tools/image_files_diff.sh).

usage: image_manifest.py manifest < image.tar > manifest.json        the tar is `docker export CONTAINER`; the manifest lists every entry with its type, mode, owner, size, link target and sha256
       image_manifest.py config < inspect.json > config.json         the part of `docker image inspect IMAGE` that decides how the image runs (user, entrypoint, command, ports, environment, ...)
       image_manifest.py compare BASE NEW [--label TEXT]             two manifests (or two configs): lists every difference, exit status 1 when there is one, 0 when there is none, 2 when a file cannot be read
       image_manifest.py inputs BASE NEW [--label TEXT]              two listings made by tools/link_inputs.sh (what the web build's link reads): the same, exit status 1 for a difference
       image_manifest.py relink BASE NEW [--label TEXT]              two listings made by tools/relink_outputs.sh (the programs that a deterministic link of those inputs makes)

Two kinds of file are not compared byte for byte, and only these (the lists are STAMPED and LINKED below):
  - the pages that carry the build time and the build date: the stamp is replaced by a fixed text before the hash, and the number of replacements is part of the manifest, so a file that gains or
    loses a stamp differs (the build id is the same in both builds: the script passes the same ANTS_BUILD_ID);
  - the WebAssembly programs that Emscripten links (index.wasm, lvl_core.wasm), their JavaScript and the compressed copies: two links of the SAME objects do not always give the same bytes (the linker
    and the optimizer depend on the address space layout and on thread timing: the order of the functions, and so their numbers in the JavaScript, differs from link to link). Of these the final image
    keeps only the type, the mode, the owner and a size within one percent. What they are made of is compared where it is exact: the link's inputs (every object, archive and library that the link reads,
    and the link command lines: `inputs`), taken from the builder stage of both builds, and the programs of a link made deterministic (`relink`).
Modification times are not compared (a COPY keeps the time of the file in the repository, a generated file has the time of its build); everything else is, byte for byte.
"""
import hashlib
import json
import re
import sys
import tarfile

HTML = "usr/share/nginx/html/"
DATE = (r"\(\d{4}-\d{2}-\d{2} \d{2}:\d{2} PT\)", "(<build date>)")                 # the build date that the pages show, minute resolution, Pacific time
TIME = (r"(ANTS_BUILD_VER\s*=\s*['\"]?)\d+", r"\1<build time>")                              # the game page's build time (seconds since 1970), lockstep versioning of its files
CACHE = (r"(index\.js\?v=)\d+", r"\1<build time>")                                       # the same time, as the cache buster of the game page's script
STAMPED = {                                                                               # file (in the image, no leading slash) -> the stamps that may differ between two builds
    HTML + "index.html": (TIME, CACHE, DATE),
    HTML + "play.html": (TIME, CACHE, DATE),
    HTML + "lobby.html": (DATE,),
    HTML + "matches.html": (DATE,),
    HTML + "match.html": (DATE,),
    HTML + "live.html": (DATE,),
    HTML + "mapmaker.html": (DATE,),
    HTML + "changelog.html": (DATE,),
}
LINKED = (HTML + "index.js", HTML + "index.js.gz", HTML + "index.wasm", HTML + "index.wasm.gz", HTML + "lvl_core.js", HTML + "lvl_core.wasm")      # the files that a link makes (and the gzip copies of two)
SIZE_PERCENT = 1                        # by how many percent the size of a linked file may differ (the numbers of functions are LEB128 numbers of a changing length)
CONFIG_KEYS = ("User", "Entrypoint", "Cmd", "ExposedPorts", "Env", "WorkingDir", "Healthcheck", "Volumes", "StopSignal", "Shell", "OnBuild")
MINIMUM_ENTRIES = 20                    # an image with fewer entries than this is an empty export, not an image: the comparison would pass for nothing


def kind(member):
    for name, test in (("file", member.isreg), ("dir", member.isdir), ("symlink", member.issym), ("hardlink", member.islnk), ("char", member.ischr), ("block", member.isblk), ("fifo", member.isfifo)):
        if test():
            return name
    return "other"


def manifest(stream):
    """{path: {...}} of every entry of a tar stream."""
    entries = {}
    with tarfile.open(fileobj=stream, mode="r|*") as tar:
        for member in tar:
            path = member.name.lstrip("./").rstrip("/") or "."
            entry = {"type": kind(member), "mode": "%o" % (member.mode & 0o7777), "uid": member.uid, "gid": member.gid}
            if entry["type"] == "file" and path in LINKED:
                entry["approx_size"] = len(tar.extractfile(member).read())
            elif entry["type"] == "file":
                data = tar.extractfile(member).read()
                stamps = 0
                for pattern, replacement in STAMPED.get(path, ()):
                    text, count = re.subn(pattern.encode("ascii"), replacement.encode("ascii"), data)
                    data, stamps = text, stamps + count
                entry.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
                if path in STAMPED:
                    entry["stamps"] = stamps
            elif entry["type"] in ("symlink", "hardlink"):
                entry["target"] = member.linkname
            elif entry["type"] in ("char", "block"):
                entry["device"] = [member.devmajor, member.devminor]
            entries[path] = entry
    return entries


def config(inspected):
    """The keys of CONFIG_KEYS of `docker image inspect` (a list of one image, or its Config)."""
    if isinstance(inspected, list):
        inspected = inspected[0]
    section = inspected.get("Config", inspected)
    return {key: section.get(key) for key in CONFIG_KEYS}


def differences(base, new, label=""):
    """[text] of what differs between two manifests or two configs of the same shape."""
    found = []
    prefix = label + ": " if label else ""
    for path in sorted(set(base) | set(new)):
        if path not in new:
            found.append("%sonly in the base image: %s" % (prefix, path))
        elif path not in base:
            found.append("%sonly in the new image: %s" % (prefix, path))
        elif base[path] != new[path]:
            if isinstance(base[path], dict) and isinstance(new[path], dict):
                for key in sorted(set(base[path]) | set(new[path])):
                    if key == "approx_size" and abs(base[path].get(key, 0) - new[path].get(key, 0)) * 100 <= SIZE_PERCENT * max(base[path].get(key, 0), 1):
                        continue
                    if base[path].get(key) != new[path].get(key):
                        found.append("%s%s differs in %s: %s -> %s" % (prefix, path, key, json.dumps(base[path].get(key)), json.dumps(new[path].get(key))))
            else:
                found.append("%s%s differs: %s -> %s" % (prefix, path, json.dumps(base[path]), json.dumps(new[path])))
    return found


HEX = re.compile(r"[0-9a-f]{64}$")
REQUIRED = {                            # what a listing must hold, or it is an empty one and a comparison of two of them would pass for nothing
    "inputs": {"objects": (".o", 40), "archives": (".a", 4), "command lines": ("link.txt", 2), "flags": ("flags.make", 2), "the version header": ("version.hpp", 1),
               "SDL2": ("libSDL2.a", 1), "SDL2_ttf": ("libSDL2_ttf.a", 1), "FreeType": ("libfreetype.a", 1), "HarfBuzz": ("libharfbuzz.a", 1)},
    "relink": {"the game's script": ("index.js", 1), "the game's program": ("index.wasm", 1), "the map core's script": ("lvl_core.js", 1), "the map core's program": ("lvl_core.wasm", 1)},
}


def listing(text, kind):
    """{(kind of line, path): sha256 or command line} of a listing ('file<TAB>path<TAB>sha256' and 'link<TAB>path<TAB>command line' lines); ValueError for a listing that is malformed or too small."""
    found = {}
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        parts = line.split("\t", 2)
        if len(parts) != 3 or parts[0] not in ("file", "link") or not parts[1]:
            raise ValueError("line %d is not a listing line: %r" % (number, line[:80]))
        if parts[0] == "file" and not HEX.match(parts[2]):
            raise ValueError("line %d has no sha256 for %s" % (number, parts[1]))
        if parts[0] == "link" and not parts[2].strip():
            raise ValueError("line %d has an empty command line for %s" % (number, parts[1]))
        found[(parts[0], parts[1])] = parts[2]
    for what, (ending, least) in REQUIRED[kind].items():
        if sum(1 for (line_kind, path) in found if line_kind == "file" and path.endswith(ending)) < least:
            raise ValueError("the listing has fewer than %d entries for %s (%s)" % (least, what, ending))
    if kind == "inputs" and sum(1 for (line_kind, path) in found if line_kind == "link") < 2:
        raise ValueError("the listing holds fewer than two link command lines")
    return found


def listing_differences(base, new, label=""):
    """[text] of what differs between two listings (made by `listing`)."""
    found = []
    prefix = label + ": " if label else ""
    for key in sorted(set(base) | set(new)):
        what, path = key
        if key not in new:
            found.append("%sonly in the base build: %s" % (prefix, path))
        elif key not in base:
            found.append("%sonly in the new build: %s" % (prefix, path))
        elif base[key] != new[key]:
            if what == "link":
                found.append("%s%s: the link command line differs:\n    base: %s\n    new:  %s" % (prefix, path, base[key], new[key]))
            else:
                found.append("%s%s differs: sha256 %s -> %s" % (prefix, path, base[key][:16], new[key][:16]))
    return found


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main(argv):
    if len(argv) >= 2 and argv[1] == "manifest" and len(argv) == 2:
        entries = manifest(sys.stdin.buffer)
        if len(entries) < MINIMUM_ENTRIES:
            print("image_manifest: the image export has only %d entries" % len(entries), file=sys.stderr)
            return 2
        json.dump(entries, sys.stdout, sort_keys=True, indent=0)
        return 0
    if len(argv) >= 2 and argv[1] == "config" and len(argv) == 2:
        json.dump(config(json.load(sys.stdin)), sys.stdout, sort_keys=True, indent=0)
        return 0
    if len(argv) >= 4 and argv[1] == "compare":
        label = argv[argv.index("--label") + 1] if "--label" in argv else ""
        try:
            base, new = load(argv[2]), load(argv[3])
        except (OSError, ValueError) as e:
            print("image_manifest: %s" % e, file=sys.stderr)
            return 2
        found = differences(base, new, label)
        for line in found:
            print(line)
        print("%s%d entries compared, %d difference(s)" % (label + ": " if label else "", len(set(base) | set(new)), len(found)))
        return 1 if found else 0
    if len(argv) >= 4 and argv[1] in ("inputs", "relink"):
        label = argv[argv.index("--label") + 1] if "--label" in argv else argv[1]
        try:
            with open(argv[2], encoding="utf-8") as f:
                base = listing(f.read(), argv[1])
            with open(argv[3], encoding="utf-8") as f:
                new = listing(f.read(), argv[1])
        except (OSError, ValueError) as e:
            print("image_manifest: %s: %s" % (label, e), file=sys.stderr)
            return 2
        found = listing_differences(base, new, label)
        for line in found:
            print(line)
        print("%s: %d entries compared, %d difference(s)" % (label, len(set(base) | set(new)), len(found)))
        return 1 if found else 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
