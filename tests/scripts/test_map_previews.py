#!/usr/bin/env python3
"""Pre-rendered map previews (tools/map_previews.py; run by ./run_tests.sh --tools).

  - the pure logic on a made-up folder with a fake renderer (no game): the picture's name, the hash bookkeeping of previews.json, resumability (a second run draws nothing, an
    interrupted run goes on), a changed map is drawn again, a missing picture is drawn again, a map that is gone loses its picture, --force, a damaged index, parallel runs;
  - the folder of the repository: previews.json lists exactly the maps of maps.json with their hashes, and every picture exists and is a PNG of the expected size.
"""
import json
import os
import shutil
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import map_previews  # noqa: E402
from PIL import Image  # noqa: E402

FOLDER = os.path.join(REPO, "Community-Maps")


class FakeRenderer:
    """Stands in for the game: a 300 x 300 picture whose colour depends on the map's bytes; remembers what it was asked."""
    def __init__(self, fail_on=None):
        self.calls = []
        self.fail_on = fail_on

    def __call__(self, path):
        self.calls.append(os.path.basename(path))
        if self.fail_on and os.path.basename(path) == self.fail_on:
            raise RuntimeError("the game did not make its screenshot")
        with open(path, "rb") as f:
            seed = sum(f.read()) % 256
        return Image.new("RGB", (map_previews.SOURCE, map_previews.SOURCE), (seed, 255 - seed, 7))


