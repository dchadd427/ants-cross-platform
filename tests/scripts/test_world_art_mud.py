#!/usr/bin/env python3
"""The golden file of the dense mud (tests/data/world_art_mud_golden.txt, made by tools/world_art/mud_reference.py from tools/world_art/mud_fields.py; tools/world_art/README.md): run by ./run_tests.sh and
the CI with the other python tests of tests/scripts. The game's C++ port of the mud fields is compared with this file (tests/test_app/test_world_art_mud.cpp), so the file itself is held here:
  - TheGoldenFile: the committed file is in its format (the header, LF only, the records in their order and numbers, nine decimals, whole numbers where a whole number belongs, every value finite and
    in the range of its kind), its map is the synthetic one that the README describes, and its sample points reach every part of the depth that the looks distinguish;
  - TheLooks: the fields that make it follow the rules of the looks (no bank at the border of the map, depth 0 at a bank and 7 at most, a look that is exactly nothing before its first depth);
  - TheRegeneration: `mud_reference.py` writes the committed file again byte for byte (about 5 seconds);
  - TheCommand: the tool refuses a file inside the map library;
  - TheChecksAreNotVacuous: each rule above refuses a copy of the real file that is broken in that one way.
Needs numpy (the tests are skipped only on a machine that has none; CI has it).
"""
import importlib.util
import math
import os
import re
import subprocess
import sys
import tempfile
import unittest

try:
    import numpy as np
except ImportError:
    np = None

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GOLDEN = os.path.join(REPO, "tests", "data", "world_art_mud_golden.txt")
TOOL = os.path.join(REPO, "tools", "world_art", "mud_reference.py")

HEADER = "ants-world-art-mud-golden 1"
COUNTS = [("mask", 20), ("hash", 12), ("vnoise", 12), ("fbm", 16), ("depth", 40), ("celldepth", 20), ("wetpatch", 40), ("ripple_median", 1)]
WHOLE = r"-?(?:0|[1-9][0-9]*)"
DECIMAL = r"-?[0-9]+\.[0-9]{9}"
FORMS = {                         # the words after the kind, as a regular expression each
    "mask": r"[#.]{24}",
    "hash": " ".join([WHOLE] * 3 + [DECIMAL]),
    "vnoise": " ".join([DECIMAL, DECIMAL, WHOLE, DECIMAL]),
    "fbm": " ".join([DECIMAL, DECIMAL, DECIMAL, WHOLE, WHOLE] + [DECIMAL] * 5),
    "depth": " ".join([DECIMAL] * 3),
    "celldepth": " ".join([WHOLE, WHOLE, DECIMAL]),
    "wetpatch": " ".join([DECIMAL] * 8),
    "ripple_median": DECIMAL,
}
BAND_EDGES = (1.5, 1.8, 3.0, 5.0)         # the depths where the looks begin (the patches 1.5, the darkness and ripples 1.8, the sheen 3) and reach their full strength (5)
WETPATCH_PER_BAND = 3                     # at least this many sample points in each of the five bands that the edges make


