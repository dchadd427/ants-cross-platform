#!/usr/bin/env python3
"""The files of a Docker image, and the difference between two images (the helper of tools/image_files_diff.sh).

usage: image_manifest.py manifest < image.tar > manifest.json        the tar is `docker export CONTAINER`; the manifest lists every entry with its type, mode, owner, size, link target and sha256
       image_manifest.py config < inspect.json > config.json         the part of `docker image inspect IMAGE` that decides how the image runs (user, entrypoint, command, ports, environment, ...)
       image_manifest.py compare BASE NEW [--label TEXT]             two manifests (or two configs): lists every difference, exit status 1 when there is one, 0 when there is none, 2 when a file cannot be read

Two kinds of file are not compared byte for byte, and only these (the lists are STAMPED and LOOSE below):
  - the pages that carry the build time and the build date: the stamp is replaced by a fixed text before the hash, and the number of replacements is part of the manifest, so a file that gains or
    loses a stamp differs (the build id is the same in both builds: the script passes the same ANTS_BUILD_ID);
  - the WebAssembly programs that Emscripten links (index.wasm, lvl_core.wasm) and their JavaScript: two builds of the SAME Dockerfile do not give the same bytes (the order of the functions, and so
    their numbers in the JavaScript, differs from build to build: CI showed it by building the old Dockerfile twice). Of these only a signature is compared: for a program, the bytes of its data section
    (every string and table that the program holds, the version text included), the list of its sections and its size within 64 bytes; for the JavaScript, the text with every run of digits made one.
    A change of what the program says or holds is found; a change of the code's own order is not, and no test can tell it from the noise.
Modification times are not compared (a COPY keeps the time of the file in the repository, a generated file has the time of its build); everything else is, byte for byte.
"""
import gzip
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
LOOSE = {                                                                                  # file -> how its signature is made (see the top of this file)
    HTML + "index.js": "js", HTML + "index.js.gz": "js.gz", HTML + "index.wasm": "wasm", HTML + "index.wasm.gz": "wasm.gz",
    HTML + "lvl_core.js": "js", HTML + "lvl_core.wasm": "wasm",
}
SIZE_TOLERANCE = 64                     # bytes by which the size of a loose program may differ (the numbers of functions are LEB128 numbers of a changing length)
CONFIG_KEYS = ("User", "Entrypoint", "Cmd", "ExposedPorts", "Env", "WorkingDir", "Healthcheck", "Volumes", "StopSignal", "Shell", "OnBuild")
MINIMUM_ENTRIES = 20                    # an image with fewer entries than this is an empty export, not an image: the comparison would pass for nothing


def kind(member):
    for name, test in (("file", member.isreg), ("dir", member.isdir), ("symlink", member.issym), ("hardlink", member.islnk), ("char", member.ischr), ("block", member.isblk), ("fifo", member.isfifo)):
        if test():
            return name
    return "other"


def leb(data, at):
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, at


def wasm_signature(data):
    """(sha256 of the data section, the ids of the sections in order) of a WebAssembly file; ValueError for a file that is none."""
    if data[:4] != b"\0asm":
        raise ValueError("not a WebAssembly file")
    at, ids, payload = 8, [], b""
    while at < len(data):
        section = data[at]
        size, at = leb(data, at + 1)
        ids.append(section)
        if section == 11:
            payload = data[at:at + size]
        at += size
    return hashlib.sha256(payload).hexdigest(), ids


def loose_signature(kind, data):
    """The text that stands for a loose file: (signature, size to compare within SIZE_TOLERANCE)."""
    if kind.endswith(".gz"):
        data, kind = gzip.decompress(data), kind[:-3]
    if kind == "wasm":
        try:
            digest, ids = wasm_signature(data)
        except (ValueError, IndexError):
            return "unreadable:" + hashlib.sha256(data).hexdigest(), len(data)
        return "data:%s sections:%s" % (digest, ",".join(map(str, ids))), len(data)
    return hashlib.sha256(re.sub(rb"\d+", b"#", data)).hexdigest(), len(data)


def manifest(stream):
    """{path: {...}} of every entry of a tar stream."""
    entries = {}
    with tarfile.open(fileobj=stream, mode="r|*") as tar:
        for member in tar:
            path = member.name.lstrip("./").rstrip("/") or "."
            entry = {"type": kind(member), "mode": "%o" % (member.mode & 0o7777), "uid": member.uid, "gid": member.gid}
            if entry["type"] == "file" and path in LOOSE:
                signature, size = loose_signature(LOOSE[path], tar.extractfile(member).read())
                entry.update(signature=signature, approx_size=size)
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
                    if key == "approx_size" and abs(base[path].get(key, 0) - new[path].get(key, 0)) <= SIZE_TOLERANCE:
                        continue
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
