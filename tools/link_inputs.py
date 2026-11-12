#!/usr/bin/env python3
"""Lists what the link of the web image reads, for tools/image_files_diff.sh. It runs INSIDE the builder stage of a web image, read from its standard input:

    docker run --rm -i IMAGE python3 - < tools/link_inputs.py

One line per file, sorted by path, fields separated by a tab:
  file  PATH  SHA256   every object file, archive, response file (the lists of objects and libraries that the link command lines name: @...objects1.rsp, @...linkLibs.rsp), link command file and
                      compile flags file of the CMake build folder and every generated file, by the sha256 of the file; and every library archive and every object file (crt1.o, crt1_reactor.o: the
                      start-up code of the two programs) in Emscripten's cache (system libraries and ports) by the sha256 of its members, or of the object, with the debug information left out
                      (see library_digest)
  link  PATH  TEXT     each link command file again, with the command line itself, so that a difference is readable
Two builds that give the same listing link the same objects and libraries with the same command lines. (ANTS_SRC names another source folder than /src: for the tests.)

Why the debug information is left out of the libraries of the cache: the ports are compiled with debug information that holds the folder of their compile (the pre-warming stage's /warm in the new Dockerfile,
the build folder in the old one), so two builds differ there although the program that the link makes does not (the link drops it: no -g). Nothing else of a library is left out.
"""
import hashlib
import os
import subprocess
import sys

DEBUG_SECTIONS = (".debug_", "reloc..debug_")        # custom sections of a WebAssembly object: the debug information and the relocations that belong to it


def read_leb(data, at):
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, at


def object_without_debug(data):
    """The sections of a WebAssembly object as (id, name, payload) without the debug sections; anything else is returned as it is (an LLVM bitcode object, say)."""
    if data[:4] != b"\0asm":
        return data
    at, kept = 8, [data[:8]]
    while at < len(data):
        start = at
        section = data[at]
        size, at = read_leb(data, at + 1)
        end = at + size
        if section == 0:
            length, name_at = read_leb(data, at)
            if data[name_at:name_at + length].decode("utf-8", "replace").startswith(DEBUG_SECTIONS):
                at = end
                continue
        kept.append(data[start:end])
        at = end
    return b"".join(kept)


def archive_members(data):
    """[(name, content)] of an `ar` archive (GNU or BSD names), without the symbol tables: they hold offsets into the archive, which follow the sizes of the members."""
    if data[:8] != b"!<arch>\n":
        raise ValueError("not an archive")
    at, members, long_names = 8, [], b""
    while at + 60 <= len(data):
        header = data[at:at + 60]
        name = header[:16].decode("ascii", "replace").strip()
        size = int(header[48:58].decode("ascii").strip())
        body = data[at + 60:at + 60 + size]
        at += 60 + size + (size & 1)
        if name == "//":
            long_names = body
            continue
        if name in ("/", "/SYM64/", "__.SYMDEF", "__.SYMDEF SORTED"):
            continue
        if name.startswith("#1/"):                                                     # (BSD: the name is the first bytes of the content)
            length = int(name[3:])
            name, body = body[:length].rstrip(b"\0").decode("utf-8", "replace"), body[length:]
        elif name.startswith("/") and name[1:].isdigit():                              # (GNU: an offset into the table of long names)
            start = int(name[1:])
            name = long_names[start:long_names.index(b"/\n", start)].decode("utf-8", "replace")
        else:
            name = name.rstrip("/")
        members.append((name, body))
    return members


def library_digest(data):
    """sha256 of the members of an archive (their names and contents) with the debug information of the WebAssembly objects left out; the sha256 of the file when it is no archive."""
    try:
        members = archive_members(data)
    except (ValueError, IndexError):
        return hashlib.sha256(data).hexdigest()
    digest = hashlib.sha256()
    for name, body in members:
        body = object_without_debug(body)
        digest.update(b"%s\0%d\0" % (name.encode("utf-8"), len(body)))
        digest.update(body)
    return digest.hexdigest()


def file_digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def object_digest(data):
    """sha256 of an object file of the cache with the debug information left out."""
    return hashlib.sha256(object_without_debug(data)).hexdigest()


def emscripten_cache():
    try:
        cache = subprocess.run(["em-config", "CACHE"], capture_output=True, text=True, timeout=60).stdout.strip().splitlines()[-1]
    except (OSError, subprocess.SubprocessError, IndexError):
        cache = ""
    if not os.path.isdir(os.path.join(cache, "sysroot", "lib")):
        cache = "/emsdk/upstream/emscripten/cache"
    return cache


def walk(folder, wanted):
    return sorted(os.path.join(root, name) for root, _, names in os.walk(folder) for name in names if wanted(name))


def main():
    source = os.environ.get("ANTS_SRC", "/src")
    if not os.path.isdir(os.path.join(source, "build_web")):
        print("link_inputs: no build_web folder in %s" % source, file=sys.stderr)
        return 2
    os.chdir(source)
    cache = emscripten_cache()
    if not os.path.isdir(os.path.join(cache, "sysroot", "lib")):
        print("link_inputs: no Emscripten cache folder", file=sys.stderr)
        return 2
    builds = walk("build_web", lambda n: n.endswith((".o", ".a", ".rsp")) or n in ("link.txt", "flags.make")) + walk("build_web/generated", lambda n: True)
    entries = [(path, file_digest(path)) for path in sorted(set(builds))]
    for path in walk(os.path.join(cache, "sysroot", "lib"), lambda n: n.endswith((".a", ".o"))):
        with open(path, "rb") as f:
            entries.append((path, library_digest(f.read()) if path.endswith(".a") else object_digest(f.read())))
    lines = ["file\t%s\t%s" % entry for entry in sorted(entries)]
    for path in walk("build_web", lambda n: n == "link.txt"):
        with open(path, encoding="utf-8", errors="replace") as f:
            lines.append("link\t%s\t%s" % (path, " ".join(f.read().split("\n")).strip()))
    sys.stdout.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
