#!/usr/bin/env python3
"""Every write of an ant's order goes through the three setters of src/ants_sim/order_claims.cpp (run by ./run_tests.sh --fast and by the CI).

The claim lists (docs/ARCHITECTURE.md, "Lookups in the tick loop") hold the ants that have a Move or Home order to a tile; they are exact because every write of `orig_order`
and `orig_order_tile` (and AntUnit::clear_path(), which writes both) ends in `claim_note`. A new line that writes one of them directly would leave the lists wrong, and no test of
a match need meet it: for most sites this is the only guard. This test reads every .cpp, .hpp and .h of src/, include/ and tools/ and fails for such a line outside
order_claims.cpp (the setters) and ant_unit.hpp (the two field initialisers and clear_path itself, line by line). Found: an assignment or any compound operator (=, +=, -=, *=, /=,
%=, |=, &=, ^=, <<=, >>=, ++, --) on either field or on a member (.x, .y) of the tile, std::swap, std::exchange, std::tie, memcpy, memmove, memset, std::copy or std::fill applied to either
field, an address taken (&a.orig_order) and a reference bound to one (T& r = a.orig_order). Reads, comparisons (`==`), the words in comments and a logical or bitwise `&` on a value are
not writes. What a line scan cannot see: a whole AntUnit assigned or copied (an engine copy copies the lists with the ants), and a call that lies across lines. The tests of tests/
write the fields on purpose, from outside, and are not looked at.
"""
import glob
import os
import re
import shutil
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SETTERS = "src/ants_sim/order_claims.cpp"
FIELDS_HEADER = "include/ants_sim/ant_unit.hpp"
# the only lines of the header that may match: clear_path and its two writes (the initialisers `orig_order{...}` are not writes)
HEADER_ALLOWED = {"void clear_path() noexcept {", "orig_order = kOrderNone;", "orig_order_tile = TileCoord{kNoOrderTileX, kNoOrderTileY};"}
FIELD = r"\borig_order(?:_tile)?"
OPS = r"(?:=(?!=)|[-+*/%|&^]=|<<=|>>=|\+\+|--)"
WRITE = re.compile("|".join([
    FIELD + r"\s*" + OPS,                                                              # a.orig_order = x;  ++ -- += |= <<= ...
    r"\borig_order_tile\s*\.\s*[xy]\s*" + OPS,                                         # a.orig_order_tile.x = 3;
    FIELD + r"\s*\)\s*=(?!=)",                                                         # (a.orig_order) = x;
    r"(?:\+\+|--)\s*[\w.>\-]*" + FIELD,                                                # ++a.orig_order
    r"\bclear_path\s*\(\s*\)",                                                         # a.clear_path();
    r"\b(?:std::swap|std::exchange|std::tie|memcpy|memmove|memset|std::copy|std::fill|std::copy_n|std::fill_n)\b[^;]*" + FIELD,   # std::swap(a.orig_order, b)
    r"[&*]{1,2}\s*\w+\s*=\s*[\w:.>\-\[\]()*]*" + FIELD,                                # auto& r = a.orig_order;  T* p = a.orig_order;
]))
ADDRESS = re.compile(r"&\s*[\w:.>\-\[\]*()]*" + FIELD)                                 # &a.orig_order, (&a)->orig_order is not caught: that is no field


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)   # (the line numbers stay)
    return re.sub(r"//[^\n]*", "", text)


def takes_address(line):
    """An `&` that starts an expression just before one of the fields: not `&&`, and not a binary and (the character before it ends a value)."""
    for m in ADDRESS.finditer(line):
        if m.start() > 0 and line[m.start() - 1] == "&":          # `&&` (a logical and); `&& &a.orig_order` has a blank between and is an address
            continue
        before = line[:m.start()].rstrip()
        if before and (before[-1].isalnum() or before[-1] in "_)]"):
            continue
        return True
    return False


def direct_writes(text):
    """The numbers of the lines of `text` that write an order field directly (comments left out)."""
    found = []
    for number, line in enumerate(strip_comments(text).split("\n"), 1):
        if WRITE.search(line) or takes_address(line):
            found.append(number)
    return found


def source_files(root):
    for base in ("src", "tools", "include"):
        for path in glob.glob(os.path.join(root, base, "**", "*"), recursive=True):
            if path.endswith((".cpp", ".hpp", ".h")) and os.path.isfile(path):
                yield os.path.relpath(path, root).replace(os.sep, "/"), path


def offenders(root):
    """[(file, [lines])] for every file of the tree that writes an order field outside the setters (and, in the fields' header, outside clear_path)."""
    out, seen = [], 0
    for rel, path in source_files(root):
        seen += 1
        if rel == SETTERS:
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        lines = direct_writes(text)
        if rel == FIELDS_HEADER:
            raw = text.split("\n")
            lines = [n for n in lines if raw[n - 1].strip() not in HEADER_ALLOWED]
        if lines:
            out.append((rel, lines))
    return out, seen


class OrdersAreWrittenByTheSetters(unittest.TestCase):
    def test_no_direct_write_outside_the_setters(self):
        found, seen = offenders(REPO)
        self.assertGreater(seen, 100, "where are the sources?")
        for rel, lines in found:
            self.fail("%s writes an order field directly on line(s) %s: call set_order_kind, set_order_tile or clear_path_of "
                      "(src/ants_sim/order_claims.cpp), which tell the claim lists" % (rel, lines))

    def test_the_setters_are_where_they_are_said_to_be(self):
        with open(os.path.join(REPO, SETTERS), encoding="utf-8") as f:
            text = f.read()
        for name in ("set_order_kind", "set_order_tile", "clear_path_of"):
            self.assertRegex(text, r"SimulationEngineImpl::%s\(" % name)
        self.assertEqual(len(direct_writes(text)), 3, "the three setters write the fields (and nothing else in the file does)")

    def test_the_fields_header_has_only_clear_path_and_the_initialisers(self):
        with open(os.path.join(REPO, FIELDS_HEADER), encoding="utf-8") as f:
            text = f.read()
        raw = text.split("\n")
        hits = {raw[n - 1].strip() for n in direct_writes(text)}
        self.assertEqual(hits, HEADER_ALLOWED)