def load_fields():
    """tools/world_art/mud_fields.py, loaded by its path under a name of its own"""
    spec = importlib.util.spec_from_file_location("world_art_mud_fields", os.path.join(REPO, "tools", "world_art", "mud_fields.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def described_mask():
    """the map as the README describes it, rows of '#' (plain mud) and '.': 24 columns x 20 rows, a 3 x 3 block at columns 4 to 6 and rows 4 to 6, a column at 15 for rows 2 to 13, a 2 x 2 block at
    columns 20 and 21, rows 15 and 16"""
    rows = [["#"] * 24 for _ in range(20)]
    for r in range(4, 7):
        for c in range(4, 7):
            rows[r][c] = "."
    for r in range(2, 14):
        rows[r][15] = "."
    for r in range(15, 17):
        for c in range(20, 22):
            rows[r][c] = "."
    return ["".join(row) for row in rows]


def records_of(text):
    """[(kind, [words])] of the lines after the header"""
    return [(line.split(" ")[0], line.split(" ")[1:]) for line in text[:-1].split("\n")[1:]]


def format_problems(text):
    """what is wrong with the text's format: the header, the line ends, the records and their words, the numbers of each kind and their order, the values' ranges"""
    out = []
    if "\r" in text or not text.endswith("\n") or text.endswith("\n\n"):
        return ["one record a line, every line ends with LF, no empty line"]
    lines = text[:-1].split("\n")
    if lines[0] != HEADER:
        return ["the first line is the header"]
    kinds = [line.split(" ")[0] for line in lines[1:]]
    want = [kind for kind, count in COUNTS for _ in range(count)]
    if kinds != want:
        out.append("the records are %s" % ", ".join("%d %s" % (count, kind) for kind, count in COUNTS))
    for line in lines[1:]:
        kind = line.split(" ")[0]
        if kind not in FORMS or not re.fullmatch(kind + " " + FORMS[kind], line):
            out.append("not a record of its kind: %r" % line)
    if out:
        return out
    for kind, words in records_of(text):
        numbers = [] if kind == "mask" else [float(w) for w in words]
        if not all(math.isfinite(v) for v in numbers):
            out.append("a value is not finite: %s %s" % (kind, " ".join(words)))
        elif kind in ("hash", "vnoise", "fbm") and not 0.0 <= numbers[-1] <= 1.0:
            out.append("a noise value is not in 0 to 1: %s %s" % (kind, " ".join(words)))
        elif kind in ("depth", "celldepth") and not 0.0 <= numbers[-1] <= 7.0:
            out.append("a depth is not in 0 to 7 tiles: %s %s" % (kind, " ".join(words)))
        elif kind == "wetpatch" and not all(0.0 < v <= 1.0 for v in numbers[2:5]):
            out.append("a color's factor is not above 0 and at most 1: %s" % " ".join(words))
        elif kind == "ripple_median" and numbers[0] <= 0.0:
            out.append("the median of the ripple field's gradient is not above 0")
    return out


def mask_problems(text):
    return [] if [words[0] for kind, words in records_of(text) if kind == "mask"] == described_mask() else ["the map is not the one that the README describes"]


def coverage_problems(text, fields):
    """the sample points of the wetpatch records lie in every depth band between the edges, and one lies in each corner of the map"""
    mask = np.array([[c == "#" for c in row] for row in described_mask()])
    D = fields.depth_grid(mask)
    points = [(float(words[0]), float(words[1])) for kind, words in records_of(text) if kind == "wetpatch"]
    depth = fields.looks_at(D, np.array([[p[0]] for p in points]), np.array([[p[1]] for p in points]), ("wet",))["depth"][:, 0]
    out = []
    edges = (-np.inf,) + BAND_EDGES + (np.inf,)
    for lo, hi in zip(edges, edges[1:]):
        found = int(((depth >= lo) & (depth < hi)).sum())
        if found < WETPATCH_PER_BAND:
            out.append("%d sample points have a depth in [%s, %s), at least %d are wanted" % (found, lo, hi, WETPATCH_PER_BAND))
    for name, (cx, cy) in {"upper left": (0, 0), "upper right": (768, 0), "lower left": (0, 640), "lower right": (768, 640)}.items():
        if not any(abs(p[0] - cx) < 4 and abs(p[1] - cy) < 4 for p in points):
            out.append("no sample point in the %s corner of the map" % name)
    return out


@unittest.skipUnless(np, "numpy is needed")
class Committed(unittest.TestCase):
    """the committed file, read once"""

    @classmethod
    def setUpClass(cls):
        with open(GOLDEN, "rb") as f:
            cls.raw = f.read()
        cls.text = cls.raw.decode("utf-8")


class TheGoldenFile(Committed):
    def test_the_file_is_in_its_format(self):
        self.assertEqual(format_problems(self.text), [])

    def test_the_map_is_the_synthetic_one_of_the_readme(self):
        self.assertEqual(mask_problems(self.text), [])

    def test_the_sample_points_reach_every_depth_that_the_looks_distinguish(self):
        self.assertEqual(coverage_problems(self.text, load_fields()), [])


@unittest.skipUnless(np, "numpy is needed")
class TheLooks(unittest.TestCase):
    """the fields that the golden file holds the C++ port to"""

    @classmethod
    def setUpClass(cls):
        cls.fields = load_fields()
        cls.mask = np.array([[c == "#" for c in row] for row in described_mask()])

    def test_the_border_of_the_map_is_not_a_bank(self):
        D = self.fields.depth_grid(np.ones((6, 9), bool))
        self.assertEqual(D.shape, (6 * 32 // 4, 9 * 32 // 4))
        self.assertTrue((D == self.fields.CAP).all())

    def test_the_depth_is_nothing_at_a_bank_and_never_over_seven_tiles(self):
        D = self.fields.depth_grid(self.mask)
        self.assertEqual(float(D.min()), 0.0)
        self.assertEqual(float(D.max()), 7.0)
        self.assertEqual(self.fields.cell_depth(D, 15, 7), 0.0)               # a cell of the column that is no mud
        self.assertEqual(self.fields.cell_depth(D, 4, 4), 0.0)
        self.assertEqual(self.fields.cell_depth(D, 23, 0), 7.0)               # the far corner

    def test_a_look_is_exactly_nothing_before_its_first_depth(self):
        crop = (0, 0, 768, 640)
        out = self.fields.layers(self.mask, 1, crop, ss=1)
        shallow = out["depth"] < 1.5                                             # the first look begins at 1.5 tiles (the patches), the others at 1.8 and 3
        self.assertGreater(int(shallow.sum()), 100000)
        self.assertTrue((out["mult"][shallow] == 1.0).all())
        self.assertTrue((out["add"][shallow] == 0.0).all())
        self.assertTrue((out["rip"][shallow] == 0.0).all())
        deep = out["depth"] > 5.5
        self.assertGreater(int(deep.sum()), 100000)
        self.assertLess(float(out["mult"][deep].max()), 0.95)                    # the middle is darker
        self.assertGreater(float(np.abs(out["rip"][deep]).max()), 1.0)           # and has ripples

    def test_a_point_gives_the_same_values_alone_or_in_a_crop(self):
        D = self.fields.depth_grid(self.mask)
        one = self.fields.looks_at(D, np.array([[300.5]]), np.array([[400.5]]), ("wet", "patch"))
        crop = self.fields.layers(self.mask, 1, (300, 400, 1, 1), which=("wet", "patch"), ss=1)
        self.assertTrue(np.array_equal(one["mult"][0, 0], crop["mult"][0, 0]))
        self.assertTrue(np.array_equal(one["add"][0, 0], crop["add"][0, 0]))

    def test_the_hash_takes_whole_numbers_of_either_sign_modulo_two_to_the_32(self):
        a = self.fields._hash(np.array([-1, 0, 5]), np.array([3, -7, 0]), 7)
        b = self.fields._hash(np.array([-1 + 2 ** 32, 0, 5]), np.array([3, -7 + 2 ** 32, 0]), 7)
        self.assertTrue(np.array_equal(a, b))
        self.assertTrue(((a >= 0.0) & (a < 1.0)).all())
        self.assertEqual(len(set(a.tolist())), 3)


@unittest.skipUnless(np, "numpy is needed")
class TheRegeneration(unittest.TestCase):
    def test_the_tool_writes_the_committed_file_again_byte_for_byte(self):
        with tempfile.TemporaryDirectory(prefix="world_art_mud_") as folder:
            out = os.path.join(folder, "golden.txt")
            run = subprocess.run([sys.executable, TOOL, "--out", out], capture_output=True, text=True, timeout=300)
            self.assertEqual(run.returncode, 0, run.stderr)
            with open(out, "rb") as f, open(GOLDEN, "rb") as committed:
                self.assertEqual(f.read(), committed.read())


@unittest.skipUnless(np, "numpy is needed")
class TheCommand(unittest.TestCase):
    def test_a_file_in_the_map_library_is_refused_and_nothing_is_written(self):
        for folder in ("Original-Ants", "Community-Maps"):
            path = os.path.join(REPO, folder, "world_art_mud_golden.txt")
            run = subprocess.run([sys.executable, TOOL, "--out", path], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 2, run.stderr)
            self.assertFalse(os.path.exists(path))


@unittest.skipUnless(np, "numpy is needed")
class TheChecksAreNotVacuous(Committed):
    def replace_line(self, startswith, new, index=0):
        """the committed text with the `index`-th line that starts with `startswith` replaced"""
        lines = self.text.split("\n")
        at = [i for i, line in enumerate(lines) if line.startswith(startswith)][index]
        lines[at] = new
        return "\n".join(lines)

    def test_the_format_is_refused_when_it_is_broken(self):
        lines = self.text.split("\n")
        first_hash = lines.index(next(line for line in lines if line.startswith("hash ")))
        first_vnoise = lines.index(next(line for line in lines if line.startswith("vnoise ")))
        swapped = list(lines)
        swapped[first_hash], swapped[first_vnoise] = swapped[first_vnoise], swapped[first_hash]
        for why, text in {
            "CRLF": self.text.replace("\n", "\r\n"),
            "no LF at the end": self.text[:-1],
            "an empty line at the end": self.text + "\n",
            "another header": self.text.replace(HEADER, "ants-world-art-mud-golden 2", 1),
            "a record missing": "\n".join(lines[:first_hash] + lines[first_hash + 1:]),
            "a record twice": "\n".join(lines[:first_hash] + [lines[first_hash]] + lines[first_hash:]),
            "records out of order": "\n".join(swapped),
            "another kind of record": self.text + "frame 1 2 3\n",
            "a mask row a cell short": self.replace_line("mask ", "mask " + "#" * 23),
            "a mask row with another character": self.replace_line("mask ", "mask " + "#" * 23 + "x"),
            "a number with eight decimals": self.replace_line("depth ", "depth 0.00000000 0.000000000 5.568465902"),
            "a number with ten decimals": self.replace_line("depth ", "depth 0.0000000000 0.000000000 5.568465902"),
            "a seed that is not whole": self.replace_line("hash ", "hash 0 0 7.5 0.908025563"),
            "a value that is a word": self.replace_line("celldepth ", "celldepth 5 5 nan"),
            "a hash over 1": self.replace_line("hash ", "hash 0 0 7 1.000000001"),
            "a noise value below 0": self.replace_line("vnoise ", "vnoise 0.000000000 0.000000000 7 -0.000000001"),
            "a depth over 7": self.replace_line("depth ", "depth 0.000000000 0.000000000 7.000000001"),
            "a negative depth": self.replace_line("celldepth ", "celldepth 5 5 -0.500000000"),
            "a color factor of 0": self.replace_line("wetpatch ", "wetpatch 659.500000000 522.500000000 0.000000000 1.000000000 1.000000000 0.000000000 0.000000000 0.000000000"),
            "a color factor over 1": self.replace_line("wetpatch ", "wetpatch 659.500000000 522.500000000 1.000000001 1.000000000 1.000000000 0.000000000 0.000000000 0.000000000"),
            "a median of 0": self.replace_line("ripple_median ", "ripple_median 0.000000000"),
        }.items():
            self.assertNotEqual(format_problems(text), [], why)

    def test_a_map_that_is_not_the_described_one_is_found(self):
        self.assertEqual(len(mask_problems(self.replace_line("mask ", "mask " + "." + "#" * 23))), 1)
        self.assertEqual(len(mask_problems(self.replace_line("mask ", "mask " + "#" * 24, index=2))), 1)             # the column's first cell filled in

    def test_sample_points_that_miss_a_depth_band_or_a_corner_are_found(self):
        fields = load_fields()
        self.assertEqual(coverage_problems(self.text, fields), [])
        lines = self.text.split("\n")
        wet = [line for line in lines if line.startswith("wetpatch ")]
        mask = np.array([[c == "#" for c in row] for row in described_mask()])
        xs = np.array([[float(line.split(" ")[1])] for line in wet])
        ys = np.array([[float(line.split(" ")[2])] for line in wet])
        depth = fields.looks_at(fields.depth_grid(mask), xs, ys, ("wet",))["depth"][:, 0]
        for lo, hi in zip((-np.inf,) + BAND_EDGES, BAND_EDGES + (np.inf,)):
            gone = {line for line, d in zip(wet, depth) if lo <= d < hi}
            text = "\n".join(line for line in lines if line not in gone)
            self.assertTrue(any("in [%s, %s)" % (lo, hi) in problem for problem in coverage_problems(text, fields)), (lo, hi))
        corners = {line for line in wet if float(line.split(" ")[1]) in (0.5, 767.5) and float(line.split(" ")[2]) in (0.5, 639.5)}
        self.assertEqual(len(corners), 4)
        text = "\n".join(line for line in lines if line not in corners)
        self.assertEqual(len([problem for problem in coverage_problems(text, fields) if "corner" in problem]), 4)


if __name__ == "__main__":
    unittest.main()
