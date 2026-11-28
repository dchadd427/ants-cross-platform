"""Reads a list of mutants for the tests that run some of its mutants themselves, on a copy of the files (used by the tests of the pages that keep their mutants in tests/data; not a test file).

A list (tests/data/NAME_mutants.json) may have a second half, tests/data/NAME_server_mutants.json: the mutants that need files which are not in every copy of the tree. tools/mutate.py
reads the two as one list, and so does load(): the first half's own keys, then the mutants of the first half and those of the second, each of the second half with the defaults
of its own file (the first half's mutants stay as they are written). A tree that does not have the second half reads the first alone.
"""
import json
import os

SECOND_HALF = "_server_mutants.json"


def second_half(path):
    """the second half of the list at `path` (NAME_server_mutants.json for NAME_mutants.json) when it is there, else None; a second half has none"""
    folder, name = os.path.split(path)
    if not name.endswith("_mutants.json") or name.endswith(SECOND_HALF):
        return None
    other = os.path.join(folder, name[:-len("_mutants.json")] + SECOND_HALF)
    return other if os.path.isfile(other) else None


def load(path):
    """the list as a dict (what json.load gives for the file) whose "mutants" are those of both halves, the first half's first"""
    with open(path, encoding="utf-8") as f:
        spec = json.load(f)
    other = second_half(path)
    if other:
        with open(other, encoding="utf-8") as f:
            more = json.load(f)
        defaults = more.get("defaults", {})
        spec["mutants"] = list(spec["mutants"]) + [dict(defaults, **m) for m in more["mutants"]]
    return spec