class Logic(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="map_previews.")
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.folder = os.path.join(self.dir, "maps")
        os.mkdir(self.folder)
        self.write("Alpha.lvl", b"aaa")
        self.write("Beta !.LVL", b"bbbb")
        self.write("notes.txt", b"not a map")
        self.out = os.path.join(self.folder, "previews")

    def write(self, name, data):
        with open(os.path.join(self.folder, name), "wb") as f:
            f.write(data)

    def run_update(self, renderer, **kwargs):
        return map_previews.update(self.folder, self.out, renderer, **kwargs)

    def index(self):
        return json.load(open(os.path.join(self.folder, "previews.json"), encoding="utf-8"))

    def test_names(self):
        self.assertEqual(map_previews.picture_name("Alpha.lvl"), "Alpha.png")
        self.assertEqual(map_previews.picture_name("Beta !.LVL"), "Beta !.png")
        self.assertEqual(map_previews.picture_name("a.b.lvl"), "a.b.png")
        self.assertEqual(map_previews.level_files(self.folder), ["Alpha.lvl", "Beta !.LVL"])

    def test_the_hash_is_the_one_of_maps_json(self):
        self.assertEqual(map_previews.file_hash(os.path.join(self.folder, "Alpha.lvl")), map_previews.fnv1a64(b"aaa"))

    def test_first_run_draws_every_map_and_writes_the_index(self):
        fake = FakeRenderer()
        drawn, skipped, removed = self.run_update(fake)
        self.assertEqual((drawn, skipped, removed), (["Alpha.lvl", "Beta !.LVL"], [], []))
        self.assertEqual(self.index(), {
            "Alpha.lvl": {"hash": map_previews.fnv1a64(b"aaa"), "picture": "Alpha.png"},
            "Beta !.LVL": {"hash": map_previews.fnv1a64(b"bbbb"), "picture": "Beta !.png"}})
        for name in ("Alpha.png", "Beta !.png"):
            picture = Image.open(os.path.join(self.out, name))
            self.assertEqual((picture.format, picture.size), ("PNG", (map_previews.SIZE, map_previews.SIZE)))
            self.assertLessEqual(len(picture.getpalette()) // 3, 256)

    def test_second_run_draws_nothing(self):
        self.run_update(FakeRenderer())
        fake = FakeRenderer()
        self.assertEqual(self.run_update(fake), ([], ["Alpha.lvl", "Beta !.LVL"], []))
        self.assertEqual(fake.calls, [])

    def test_the_pictures_are_the_same_every_time(self):
        self.run_update(FakeRenderer())
        first = open(os.path.join(self.out, "Alpha.png"), "rb").read()
        self.run_update(FakeRenderer(), force=True)
        self.assertEqual(open(os.path.join(self.out, "Alpha.png"), "rb").read(), first)

    def test_a_changed_map_is_drawn_again_and_only_it(self):
        self.run_update(FakeRenderer())
        self.write("Alpha.lvl", b"repaired")
        fake = FakeRenderer()
        self.assertEqual(self.run_update(fake), (["Alpha.lvl"], ["Beta !.LVL"], []))
        self.assertEqual(self.index()["Alpha.lvl"]["hash"], map_previews.fnv1a64(b"repaired"))

    def test_a_missing_picture_is_drawn_again(self):
        self.run_update(FakeRenderer())
        os.remove(os.path.join(self.out, "Beta !.png"))
        self.assertEqual(self.run_update(FakeRenderer())[0], ["Beta !.LVL"])

    def test_a_new_map_is_added_and_a_gone_map_loses_its_picture(self):
        self.run_update(FakeRenderer())
        self.write("Gamma.lvl", b"g")
        os.remove(os.path.join(self.folder, "Alpha.lvl"))
        with open(os.path.join(self.out, "keep.png"), "wb") as f:
            f.write(b"not listed in the index: not touched")
        drawn, skipped, removed = self.run_update(FakeRenderer())
        self.assertEqual((drawn, skipped, removed), (["Gamma.lvl"], ["Beta !.LVL"], ["Alpha.png"]))
        self.assertEqual(sorted(os.listdir(self.out)), ["Beta !.png", "Gamma.png", "keep.png"])
        self.assertEqual(sorted(self.index()), ["Beta !.LVL", "Gamma.lvl"])

    def test_an_interrupted_run_goes_on(self):
        fake = FakeRenderer(fail_on="Beta !.LVL")
        with self.assertRaises(RuntimeError):
            self.run_update(fake)
        self.assertEqual(sorted(self.index()), ["Alpha.lvl"])             # what was done is kept
        again = FakeRenderer()
        self.assertEqual(self.run_update(again), (["Beta !.LVL"], ["Alpha.lvl"], []))
        self.assertEqual(again.calls, ["Beta !.LVL"])

    def test_force_draws_everything(self):
        self.run_update(FakeRenderer())
        self.assertEqual(self.run_update(FakeRenderer(), force=True)[0], ["Alpha.lvl", "Beta !.LVL"])

    def test_a_damaged_or_odd_index_means_draw_everything(self):
        for text in ("{not json", "[]", '{"Alpha.lvl": 3, "Beta !.LVL": {"hash": 1}}'):
            with open(os.path.join(self.folder, "previews.json"), "w") as f:
                f.write(text)
            self.assertEqual(self.run_update(FakeRenderer())[0], ["Alpha.lvl", "Beta !.LVL"], text)
        os.remove(os.path.join(self.folder, "previews.json"))
        self.assertEqual(map_previews.read_index(os.path.join(self.folder, "previews.json")), {})

    def test_parallel_runs_give_the_same_result(self):
        for i in range(12):
            self.write("Map%d.lvl" % i, b"x" * i)
        self.run_update(FakeRenderer(), jobs=4)
        parallel = (self.index(), sorted(os.listdir(self.out)))
        self.run_update(FakeRenderer(), force=True)
        self.assertEqual((self.index(), sorted(os.listdir(self.out))), parallel)
        self.assertEqual(len(parallel[0]), 14)

    def test_a_preview_of_the_wrong_size_is_refused(self):
        with self.assertRaises(ValueError):
            map_previews.encode(Image.new("RGB", (299, 300)))


class TheFolder(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(FOLDER, "maps.json"), encoding="utf-8") as f:
            cls.maps = {m["file"]: m for m in json.load(f)["maps"]}
        with open(os.path.join(FOLDER, "previews.json"), encoding="utf-8") as f:
            cls.index = json.load(f)

    def test_the_index_lists_exactly_the_maps_of_maps_json_with_their_hashes(self):
        self.assertEqual(sorted(self.index), sorted(self.maps))
        for file, entry in self.index.items():
            self.assertEqual(sorted(entry), ["hash", "picture"], file)
            self.assertEqual(entry["hash"], self.maps[file]["hash"], file)
            self.assertEqual(entry["picture"], map_previews.picture_name(file), file)

    def test_every_picture_exists_is_a_png_of_the_expected_size_and_nothing_else_is_there(self):
        folder = os.path.join(FOLDER, "previews")
        self.assertEqual(sorted(os.listdir(folder)), sorted(e["picture"] for e in self.index.values()))
        for entry in self.index.values():
            with Image.open(os.path.join(folder, entry["picture"])) as picture:
                self.assertEqual((picture.format, picture.size), ("PNG", (map_previews.SIZE, map_previews.SIZE)), entry["picture"])

    def test_the_pictures_are_small(self):
        folder = os.path.join(FOLDER, "previews")
        self.assertLess(sum(os.path.getsize(os.path.join(folder, n)) for n in os.listdir(folder)), 8 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