class TheCheckCanFail(unittest.TestCase):
    """The scan above is not vacuous: every form of a write is found, the forms of a read are not."""

    WRITES = ("a.orig_order = 3;", "x->orig_order_tile = t;", "a.orig_order=AntUnit::kOrderMove;", "    n->orig_order_tile  =no_order_tile();", "a.clear_path();", "u->clear_path ( );",
              "++a.orig_order;", "a.orig_order++;", "--a.orig_order;", "a.orig_order += 1;", "a.orig_order -= 1;", "a.orig_order |= 4;", "a.orig_order &= 4;", "a.orig_order ^= 4;",
              "a.orig_order *= 2;", "a.orig_order /= 2;", "a.orig_order %= 2;", "a.orig_order <<= 1;", "a.orig_order >>= 1;",
              "a.orig_order_tile.x = 3;", "a.orig_order_tile . y=4;", "a.orig_order_tile.x += 1;", "a.orig_order_tile.y++;", "--a.orig_order_tile.x;", "a.orig_order_tile.x <<= 2;",
              "std::swap(a.orig_order, b);", "std::swap(b.orig_order_tile, a.orig_order_tile);", "const auto old = std::exchange(a.orig_order, 3);", "std::tie(a.orig_order, k) = p;",
              "memcpy(&a.orig_order, &v, 1);", "std::memmove(dst, a.orig_order_tile, 8);", "memset(a.orig_order, 0, 1);", "std::fill(a.orig_order, e, 0);",
              "uint8_t* p = &a.orig_order;", "foo(&a.orig_order_tile);", "foo( & a.orig_order_tile );", "auto p = &x->orig_order;", "uint8_t& r = a.orig_order;", "auto& t = a.orig_order_tile;",
              "TileCoord& t = x->orig_order_tile;", "(a.orig_order) = 3;", "bool b = x && &a.orig_order != nullptr;")

    READS = ("if (a.orig_order == 3) return;", "x = a.orig_order;", "// a.orig_order = 3;", "/* a.orig_order_tile = t; */", "foo(a.orig_order, b.orig_order_tile);",
             "const bool same = a.orig_order_tile != b.orig_order_tile;", "a.orig_order_tile == t) {", "const uint8_t o = a.orig_order;", "int n = a.orig_order_tile.x + 1;",
             "if (a.orig_order_tile.x == 3 && a.orig_order_tile.y >= 2) {", "return x->orig_order != 0 && y->orig_order == 0;", "if (a.orig_order >= 3 && b.orig_order <= 2) {",
             "const bool both = ok && a.orig_order == 1;", "const int m = flags & a.orig_order;", "const int m2 = (flags) & a.orig_order_tile.x;", "if (a.orig_order != b.orig_order) {",
             "uint8_t     orig_order{kOrderNone};     // +0xa8", "TileCoord   orig_order_tile{kNoOrderTileX, kNoOrderTileY}; // +0xac", "const auto k = std::min(a.orig_order, b.orig_order);",
             "cells[a.orig_order_tile.y * w + a.orig_order_tile.x] = 1;")

    def test_writes_are_found(self):
        for line in self.WRITES:
            self.assertEqual(direct_writes(line + "\n"), [1], line)

    def test_reads_and_comments_are_not(self):
        for line in self.READS:
            self.assertEqual(direct_writes(line + "\n"), [], line)

    def test_a_scratch_tree_with_each_form_fails_and_the_clean_tree_passes(self):
        with tempfile.TemporaryDirectory() as root:
            for sub in ("src/ants_sim", "include/ants_sim", "tools"):
                os.makedirs(os.path.join(root, sub))
            shutil.copy(os.path.join(REPO, FIELDS_HEADER), os.path.join(root, FIELDS_HEADER))
            with open(os.path.join(root, "src/ants_sim/a.cpp"), "w") as f:
                f.write("void f(AntUnit& a) { if (a.orig_order == 1) return; auto t = a.orig_order_tile; }\n")
            self.assertEqual(offenders(root)[0], [], "the clean tree (the real fields' header and a file that only reads) passes")
            for n, line in enumerate(self.WRITES):
                where = ("src/ants_sim/w%d.cpp" if n % 3 == 0 else "tools/w%d.cpp" if n % 3 == 1 else "include/ants_sim/w%d.hpp") % n
                with open(os.path.join(root, where), "w") as f:
                    f.write("void g(AntUnit& a, AntUnit& b, AntUnit* x) {\n    %s\n}\n" % line)
                found = dict(offenders(root)[0])
                self.assertEqual(found.get(where), [2], line)
                os.remove(os.path.join(root, where))
            self.assertEqual(offenders(root)[0], [])

    def test_the_fields_header_exemption_is_narrow(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, "include/ants_sim"))
            with open(os.path.join(REPO, FIELDS_HEADER), encoding="utf-8") as f:
                text = f.read()
            path = os.path.join(root, FIELDS_HEADER)
            for extra in ("void bad() { orig_order = 3; }", "void bad() { orig_order_tile.x = 3; }", "void bad() { orig_order |= 1; }"):
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text + "\n" + extra + "\n")
                found = offenders(root)[0]
                self.assertEqual([rel for rel, _ in found], [FIELDS_HEADER], extra)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            self.assertEqual(offenders(root)[0], [])


if __name__ == "__main__":
    unittest.main()
