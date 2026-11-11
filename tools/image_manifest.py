#!/usr/bin/env python3
"""The files of a Docker image, and the difference between two images (the helper of tools/image_files_diff.sh).

usage: image_manifest.py manifest < image.tar > manifest.json        the tar is `docker export CONTAINER`; the manifest lists every entry with its type, mode, owner, size, link target and sha256
       image_manifest.py config < inspect.json > config.json         the part of `docker image inspect IMAGE` that decides how the image runs (user, entrypoint, command, ports, environment, ...)
       image_manifest.py compare BASE NEW [--label TEXT]             two manifests (or two configs): lists every difference, exit status 1 when there is one, 0 when there is none, 2 when a file cannot be read

Only what a build writes by its clock is left out of the sha256, and only in the files that STAMPED names: the build time stamps of the game page and the build date of the pages (the build id is the
same in both builds: the script passes the same ANTS_BUILD_ID). In those files the stamp is replaced by a fixed text before the hash, and the number of replacements is part of the manifest, so a
file that gains or loses a stamp differs. Modification times are not compared (a COPY keeps the time of the file in the repository, a generated file has the time of its build); everything else is.
"""
import hashlib
import json
import re
import sys
import tarfile

HTML = "usr/share/nginx/html/"
DATE = (r"\(\d{4}-\d{2}-\d{2} \d{2}:\d{2} PT\)", "(<build date>)")                 # the build date that the pages show, minute resolution, Pacific time
TIME = (r"(ANTS_BUILD_VER = ')\d+(')", r"\1<build time>\2")                              # the game page's build time (seconds since 1970), lockstep versioning of its files
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
            if entry["type"] == "file":
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
                    if base[path].get(key) != new[path].get(key):
                        found.append("%s%s differs in %s: %s -> %s" % (prefix, path, key, json.dumps(base[path].get(key)), json.dumps(new[path].get(key))))
            else:
                found.append("%s%s differs: %s -> %s" % (prefix, path, json.dumps(base[path]), json.dumps(new[path])))
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
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
