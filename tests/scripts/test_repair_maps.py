#!/usr/bin/env python3
"""The repairs of the players' maps (tools/repair_maps.py), on small made-up levels written here byte by byte in the layout of the engine's loader (src/ants_assets/lvl_parser.cpp).

  - the layout reader: a whole level, a level that ends inside block 1, 2 or 3, a block 2 object without stages, what is no version 8 level;
  - undo_text_damage: each of the three damages (a lost 0x00 behind a 0x0D, a 0x0D 0x0A for a 0x0A, a lost 0x0D in front of a 0x0A) made on a good level, in a layer and in the blocks,
    comes back byte for byte; a good level, a level with two equally good readings, one with a reading that costs too much and one that ends inside a block are left alone;
  - fix_start_markers: a coordinate takes its low byte when that is inside the grid, a marker that cannot be placed is dropped, plants and markers inside the grid are not touched;
  - fix_tiles: the neighbours' commonest tile in layer 1 (flags and properties kept), the empty cell in layer 2, the empty tile in block 3, nothing when more than 5 % are such;
  - repair: the order of the repairs, the sentences, a repaired file that is repaired again stays as it is, the result is always a whole level; the program on the command line.
The repaired maps of Community-Maps/ are pinned by test_community_maps.py (a repair of a repaired file finds nothing to do).
"""
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import repair_maps  # noqa: E402

EMPTY = 0x7FFE
NAMES = [".", "water", "grass", "sand", "dirt", "tree", "GSTART", "RSTART", "USTART", "BSTART"] + ["tile%d" % i for i in range(10, 30)]       # tile indexes 0 .. 29
G, R, U, B = 6, 7, 8, 9
GROUND = 2


def w(*words):
    return struct.pack("<%dH" % len(words), *words)


def level(rows=20, cols=20, names=NAMES, l1=None, l2=None, b1=(), b2=(), b3=(0, EMPTY), b4=(), egg=0, mode=1):
    """A version 8 level. l1 / l2: {(row, column): (tile, flags, properties)} over the default cells; b1: [(tile, row, column)]; b2: [(row, column, units, points, [(weight, tile)])];
    b4: [(row, column, flag, 44 bytes or b"")]."""
    out = struct.pack("<II", 8, mode) + w(12) + b"A made-up level".ljust(30, b"\0")
    out += w(len(names) - 1) + b"".join(n.encode().ljust(11, b"\0") for n in names)
    out += struct.pack("<II", rows, cols)
    for layer, default, given in ((1, (GROUND, 0, 0xCDCD), l1 or {}), (2, (EMPTY, 0, 0), l2 or {})):
        for y in range(rows):
            for x in range(cols):
                out += w(*given.get((y, x), default))
    out += w(len(b1)) + b"".join(w(*r) for r in b1)
    out += w(len(b2))
    for y, x, units, points, stages in b2:
        out += w(y, x, units, points, len(stages)) + b"".join(w(*s) for s in stages)
    out += w(*b3) + w(len(b4))
    for y, x, flag, extra in b4:
        out += w(y, x) + struct.pack("<I", flag) + extra
    return out + w(egg)


def damage(data, start, kind):
    """What a text transfer does to the bytes from `start` on (the inverse of what undo_text_damage searches)."""
    head, tail = data[:start], data[start:]
    if kind == "nul":
        tail = tail.replace(b"\r\0", b"\r")
    elif kind == "lf":
        tail = tail.replace(b"\n", b"\r\n")
    elif kind == "cr":
        tail = tail.replace(b"\r\n", b"\n")
    return head + tail


def layout_of(data):
    return repair_maps.layout(data)


