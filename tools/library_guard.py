"""The owner's rule for the tools, in one place (the C++ tools use tools/library_guard.hpp, the same rule in the same words).

No tool may modify, re-save, repair, re-render or delete a map of the library (Original-Ants/ and Community-Maps/, the six originals and the players' maps, anything the site
serves under /maps), and no tool writes over the file it reads. A tool that writes a path a person gives it calls `require_writable(path, inputs)` first; it raises
`LibraryRefused` (a ValueError) with a sentence in plain words, and the tool prints it and exits non-zero. Reading the library stays fine.

A path is refused when
  - one of its folders (or the file itself) is named Original-Ants or Community-Maps, in any case (the files of Windows and macOS fold case; Linux is held to the same names,
    which is stricter and harmless), as written (with `.` and `..` taken out) and again after its symbolic links are followed;
  - one of its existing folders is this checkout's own Original-Ants or Community-Maps (the same folder by the system's own test: another case, a short name on Windows);
  - it is an existing file with more than one name (a hard link; a library map may be the other name);
  - it is a file that the command reads (the same file by the system's own test).
Words: docs/WORKFLOW.md, "Tools never touch the map library".
"""
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir))
LIBRARY_NAMES = ("original-ants", "community-maps")


class LibraryRefused(ValueError):
    """A path that a tool must not write; the text is the sentence for the person."""


def checkout_library():
    """This checkout's own library folders."""
    return [os.path.join(REPO, "Original-Ants"), os.path.join(REPO, "Community-Maps")]


def _parts(path):
    return [p for p in path.replace("\\", "/").split("/") if p]


def names_library(path):
    """True when a folder of `path`, or its last name, is Original-Ants or Community-Maps in any case."""
    return any(p.lower() in LIBRARY_NAMES for p in _parts(path))


def _same(a, b):
    try:
        return os.path.samefile(a, b)
    except OSError:
        return False


def inside_folders(path, library):
    """True when `path` or a folder above it is the same folder as one of `library`."""
    at = path
    for _ in range(256):
        if os.path.exists(at) and any(os.path.isdir(folder) and _same(at, folder) for folder in library):
            return True
        up = os.path.dirname(at)
        if not up or up == at:
            return False
        at = up
    return False


def write_refusal(path, inputs=(), library=None):
    """'' when a tool may write `path`; else the reason, in plain words. `inputs` are the files the command reads; `library` the folders to hold apart (default: this checkout's)."""
    if not path:
        return ""
    library = checkout_library() if library is None else list(library)
    absolute = os.path.normpath(os.path.abspath(path))
    real = os.path.realpath(absolute)
    if names_library(absolute) or names_library(real) or inside_folders(real, library):
        return ("%s is in the map library (Original-Ants or Community-Maps): no tool writes there, the library maps are never changed, re-saved, repaired, re-rendered or deleted; "
                "write to a copy somewhere else" % path)
    if os.path.isfile(real) and os.stat(real).st_nlink > 1:
        return "%s is a file with more than one name (a hard link), and the other name may be a map of the library: write to a new file" % path
    for source in inputs:
        if source and os.path.exists(source) and os.path.exists(real) and _same(source, real):
            return "%s is the file that is read: a tool never writes over its own input, write to another file" % path
    return ""


def require_writable(path, inputs=(), library=None):
    """Raises LibraryRefused when a tool may not write `path`."""
    why = write_refusal(path, inputs, library)
    if why:
        raise LibraryRefused(why)
