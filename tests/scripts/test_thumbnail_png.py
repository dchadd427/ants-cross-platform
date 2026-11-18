#!/usr/bin/env python3
"""The pictures of the players' maps, read by a decoder that the repository did not write (run by ./run_tests.sh --fast, where the game program is built).

ants_thumb (src/ants_thumb/main.cpp) draws a map file as the setup screen does and writes a PNG with the repository's own deflate (src/ants_app/png_encode.cpp). The C++ tests read that PNG back with
the repository's own reader; this test reads it with the zlib of Python, which is the reference, and looks at what the picture is: 300 x 300 pixels, a frame whose CRC-32 checks are right, rows that unfilter,
the floor of the setup screen's box (7, 11, 15) round a map that is not square, and, where Pillow is there, a picture that is close to the library's own picture of the same map (the library's are lossy WebP
files that the game made of the same screen: a peak signal-to-noise ratio of 30 dB or more).

The program is found at $ANTS_THUMB, else build/src/ants_thumb/ants_thumb (or in $BUILD_DIR); a checkout that has not built it skips these tests.
"""
import io
import math
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

try:
    from PIL import Image
except ImportError:
    Image = None

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHD = os.path.join(REPO, "Original-Ants", "ants.chd")
MAPS = os.path.join(REPO, "Community-Maps")
FLOOR = (7, 11, 15)


def program():
    for candidate in (os.environ.get("ANTS_THUMB"), os.path.join(REPO, os.environ.get("BUILD_DIR", "build"), "src", "ants_thumb", "ants_thumb")):
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def read_png(data):
    """(width, height, rows of RGB bytes) of an 8-bit RGB PNG made of IHDR, IDAT and IEND; every CRC-32 and the zlib stream are checked."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "the signature"
    pos, chunks = 8, []
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        (crc,) = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])
        assert crc == zlib.crc32(kind + body) & 0xFFFFFFFF, "the CRC-32 of " + kind.decode()
        chunks.append((kind, body))
        pos += 12 + length
    assert pos == len(data) and [c[0] for c in chunks] == [b"IHDR", b"IDAT", b"IEND"], "the chunks"
    width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", chunks[0][1])
    assert (depth, color, compression, filtering, interlace) == (8, 2, 0, 0, 0), "8-bit RGB, no interlace"
    raw = zlib.decompress(chunks[1][1])
    stride = width * 3
    assert len(raw) == (stride + 1) * height, "one filter byte and a row for every row"
    rows, previous = [], bytearray(stride)
    for y in range(height):
        filter_type, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b = previous[i]
            c = previous[i - 3] if i >= 3 else 0
            if filter_type == 1:
                line[i] = (line[i] + a) & 255
            elif filter_type == 2:
                line[i] = (line[i] + b) & 255
            elif filter_type == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif filter_type == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            else:
                assert filter_type == 0, "a filter of the PNG standard"
        rows.append(line)
        previous = line
    return width, height, rows


@unittest.skipUnless(program(), "ants_thumb is not built")
class ThePictureOfAMap(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(prefix="thumbnail_png.")
        cls.addClassCleanup(cls.dir.cleanup)

    def draw(self, name):
        out = os.path.join(self.dir.name, name.replace(" ", "_") + ".png")
        run = subprocess.run([program(), "--chd", CHD, "--out", out, os.path.join(MAPS, name + ".lvl")], capture_output=True, text=True, timeout=120, cwd=self.dir.name)
        self.assertEqual(run.returncode, 0, run.stderr)
        with open(out, "rb") as f:
            return f.read()

    def test_a_square_map_fills_the_box_and_the_file_is_a_good_png(self):
        data = self.draw("Battle Islands")
        width, height, rows = read_png(data)
        self.assertEqual((width, height), (300, 300))
        self.assertLess(len(data), 120000)
        self.assertGreater(len({bytes(r[i:i + 3]) for r in rows[::10] for i in range(0, 900, 30)}), 20, "a picture with many colors, not a flat one")
        self.assertEqual(self.draw("Battle Islands"), data, "the same map, the same bytes")

    def test_a_map_that_is_not_square_is_centred_on_the_floor_of_the_box(self):
        width, height, rows = read_png(self.draw("SWAMP"))                  # 60 columns, 40 rows: 300 x 200 in the middle
        self.assertEqual((width, height), (300, 300))
        for y in list(range(0, 50)) + list(range(250, 300)):
            self.assertTrue(all(tuple(rows[y][i:i + 3]) == FLOOR for i in range(0, 900, 3)), "row %d is the floor" % y)
        self.assertGreater(len({tuple(rows[150][i:i + 3]) for i in range(0, 900, 3)}), 10, "the picture is in the middle")

    @unittest.skipUnless(Image, "Pillow is needed")
    def test_the_picture_is_the_librarys_own_picture_of_the_map(self):
        for name in ("Battle Islands", "SWAMP"):
            ours = Image.open(io.BytesIO(self.draw(name))).convert("RGB")
            library = Image.open(os.path.join(MAPS, "previews", name + ".webp")).convert("RGB")
            self.assertEqual(ours.size, library.size)
            a, b = ours.tobytes(), library.tobytes()
            mse = sum((x - y) ** 2 for x, y in zip(a, b)) / len(a)
            self.assertGreater(10 * math.log10(255 * 255 / mse), 30, name + " against its picture in the library")


if __name__ == "__main__":
    unittest.main()