class TheLayout(unittest.TestCase):
    def test_a_whole_level(self):
        data = level(rows=12, cols=15, b1=[(G, 1, 2), (R, 3, 4)], b2=[(5, 6, 10, 30, [(10, 2), (0, EMPTY)]), (7, 8, 10, 30, [(10, 3)])], b4=[(1, 1, 0, b""), (2, 2, 1, b"\1" * 44)])
        lay = layout_of(data)
        self.assertIsNone(lay.end)
        self.assertEqual((lay.rows, lay.cols, lay.tiles), (12, 15, len(NAMES)))
        self.assertEqual(lay.names, NAMES)
        self.assertEqual((lay.b1_count, lay.b2_count, lay.b4_count), (2, 2, 2))
        self.assertEqual(lay.objects, [(lay.b2 + 2, 2), (lay.b2 + 2 + 10 + 8, 1)])
        self.assertEqual(lay.layer1 + 12 * 15 * 12, lay.b1)
        self.assertEqual(lay.final, len(data) - 2)
        self.assertFalse(lay.ended_early)

    def test_a_level_that_ends_inside_a_block_says_which(self):
        data = level(b1=[(G, 1, 2), (R, 3, 4)], b2=[(5, 6, 10, 30, [(10, 2), (0, EMPTY)])])
        lay = layout_of(data)
        for cut, where in ((lay.b1, "block 1"), (lay.b1 + 1, "block 1"), (lay.b1 + 2 + 6, "block 1"), (lay.b2, "block 2"), (lay.b2 + 2 + 5, "block 2"), (lay.objects[0][0] + 12, "block 2"),
                           (lay.b3, "block 3"), (lay.b3 + 3, "block 3")):
            self.assertEqual(layout_of(data[:cut]).end, where, cut)
        self.assertIsNone(layout_of(data[:lay.b3 + 4]).end)                       # (block 4 and the egg stock may be missing: the loader goes on)
        self.assertEqual(layout_of(data[:lay.b3 + 4]).final, 0)

    def test_an_object_without_stages_ends_block_2(self):
        data = level(b2=[(5, 6, 10, 30, [(10, 2)]), (7, 8, 10, 30, [])])
        lay = layout_of(data)
        self.assertTrue(lay.ended_early)
        self.assertEqual(len(lay.objects), 1)

    def test_what_is_no_level_gives_none(self):
        good = level()
        self.assertIsNone(layout_of(b""))
        self.assertIsNone(layout_of(good[:30]))
        self.assertIsNone(layout_of(struct.pack("<I", 7) + good[4:]))
        self.assertIsNone(layout_of(b"MZ" + good[2:]))
        self.assertIsNone(layout_of(level(rows=0)))
        self.assertIsNone(layout_of(level(cols=101)))
        self.assertIsNone(layout_of(good[:repair_maps.HEADER_SIZE + 2 + 11 * len(NAMES) + 8 + 100]))        # (the layers are cut)

    def test_a_team_is_known_by_the_loaders_words(self):
        self.assertEqual([repair_maps.team_of(n) for n in ("GSTART", "REDHILL", "xustartx", "bkhill", "BLACKHILL", "ghill")], [0, 1, 2, 3, 3, 0])
        self.assertIsNone(repair_maps.team_of("tree"))
        self.assertIsNone(repair_maps.team_of(""))


