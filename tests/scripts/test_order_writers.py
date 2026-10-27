#!/usr/bin/env python3
"""Every write of an ant's order goes through the three setters of src/ants_sim/order_claims.cpp (run by ./run_tests.sh --fast and by the CI).

The claim lists (docs/ARCHITECTURE.md, "Lookups in the tick loop") hold the ants that have a Move or Home order to a tile; they are exact because every write of `orig_order` and
`orig_order_tile` (and AntUnit::clear_path(), which writes both) ends in `claim_note`. A new line that writes one of them directly would leave the lists wrong, and no test of a match
need meet it. This test reads every .cpp and .hpp of src/ and tools/ and fails for such a line outside order_claims.cpp (the setters) and ant_unit.hpp (the fields and clear_path
themselves). Reads, comparisons (`==`) and the words in comments are not writes. The tests of tests/ write the fields on purpose, from outside, and are not looked at.
"""
import glob
import os
import re
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ALLOWED = {"src/ants_sim/order_claims.cpp", "include/ants_sim/ant_unit.hpp"}
WRITE = re.compile(r"\borig_order(_tile)?\s*(=(?!=)|\+\+|--|\+=|-=)|\borig_order(_tile)?\s*\)\s*=(?!=)|(\+\+|--)\s*[\w.>-]*\borig_order|\bclear_path\s*\(\s*\)")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def direct_writes(text):
    """The numbers of the lines of `text` that write an order field directly (comments left out)."""
    found = []
    for number, line in enumerate(strip_comments(text).split("\n"), 1):
        if WRITE.search(line):
            found.append(number)
    return found


def sources():
    for base in ("src", "tools", "include"):
        for path in glob.glob(os.path.join(REPO, base, "**", "*"), recursive=True):
            if path.endswith((".cpp", ".hpp", ".h")) and os.path.isfile(path):
                yield os.path.relpath(path, REPO).replace(os.sep, "/"), path


class OrdersAreWrittenByTheSetters(unittest.TestCase):
    def test_no_direct_write_outside_the_setters(self):
        seen = 0
        for rel, path in sources():
            seen += 1
            if rel in ALLOWED:
                continue
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = direct_writes(f.read())
            self.assertEqual(lines, [], "%s writes an order field directly on line(s) %s: call set_order_kind, set_order_tile or clear_path_of "
                                        "(src/ants_sim/order_claims.cpp), which tell the claim lists" % (rel, lines))
        self.assertGreater(seen, 100, "where are the sources?")

    def test_the_setters_are_where_they_are_said_to_be(self):
        with open(os.path.join(REPO, "src/ants_sim/order_claims.cpp"), encoding="utf-8") as f:
            text = f.read()
        for name in ("set_order_kind", "set_order_tile", "clear_path_of"):
            self.assertRegex(text, r"SimulationEngineImpl::%s\(" % name)
        self.assertEqual(len(direct_writes(text)), 3, "the three setters write the fields (and nothing else in the file does)")


class TheCheckCanFail(unittest.TestCase):
    """The scan above is not vacuous: the forms of a write are found, the forms of a read are not."""

    def test_writes_are_found(self):
        for line in ("a.orig_order = 3;", "x->orig_order_tile = t;", "a.orig_order=AntUnit::kOrderMove;", "    n->orig_order_tile  =no_order_tile();", "a.clear_path();", "u->clear_path ( );",
                     "++a.orig_order;", "a.orig_order += 1;"):
            self.assertEqual(direct_writes(line + "\n"), [1], line)

    def test_reads_and_comments_are_not(self):
        for line in ("if (a.orig_order == 3) return;", "x = a.orig_order_tile;", "// a.orig_order = 3;", "/* a.orig_order_tile = t; */", "foo(a.orig_order, b.orig_order_tile);",
                     "const bool same = a.orig_order_tile != b.orig_order_tile;", "a.orig_order_tile == t) {"):
            self.assertEqual(direct_writes(line + "\n"), [], line)


if __name__ == "__main__":
    unittest.main()