class TheTextDamage(unittest.TestCase):
    def good(self):
        """A level whose records hold the words 13 and 10 in every place the damage can reach: the blocks, and a cell of each layer."""
        return level(l1={(3, 4): (13, 0, 0xCDCD), (5, 5): (4, 0x0100, 0xCDCD)}, l2={(2, 2): (5, 1, 13 | 10 << 8), (2, 3): (5, 1, 13 | 11 << 8), (9, 9): (EMPTY, 0, 0x0D0D)},
                     b1=[(G, 13, 5), (R, 6, 13), (U, 10, 4), (B, 13, 10)],
                     b2=[(13, 10, 10, 30, [(10, 2), (13, 3), (0, EMPTY)]), (10, 13, 12, 11, [(10, 4)])], b3=(0, EMPTY), b4=[(13, 5, 0, b""), (10, 6, 0, b"")], egg=13)

    def start_of(self, data):
        return layout_of(data).layer1

    def test_a_good_level_is_left_alone(self):
        self.assertIsNone(repair_maps.undo_text_damage(self.good()))
        self.assertIsNone(repair_maps.undo_text_damage(level()))

    def test_a_0x00_lost_behind_a_0x0d_is_put_back(self):
        data = self.good()
        broken = damage(data, self.start_of(data), "nul")
        self.assertNotEqual(broken, data)
        fixed, nuls, lfs, crs = repair_maps.undo_text_damage(broken)
        self.assertEqual(fixed, data)
        self.assertEqual((lfs, crs), (0, 0))
        self.assertEqual(nuls, len(data) - len(broken))

    def test_a_0x0d_0x0a_for_a_0x0a_is_made_a_0x0a(self):
        data = self.good()
        broken = damage(data, repair_maps.layout(data).b1, "lf")
        self.assertGreater(len(broken), len(data))
        fixed, nuls, lfs, crs = repair_maps.undo_text_damage(broken)
        self.assertEqual(fixed, data)
        self.assertEqual((nuls, lfs, crs), (0, len(broken) - len(data), 0))

    def test_a_0x0d_lost_in_front_of_a_0x0a_is_put_back(self):
        data = self.good()
        self.assertIn(b"\r\n", data[self.start_of(data):])
        broken = damage(data, self.start_of(data), "cr")
        self.assertLess(len(broken), len(data))
        fixed, nuls, lfs, crs = repair_maps.undo_text_damage(broken)
        self.assertEqual(fixed, data)
        self.assertEqual((nuls, lfs, crs), (0, 0, len(data) - len(broken)))

    def test_the_three_damages_together(self):
        data = self.good()
        lay = layout_of(data)
        broken = damage(damage(data, lay.layer1, "nul"), lay.b1 - 1000, "lf")           # (the blocks got both; the layer cells only the first)
        fixed = repair_maps.undo_text_damage(broken)
        self.assertIsNotNone(fixed)
        self.assertEqual(fixed[0], data)
        self.assertGreater(fixed[1], 0)
        self.assertGreater(fixed[2], 0)

    def test_a_damage_in_a_layer_is_found_too(self):
        data = level(l1={(3, 4): (13, 0, 0xCDCD), (3, 5): (13, 0, 0xCDCD)}, l2={(4, 4): (EMPTY, 0, 0x0D0D)})
        broken = damage(data, self.start_of(data), "nul")
        self.assertEqual(len(data) - len(broken), 2)                                    # (the two tiles of 13)
        self.assertEqual(repair_maps.undo_text_damage(broken)[0], data)

    def test_the_result_is_the_same_every_time(self):
        data = self.good()
        broken = damage(data, self.start_of(data), "nul")
        self.assertEqual(repair_maps.undo_text_damage(broken), repair_maps.undo_text_damage(broken))

    def test_two_equally_good_readings_are_not_taken(self):
        data = self.good()
        broken = damage(data, self.start_of(data), "nul")
        one = (0, [(100, "nul")], data)
        two = (0, [(200, "nul")], data[:-1] + b"\1")
        with mock.patch.object(repair_maps, "readings_of_file", return_value=[one, two]):
            self.assertIsNone(repair_maps.undo_text_damage(broken))
        with mock.patch.object(repair_maps, "readings_of_file", return_value=[one, (1, [(200, "nul")], two[2])]):
            self.assertEqual(repair_maps.undo_text_damage(broken)[0], data)               # (one cell of difference is enough)
        with mock.patch.object(repair_maps, "readings_of_file", return_value=[one]):
            self.assertEqual(repair_maps.undo_text_damage(broken)[0], data)

    def test_a_damage_that_the_file_reads_two_ways_is_not_repaired(self):
        # the words 13, 13, 10 in a row: 0x0D 0x0D 0x0A can be read as 13 13 10 with two lost zeros or in another way that is as good: the file stays as it is
        data = level(b1=[(G, 1, 1)], b2=[(10, 13, 13, 10, [(10, 4)])])
        broken = damage(data, self.start_of(data), "nul")
        self.assertEqual(len(repair_maps.readings_of_file(broken)), 2)
        self.assertEqual(repair_maps.readings_of_file(broken)[0][0], repair_maps.readings_of_file(broken)[1][0])
        self.assertIsNone(repair_maps.undo_text_damage(broken))

    def test_a_reading_that_costs_too_much_is_not_taken(self):
        data = self.good()
        broken = damage(data, self.start_of(data), "nul")
        with mock.patch.object(repair_maps, "readings_of_file", return_value=[(repair_maps.MAX_COST + 1, [(1, "nul")], data)]):
            self.assertIsNone(repair_maps.undo_text_damage(broken))
        with mock.patch.object(repair_maps, "readings_of_file", return_value=[(repair_maps.MAX_COST, [(1, "nul")], data)]):
            self.assertEqual(repair_maps.undo_text_damage(broken)[0], data)
        with mock.patch.object(repair_maps, "readings_of_file", return_value=[(0, [], data)]):
            self.assertIsNone(repair_maps.undo_text_damage(broken))                       # (a reading without a change repairs nothing)

    def test_the_cost_counts_the_cells_that_no_map_has(self):
        cell = lambda *words: w(*words)
        self.assertEqual(repair_maps.cell_cost(1, cell(2, 0, 0xCDCD), 20, 20), 0)
        self.assertEqual(repair_maps.cell_cost(1, cell(2, 0xCDCD, 0xCDCD), 20, 20), 0)
        self.assertEqual(repair_maps.cell_cost(1, cell(2, 0x7F00, 0xCDCD), 20, 20), 1)
        self.assertEqual(repair_maps.cell_cost(1, cell(2, 0, 0xFE00), 20, 20), 1)
        self.assertEqual(repair_maps.cell_cost(2, cell(EMPTY, 0, 0x1234), 20, 20), 0)
        self.assertEqual(repair_maps.cell_cost(2, cell(EMPTY, 0x0200, 0), 20, 20), 1)
        self.assertEqual(repair_maps.cell_cost(2, cell(5, 1, 13 | 10 << 8), 20, 20), 0)
        self.assertEqual(repair_maps.cell_cost(2, cell(5, 1, 25 | 10 << 8), 20, 20), 1)          # (the anchor is outside a 20 x 20 grid)
        self.assertEqual(repair_maps.cell_cost(2, cell(5, 0x300, 0), 20, 20), 1)

    def test_a_level_that_ends_inside_a_block_is_not_repaired(self):
        data = self.good()
        lay = layout_of(data)
        for cut in (lay.b1 + 2 + 8, lay.b2 + 2 + 14, lay.b3 + 2):
            self.assertIsNone(repair_maps.undo_text_damage(data[:cut]), cut)

    def test_what_is_no_level_is_not_repaired(self):
        for data in (b"", b"MZ" + b"\0" * 500, level()[:50], struct.pack("<I", 9) + level()[4:], level(rows=0), level(cols=300)):
            self.assertIsNone(repair_maps.undo_text_damage(data))

    def test_a_tile_outside_the_dictionary_is_not_a_reading(self):
        # (the search takes a cell for a cell only when its tile is in the dictionary or empty: garbage is not repaired here, fix_tiles does)
        data = level(l1={(1, 1): (500, 0, 0xCDCD)}, b1=[(G, 13, 5)])
        broken = damage(data, self.start_of(data), "nul")
        self.assertIsNone(repair_maps.undo_text_damage(broken))


class TheStartMarkers(unittest.TestCase):
    def test_a_coordinate_whose_low_byte_is_inside_takes_it(self):
        data = level(b1=[(G, 1, 2), (U, 0x5408, 7), (B, 3, 0x1A05)], b2=[(5, 6, 10, 30, [(10, 2)])], egg=9)
        fixed, moved, dropped = repair_maps.fix_start_markers(data)
        self.assertEqual((moved, dropped), (2, 0))
        self.assertEqual(fixed, level(b1=[(G, 1, 2), (U, 8, 7), (B, 3, 5)], b2=[(5, 6, 10, 30, [(10, 2)])], egg=9))

    def test_both_coordinates_are_looked_at(self):
        data = level(b1=[(R, 0x0305, 0x0404)])
        self.assertEqual(repair_maps.fix_start_markers(data)[0], level(b1=[(R, 5, 4)]))

    def test_a_marker_that_cannot_be_placed_is_dropped(self):
        data = level(b1=[(G, 1, 2), (R, 36, 3), (R, 5, 20), (U, 0x5420, 3), (B, 4, 4)], b2=[(5, 6, 10, 30, [(10, 2)])])
        fixed, moved, dropped = repair_maps.fix_start_markers(data)
        self.assertEqual((moved, dropped), (0, 3))
        self.assertEqual(fixed, level(b1=[(G, 1, 2), (B, 4, 4)], b2=[(5, 6, 10, 30, [(10, 2)])]))        # (the count is 2, nothing else is touched)

    def test_a_marker_on_the_last_cell_is_inside(self):
        self.assertIsNone(repair_maps.fix_start_markers(level(rows=20, cols=20, b1=[(G, 19, 19)])))
        self.assertEqual(repair_maps.fix_start_markers(level(rows=20, cols=20, b1=[(G, 20, 19)]))[2], 1)
        self.assertEqual(repair_maps.fix_start_markers(level(rows=20, cols=30, b1=[(G, 19, 30)]))[2], 1)
        self.assertEqual(repair_maps.fix_start_markers(level(rows=30, cols=30, b1=[(G, 0x0114, 19)]))[0], level(rows=30, cols=30, b1=[(G, 20, 19)]))

    def test_a_plant_outside_the_grid_is_left_alone(self):
        # (only a team's tile is a start marker; the loader and the engine ignore the other records outside the grid)
        self.assertIsNone(repair_maps.fix_start_markers(level(b1=[(5, 0x5408, 7), (G, 1, 1)])))

    def test_a_level_with_nothing_to_do_gives_none(self):
        self.assertIsNone(repair_maps.fix_start_markers(level()))
        self.assertIsNone(repair_maps.fix_start_markers(level(b1=[(G, 1, 1), (R, 2, 2)])))
        self.assertIsNone(repair_maps.fix_start_markers(b"MZ" + b"\0" * 100))
        lay = layout_of(level(b1=[(G, 1, 1)]))
        self.assertIsNone(repair_maps.fix_start_markers(level(b1=[(G, 1, 1)])[:lay.b1 + 2]))        # (cut inside block 1)

    def test_the_bytes_before_and_after_block_1_are_the_same(self):
        data = level(l1={(2, 2): (4, 1, 0)}, b1=[(G, 1, 2), (U, 99, 1)], b2=[(5, 6, 10, 30, [(10, 2)])], b4=[(1, 1, 0, b"")], egg=7)
        fixed = repair_maps.fix_start_markers(data)[0]
        lay = layout_of(data)
        self.assertEqual(fixed[:lay.b1], data[:lay.b1])
        self.assertEqual(fixed[-(len(data) - lay.b2):], data[lay.b2:])


class TheTiles(unittest.TestCase):
    def test_a_layer_1_cell_takes_the_commonest_tile_of_its_neighbours_and_keeps_its_flags(self):
        l1 = {(5, 5): (900, 0x0100, 0x1234), (4, 4): (3, 0, 0xCDCD), (4, 5): (3, 0, 0xCDCD), (4, 6): (4, 0, 0xCDCD), (5, 4): (3, 0, 0xCDCD), (6, 5): (4, 0, 0xCDCD), (6, 6): (3, 0, 0xCDCD)}
        fixed, c1, c2, c3 = repair_maps.fix_tiles(level(l1=l1))
        self.assertEqual((c1, c2, c3), (1, 0, 0))
        l1[(5, 5)] = (3, 0x0100, 0x1234)                      # (four of the eight neighbours have tile 3, two tile 4, two the ground tile 2)
        self.assertEqual(fixed, level(l1=l1))

    def test_a_tie_goes_to_the_lowest_tile(self):
        l1 = {(5, 5): (900, 0, 0), (4, 4): (4, 0, 0), (4, 5): (3, 0, 0), (4, 6): (4, 0, 0), (5, 4): (3, 0, 0), (5, 6): (4, 0, 0), (6, 4): (3, 0, 0), (6, 5): (4, 0, 0), (6, 6): (3, 0, 0)}
        self.assertEqual(repair_maps.fix_tiles(level(l1=l1))[0], level(l1={**l1, (5, 5): (3, 0, 0)}))

    def test_neighbours_that_are_garbage_themselves_do_not_count(self):
        l1 = {(5, 5): (900, 0, 0), (5, 6): (901, 0, 0), (4, 4): (4, 0, 0)}
        fixed = repair_maps.fix_tiles(level(l1=l1))[0]
        self.assertEqual(fixed, level(l1={(5, 5): (GROUND, 0, 0), (5, 6): (GROUND, 0, 0), (4, 4): (4, 0, 0)}))        # (the ground tile 2 has the other seven neighbours)

    def test_a_cell_without_a_good_neighbour_takes_the_commonest_tile_of_the_layer(self):
        l1 = {(y, x): (900, 0, 0) for y in range(0, 3) for x in range(0, 3)}
        l1.update({(10, 10): (4, 0, 0), (10, 11): (4, 0, 0), (10, 12): (4, 0, 0)})
        fixed, c1, _c2, _c3 = repair_maps.fix_tiles(level(l1=l1))
        self.assertEqual(c1, 9)
        self.assertEqual(fixed, level(l1={**{(y, x): (GROUND, 0, 0) for y in range(0, 3) for x in range(0, 3)}, (10, 10): (4, 0, 0), (10, 11): (4, 0, 0), (10, 12): (4, 0, 0)}))

    def test_a_corner_has_three_neighbours(self):
        l1 = {(0, 0): (900, 0, 0), (0, 1): (4, 0, 0), (1, 0): (4, 0, 0), (1, 1): (3, 0, 0)}
        self.assertEqual(repair_maps.fix_tiles(level(l1=l1))[0], level(l1={**l1, (0, 0): (4, 0, 0)}))

    def test_a_layer_2_cell_becomes_the_empty_cell(self):
        l2 = {(2, 3): (900, 220, 10762), (7, 7): (5, 1, 7 | 7 << 8), (8, 8): (0xCDCD, 1, 0)}
        fixed, c1, c2, c3 = repair_maps.fix_tiles(level(l2=l2))
        self.assertEqual((c1, c2, c3), (0, 2, 0))
        self.assertEqual(fixed, level(l2={(7, 7): (5, 1, 7 | 7 << 8)}))

    def test_the_empty_tile_is_no_tile_outside_the_dictionary(self):
        self.assertIsNone(repair_maps.fix_tiles(level(l2={(2, 2): (EMPTY, 0, 0)}, l1={(1, 1): (EMPTY, 0, 0)})))

    def test_block_3_outside_the_dictionary_becomes_the_empty_tile(self):
        for ambient in (4094, 38912, len(NAMES), 0xFFFF):
            fixed, c1, c2, c3 = repair_maps.fix_tiles(level(b3=(0, ambient), b1=[(G, 1, 1)], egg=3))
            self.assertEqual((c1, c2, c3), (0, 0, 1))
            self.assertEqual(fixed, level(b3=(0, EMPTY), b1=[(G, 1, 1)], egg=3))
        for ambient in (EMPTY, 0, 5, len(NAMES) - 1):
            self.assertIsNone(repair_maps.fix_tiles(level(b3=(0, ambient))), ambient)

    def test_all_three_at_once(self):
        fixed, c1, c2, c3 = repair_maps.fix_tiles(level(l1={(5, 5): (900, 0, 0)}, l2={(6, 6): (901, 1, 1)}, b3=(1, 4094)))
        self.assertEqual((c1, c2, c3), (1, 1, 1))
        self.assertEqual(fixed, level(l1={(5, 5): (GROUND, 0, 0)}, b3=(1, EMPTY)))

    def test_more_than_5_percent_of_the_cells_are_not_repaired(self):
        l1 = {(0, x): (900, 0, 0) for x in range(20)}                      # 20 of 400 cells: 5 %, the limit
        self.assertIsNotNone(repair_maps.fix_tiles(level(l1=l1)))
        l1[(1, 0)] = (900, 0, 0)
        self.assertIsNone(repair_maps.fix_tiles(level(l1=l1)))
        half = {(0, x): (900, 0, 0) for x in range(10)}
        half2 = {(1, x): (900, 0, 0) for x in range(11)}
        self.assertIsNone(repair_maps.fix_tiles(level(l1=half, l2=half2)))           # (both layers count)

    def test_a_clean_level_gives_none(self):
        self.assertIsNone(repair_maps.fix_tiles(level()))
        self.assertIsNone(repair_maps.fix_tiles(level(b3=(0, 3))))

    def test_a_level_that_ends_inside_a_block_is_not_touched(self):
        data = level(l1={(5, 5): (900, 0, 0)}, b1=[(G, 1, 1)])
        self.assertIsNone(repair_maps.fix_tiles(data[:layout_of(data).b1 + 4]))
        self.assertIsNone(repair_maps.fix_tiles(b"junk"))

    def test_a_record_of_block_1_or_2_outside_the_dictionary_is_not_invented(self):
        # (a tile there has a meaning that no neighbour tells: the map stays as it is and the sweep keeps it out)
        self.assertIsNone(repair_maps.fix_tiles(level(b1=[(300, 1, 1)])))
        self.assertIsNone(repair_maps.fix_tiles(level(b2=[(5, 6, 10, 30, [(10, 300)])])))


class TheRepair(unittest.TestCase):
    def broken_level(self):
        """Markers outside the grid and tiles outside the dictionary (a text-damaged file has none: the search for the damage reads only what is in the grid and the dictionary)."""
        return level(l1={(5, 5): (900, 0, 0)}, b1=[(G, 13, 5), (U, 0x5408, 3), (B, 36, 3), (R, 10, 4)], b2=[(13, 10, 10, 30, [(10, 2)])], b3=(0, 4094), b4=[(13, 10, 0, b"")], egg=2)

    def test_a_good_level_is_not_repaired(self):
        self.assertEqual(repair_maps.repair(level(b1=[(G, 1, 1)])), (None, []))

    def test_the_markers_and_the_tiles_are_repaired_in_this_order_and_each_says_what_it_did(self):
        fixed, notes = repair_maps.repair(self.broken_level())
        self.assertEqual(notes, ["1 start marker outside the grid put on the cell that the low bytes of its coordinates name", "1 start marker outside the grid dropped",
                                 "tiles outside the dictionary: 1 cell of layer 1 given the tile that most neighbouring cells have, the default ant tile of block 3 set to the empty tile"])
        self.assertEqual(fixed, level(l1={(5, 5): (GROUND, 0, 0)}, b1=[(G, 13, 5), (U, 8, 3), (R, 10, 4)], b2=[(13, 10, 10, 30, [(10, 2)])], b3=(0, EMPTY), b4=[(13, 10, 0, b"")], egg=2))

    def test_the_text_damage_is_undone_first_and_then_the_tiles(self):
        good = level(b1=[(G, 13, 5), (R, 10, 4)], b2=[(13, 10, 10, 30, [(10, 2)])], b3=(0, 4094), b4=[(13, 10, 0, b"")], egg=2)
        damaged = damage(damage(good, layout_of(good).layer1, "nul"), layout_of(good).b1, "lf")
        self.assertNotEqual(damaged, good)
        fixed, notes = repair_maps.repair(damaged)
        self.assertEqual(fixed, level(b1=[(G, 13, 5), (R, 10, 4)], b2=[(13, 10, 10, 30, [(10, 2)])], b3=(0, EMPTY), b4=[(13, 10, 0, b"")], egg=2))
        self.assertEqual(len(notes), 2)
        self.assertTrue(notes[0].startswith("text-transfer damage undone: "), notes)
        self.assertEqual(notes[1], "tiles outside the dictionary: the default ant tile of block 3 set to the empty tile")

    def test_a_repaired_level_is_repaired_no_more(self):
        fixed, _ = repair_maps.repair(self.broken_level())
        self.assertEqual(repair_maps.repair(fixed), (None, []))

    def test_the_result_is_deterministic(self):
        self.assertEqual(repair_maps.repair(self.broken_level()), repair_maps.repair(self.broken_level()))

    def test_plural_and_singular_in_the_sentences(self):
        self.assertEqual(repair_maps.plural(1, "zero byte"), "1 zero byte")
        self.assertEqual(repair_maps.plural(2, "zero byte"), "2 zero bytes")
        self.assertEqual(repair_maps.plural(0, "cell"), "0 cells")
        self.assertEqual(repair_maps.plural(2, "0x0D 0x0A pair", "pairs"), "2 pairs")
        data = level(l1={(5, 5): (900, 0, 0), (7, 7): (901, 0, 0)})
        self.assertIn("2 cells of layer 1", repair_maps.repair(data)[1][0])
        self.assertIn("1 cell of layer 1", repair_maps.repair(level(l1={(5, 5): (900, 0, 0)}))[1][0])

    def test_a_file_that_is_no_level_gives_nothing(self):
        for data in (b"", b"MZ" + b"\0" * 5000, b"\1" * 300):
            self.assertEqual(repair_maps.repair(data), (None, []))

    def test_a_level_that_ends_inside_a_block_stays_as_it_is(self):
        data = level(b1=[(G, 1, 1), (R, 2, 2)], l1={(5, 5): (900, 0, 0)})
        cut = data[:layout_of(data).b1 + 2 + 6 + 3]
        self.assertEqual(repair_maps.repair(cut), (None, []))

    def test_the_result_is_a_whole_level(self):
        # a repair that leaves the file cut would not be kept: tiles are repaired in a level that ends inside block 2
        data = level(b1=[(G, 1, 1)], b2=[(5, 6, 10, 30, [(10, 2), (10, 3)])], b3=(0, 4094))
        lay = layout_of(data)
        self.assertEqual(repair_maps.repair(data[:lay.objects[0][0] + 12]), (None, []))

    def test_the_program_prints_one_line_per_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            bad = os.path.join(tmp, "bad.lvl")
            good = os.path.join(tmp, "good.lvl")
            with open(bad, "wb") as f:
                f.write(level(b3=(0, 4094)))
            with open(good, "wb") as f:
                f.write(level())
            done = subprocess.run([sys.executable, os.path.join(REPO, "tools", "repair_maps.py"), bad, good], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0)
        self.assertEqual(done.stdout.splitlines(), ["%s: tiles outside the dictionary: the default ant tile of block 3 set to the empty tile" % bad, "%s: nothing to repair" % good])
        self.assertEqual(done.stderr, "")


if __name__ == "__main__":
    unittest.main()
