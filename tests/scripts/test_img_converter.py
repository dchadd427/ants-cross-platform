#!/usr/bin/env python3
"""The image converter (imgconv/convert.py, imgconv/server.py; docs/HUB_IMAGES.md, "The converter"): what comes out of an uploaded image is new, small and clean, and what cannot be made safe is refused.
It runs the REAL Pillow, so it needs Pillow: where Pillow is missing the tests are skipped with a loud line, except where ANTS_REQUIRE_PILLOW=1 (the CI's Linux leg), where a missing Pillow is a failure.
Every picture here is made by the test (a made-up location, a made-up camera); nothing is read from a file.

  TheMetadata      EXIF (a location, a camera, a time), color profile, text and comment chunks, XMP are not in what comes out, in the bytes as well as in what Pillow reads; the orientation is applied
  ThePolyglots     a PNG that is also a ZIP, a GIF with a script in its comment, a JPEG with a payload after its end, a page of HTML in front of nothing: what comes out holds none of it
  TheCaps          the pixel cap (from the header, nothing decoded), the side and the bytes of the result, the types (by the first bytes), damaged and truncated files, a decompression bomb
  TheAnimation     a GIF stays animated, with its own timing, within 100 frames and 15 seconds and 400 px; an animated PNG or WebP is its first frame
  TheServer        the door: a good image, a refused one, the lengths, a conversion that hangs or dies is one refused image
"""
import http.client
import io
import json
import multiprocessing
import os
import random
import struct
import sys
import threading
import time
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "..", "imgconv")))
try:
    from PIL import Image
    import convert
    import server
    HAVE = True
except ImportError:
    HAVE = False
    if os.environ.get("ANTS_REQUIRE_PILLOW") == "1":
        raise
    print("SKIP: Pillow is not installed: the image converter was NOT tested (the CI's Linux leg requires it: ANTS_REQUIRE_PILLOW=1)")

needs_pillow = unittest.skipUnless(HAVE, "Pillow is not installed")
EXIF_CAMERA = "Canary Cam 9000"
LOCATION = (37.7749, -122.4194)


def noise(size, seed=1):
    """Pixels that do not compress (so that an image is photo-like and is a JPEG, not a PNG)."""
    state, out = seed, bytearray(size[0] * size[1] * 3)
    for i in range(0, len(out), 3):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        out[i], out[i + 1], out[i + 2] = state >> 16 & 255, state >> 8 & 255, state & 255
    return Image.frombytes("RGB", size, bytes(out))


def gradient(size):
    im = Image.new("RGB", size)
    px = im.load()
    for y in range(size[1]):
        for x in range(size[0]):
            px[x, y] = (x * 255 // max(1, size[0] - 1), y * 255 // max(1, size[1] - 1), (x + y) * 255 // max(1, size[0] + size[1] - 2))
    return im


def save(im, fmt, **kw):
    out = io.BytesIO()
    im.save(out, fmt, **kw)
    return out.getvalue()


def png_chunks(data):
    """[(type, length)] of a PNG, strictly: the signature, whole chunks whose CRCs match, and nothing after IEND."""
    assert data.startswith(b"\x89PNG\r\n\x1a\n")
    at, chunks = 8, []
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        crc = struct.unpack(">I", data[at + 8 + n:at + 12 + n])[0]
        assert zlib.crc32(kind + body) == crc, "a chunk with a wrong CRC"
        chunks.append((kind.decode("ascii"), n))
        at += 12 + n
        if kind == b"IEND":
            break
    assert at == len(data), "bytes after IEND"
    return chunks


def jpeg_markers(data):
    """The marker bytes of a JPEG up to the start of its scan, and whether it ends at EOI with nothing after."""
    assert data[:2] == b"\xff\xd8"
    at, markers = 2, []
    while at < len(data):
        assert data[at] == 0xFF
        marker = data[at + 1]
        markers.append(marker)
        if marker == 0xDA:
            break
        at += 2 + struct.unpack(">H", data[at + 2:at + 4])[0]
    return markers, data.endswith(b"\xff\xd9")


def exif_jpeg(size=(80, 40), orientation=1):
    exif = Image.Exif()
    exif[0x010F] = "Canary Maker"                           # make
    exif[0x0110] = EXIF_CAMERA                              # model
    exif[0x0132] = "2026:11:20 10:42:00"                    # time
    exif[0x0112] = orientation
    gps = exif.get_ifd(0x8825)
    gps[1], gps[2], gps[3], gps[4] = "N", (37.0, 46.0, 29.64), "W", (122.0, 25.0, 9.84)
    return save(gradient(size), "JPEG", exif=exif.tobytes(), quality=90, comment=b"canary comment: nobody@example.org")


@needs_pillow
class TheMetadata(unittest.TestCase):
    def test_exif_with_a_location_a_camera_and_a_time_is_gone_from_the_bytes_and_from_what_pillow_reads(self):
        source = exif_jpeg()
        self.assertIn(b"Exif", source)
        self.assertIn(EXIF_CAMERA.encode(), source)
        self.assertIn(b"canary comment", source)
        r = convert.convert(source)
        for needle in (b"Exif", EXIF_CAMERA.encode(), b"Canary Maker", b"2026:11:20", b"GPS", b"canary comment", b"example.org", b"http://ns.adobe.com", b"ICC_PROFILE"):
            for data in (r.full, r.thumb):
                self.assertNotIn(needle, data, needle)
        for data in (r.full, r.thumb):
            im = Image.open(io.BytesIO(data))
            self.assertEqual(len(im.getexif()), 0)
            self.assertNotIn("icc_profile", im.info)
            self.assertNotIn("comment", im.info)

    def test_a_jpeg_that_comes_out_has_no_marker_that_can_carry_anything(self):
        for source in (exif_jpeg(), save(noise((90, 60)), "JPEG", quality=85, comment=b"hello", icc_profile=b"\x00" * 200)):
            r = convert.convert(source)
            for data in (r.full, r.thumb):
                markers, clean_end = jpeg_markers(data)
                self.assertTrue(clean_end, "nothing after the end of the image")
                self.assertTrue(set(markers) <= {0xE0, 0xDB, 0xC0, 0xC2, 0xC4, 0xDA}, "markers: %s" % [hex(m) for m in markers])      # (APP0, tables, the frame, the scan: no APP1 to APP15, no comment)

    def test_a_png_that_comes_out_has_only_the_chunks_of_its_pixels(self):
        from PIL import PngImagePlugin
        info = PngImagePlugin.PngInfo()
        info.add_text("Author", "canary author")
        info.add_itxt("Description", "canary description")
        im = Image.new("RGBA", (40, 30), (10, 20, 30, 128))
        source = save(im, "PNG", pnginfo=info, icc_profile=b"\x00" * 60, exif=Image.Exif().tobytes())
        self.assertIn(b"canary author", source)
        r = convert.convert(source)
        self.assertEqual(r.x, "png")
        self.assertNotIn(b"canary", r.full)
        self.assertTrue({k for k, _ in png_chunks(r.full)} <= {"IHDR", "IDAT", "IEND", "PLTE", "tRNS"}, png_chunks(r.full))

    def test_the_orientation_is_applied_before_the_metadata_that_says_it_is_dropped(self):
        r = convert.convert(exif_jpeg(size=(80, 40), orientation=6))            # 6: the picture is to be turned a quarter: 80 wide and 40 high shows as 40 wide and 80 high
        self.assertEqual((r.w, r.h), (40, 80))
        self.assertEqual(Image.open(io.BytesIO(r.full)).size, (40, 80))
        self.assertEqual((r.tw, r.th), (40, 80))

    def test_a_profile_of_a_png_or_a_webp_is_not_kept_either(self):
        for fmt in ("PNG", "WEBP"):
            source = save(gradient((50, 40)), fmt, icc_profile=b"canary-profile" + b"\x00" * 100)
            r = convert.convert(source)
            self.assertNotIn(b"canary-profile", r.full)


@needs_pillow
class ThePolyglots(unittest.TestCase):
    ZIP = b"PK\x03\x04canary-zip-payload" + b"\x00" * 40 + b"PK\x05\x06" + b"\x00" * 18
    PAGE = b"<html><script>alert('canary')</script></html>"

    def test_a_png_that_is_also_a_zip_comes_out_as_a_plain_png(self):
        source = save(Image.new("RGB", (30, 30), (1, 2, 3)), "PNG") + self.ZIP
        r = convert.convert(source)
        self.assertNotIn(b"canary-zip-payload", r.full)
        self.assertNotIn(b"PK\x03\x04", r.full)
        png_chunks(r.full)                                       # (whole chunks, and nothing after IEND)

    def test_a_gif_with_a_script_in_its_comment_comes_out_without_it(self):
        source = save(Image.new("P", (30, 30), 1), "GIF", comment=b"<script>alert('canary')</script>")
        self.assertIn(b"<script>", source)
        r = convert.convert(source)
        self.assertNotIn(b"<script>", r.full)
        self.assertNotIn(b"canary", r.full)
        self.assertNotIn(b"canary", r.thumb)

    def test_a_jpeg_with_a_payload_after_its_end_comes_out_with_nothing_after(self):
        source = save(noise((60, 40)), "JPEG", quality=80) + self.PAGE + self.ZIP
        r = convert.convert(source)
        for data in (r.full, r.thumb):
            self.assertNotIn(b"<script>", data)
            self.assertNotIn(b"PK\x03\x04", data)
            self.assertTrue(jpeg_markers(data)[1])

    def test_a_file_that_starts_like_a_png_and_holds_a_page_is_refused_and_one_that_is_a_page_is_not_a_type(self):
        with self.assertRaises(convert.ConvertError) as caught:
            convert.convert(b"\x89PNG\r\n\x1a\n" + self.PAGE)
        self.assertEqual(caught.exception.code, "bad")
        with self.assertRaises(convert.ConvertError) as caught:
            convert.convert(self.PAGE)
        self.assertEqual(caught.exception.code, "type")


@needs_pillow
class TheCaps(unittest.TestCase):
    def refused(self, data):
        with self.assertRaises(convert.ConvertError) as caught:
            convert.convert(data)
        return caught.exception.code

    def test_only_png_jpeg_gif_and_webp_by_their_first_bytes(self):
        for name, data in (("svg", b'<svg xmlns="http://www.w3.org/2000/svg"><script>1</script></svg>'), ("html", b"<!doctype html>"), ("bmp", b"BM" + b"\x00" * 60), ("tiff", b"II*\x00" + b"\x00" * 60),
                           ("ico", b"\x00\x00\x01\x00" + b"\x00" * 60), ("empty", b""), ("zip", b"PK\x03\x04" + b"\x00" * 60), ("riff-not-webp", b"RIFF\x00\x00\x00\x00WAVE" + b"\x00" * 30)):
            self.assertEqual(self.refused(data), "type", name)

    def test_the_four_types_are_accepted(self):
        im = gradient((64, 48))
        for fmt in ("PNG", "JPEG", "GIF", "WEBP"):
            r = convert.convert(save(im, fmt))
            self.assertIn(r.x, ("jpg", "png"), fmt)

    def test_the_pixel_cap_is_read_from_the_header_and_a_bomb_is_refused_before_anything_is_decoded(self):
        small = save(Image.new("RGB", (10, 10)), "PNG")
        head = small[16:24]
        bomb = bytearray(small)
        bomb[16:24] = struct.pack(">II", 30000, 30000)                       # (a 10-pixel file that says it is 900 megapixels)
        crc = zlib.crc32(b"IHDR" + bytes(bomb[16:29]))
        bomb[29:33] = struct.pack(">I", crc)
        self.assertNotEqual(head, bytes(bomb[16:24]))
        started = time.monotonic()
        self.assertEqual(self.refused(bytes(bomb)), "pixels")
        self.assertLess(time.monotonic() - started, 2.0, "refused from the header, with nothing decoded")

    def test_24_megapixels_pass_and_one_more_row_does_not(self):
        ok = save(Image.new("L", (6000, 4000), 7), "PNG")                    # (exactly 24,000,000)
        r = convert.convert(ok)
        self.assertLessEqual(max(r.w, r.h), convert.MAX_SIDE)
        over = save(Image.new("L", (6001, 4000), 7), "PNG")
        self.assertEqual(self.refused(over), "pixels")

    def test_a_big_image_is_made_to_fit_in_2048_on_its_longer_side_and_in_1_5_mib(self):
        r = convert.convert(save(noise((2600, 1500), 7), "PNG"))
        self.assertEqual(max(r.w, r.h), 2048)
        self.assertEqual((r.w, r.h), (2048, round(1500 * 2048 / 2600)))
        self.assertLessEqual(len(r.full), 1_572_864, "1.5 MiB: the number itself, not the file's name for it")
        self.assertEqual(r.x, "jpg", "a picture of noise is a JPEG")
        im = Image.open(io.BytesIO(r.full))
        self.assertEqual(im.size, (r.w, r.h))

    def test_a_drawing_that_is_too_big_as_a_png_is_not_kept_as_one(self):
        """Few colors make a PNG the choice, but 200 colors in no order do not compress: the file that is kept is never over 1.5 MiB, whatever its type."""
        size = (1800, 1800)
        pixels = random.Random(5).randbytes(size[0] * size[1]).translate(bytes(i % 200 for i in range(256)))
        drawing = Image.frombytes("P", size, pixels)
        drawing.putpalette([c for i in range(200) for c in (i, 255 - i, (i * 7) % 256)])
        data = save(drawing, "PNG")
        self.assertGreater(len(data), 1_572_864, "the test's own picture is a PNG that is too big")
        r = convert.convert(data)
        self.assertLessEqual(len(r.full), 1_572_864)

    def test_the_thumbnail_is_a_jpeg_of_360_on_its_longer_side_and_the_image_is_not_made_larger(self):
        r = convert.convert(save(gradient((700, 300)), "PNG"))
        self.assertEqual(Image.open(io.BytesIO(r.thumb)).format, "JPEG")
        self.assertEqual((r.tw, r.th), (360, round(300 * 360 / 700)))
        small = convert.convert(save(gradient((100, 50)), "PNG"))
        self.assertEqual((small.w, small.h, small.tw, small.th), (100, 50, 100, 50))

    def test_transparency_is_kept_in_a_png_and_a_flat_picture_with_few_colors_is_a_png(self):
        r = convert.convert(save(Image.new("RGBA", (30, 30), (255, 0, 0, 90)), "PNG"))
        self.assertEqual(r.x, "png")
        self.assertEqual(Image.open(io.BytesIO(r.full)).convert("RGBA").getpixel((3, 3))[3], 90)
        flat = convert.convert(save(Image.new("RGB", (64, 64), (3, 4, 5)), "JPEG"))
        self.assertEqual(flat.x, "png")
        thumb = Image.open(io.BytesIO(r.thumb))
        self.assertEqual(thumb.mode, "RGB", "a thumbnail has no transparency: it is laid over the dock's dark color")

    def test_damaged_truncated_and_empty_files_are_bad_and_say_nothing_of_the_file(self):
        good = save(noise((80, 60)), "JPEG")
        for data in (good[: len(good) // 2], good[:20], b"\xff\xd8\xff" + b"\x00" * 30, save(noise((20, 20)), "PNG")[:40], b"GIF89a" + b"\x00" * 5, b"RIFF\x04\x00\x00\x00WEBP"):
            self.assertEqual(self.refused(data), "bad")

    def test_an_error_is_one_word_and_never_the_decoders_own_text(self):
        try:
            convert.convert(b"\xff\xd8\xff" + b"\x00" * 30)
        except convert.ConvertError as e:
            self.assertEqual(str(e), "bad")
            self.assertEqual(e.code, "bad")


@needs_pillow
class TheAnimation(unittest.TestCase):
    @staticmethod
    def gif(frames=6, size=(120, 80), duration=100, extra=None):
        ims = [Image.new("RGB", size, (i * 30 % 255, 80, 160)) for i in range(frames)]
        kw = dict(save_all=True, append_images=ims[1:], duration=duration, loop=0)
        kw.update(extra or {})
        return save(ims[0], "GIF", **kw)

    def test_an_animated_gif_stays_a_gif_with_its_frames_and_its_timing(self):
        r = convert.convert(self.gif(6, duration=120))
        self.assertEqual((r.x, r.anim, r.still), ("gif", True, False))
        im = Image.open(io.BytesIO(r.full))
        self.assertEqual(im.format, "GIF")
        self.assertEqual(im.n_frames, 6)
        total = 0
        for i in range(im.n_frames):
            im.seek(i)
            total += im.info["duration"]
        self.assertEqual(total, 720)
        self.assertEqual(Image.open(io.BytesIO(r.thumb)).format, "JPEG", "the still that the log shows while it is off screen, paused or switched off")

    def test_the_metadata_of_a_gif_is_dropped_but_its_loop_is_kept(self):
        r = convert.convert(self.gif(4, extra={"comment": b"canary comment <script>"}))
        self.assertNotIn(b"canary", r.full)
        self.assertNotIn(b"<script>", r.full)
        self.assertEqual(Image.open(io.BytesIO(r.full)).info.get("loop"), 0)

    def test_a_big_animation_is_made_400_pixels_on_its_longer_side_and_fits_in_1_5_mib(self):
        r = convert.convert(self.gif(5, size=(900, 500)))
        self.assertEqual(max(r.w, r.h), 400)
        self.assertLessEqual(len(r.full), 1_572_864)

    def test_more_than_100_frames_or_15_seconds_is_refused_with_frames(self):
        for data in (self.gif(101, size=(20, 20), duration=20), self.gif(10, size=(20, 20), duration=1600)):
            with self.assertRaises(convert.ConvertError) as caught:
                convert.convert(data)
            self.assertEqual(caught.exception.code, "frames")

    def test_exactly_100_frames_and_exactly_15_seconds_pass(self):
        r = convert.convert(self.gif(100, size=(20, 20), duration=150))
        self.assertTrue(r.anim)
        self.assertEqual(Image.open(io.BytesIO(r.full)).n_frames, 100)

    def test_a_frame_that_says_less_than_20_ms_is_written_as_at_least_20(self):
        r = convert.convert(self.gif(3, size=(20, 20), duration=10))
        im = Image.open(io.BytesIO(r.full))
        for i in range(im.n_frames):
            im.seek(i)
            self.assertGreaterEqual(im.info["duration"], 20)

    def test_a_gif_of_one_frame_is_a_still_image(self):
        r = convert.convert(self.gif(1))
        self.assertEqual((r.anim, r.still), (False, False))
        self.assertIn(r.x, ("png", "jpg"))

    def test_an_animated_webp_or_png_is_its_first_frame_and_says_so(self):
        ims = [Image.new("RGB", (60, 40), (i * 40, 10, 10)) for i in range(4)]
        for fmt in ("WEBP", "PNG"):
            try:
                data = save(ims[0], fmt, save_all=True, append_images=ims[1:], duration=100, loop=0)
            except Exception:
                continue                                                  # (a Pillow without that codec)
            if getattr(Image.open(io.BytesIO(data)), "n_frames", 1) < 2:
                continue
            r = convert.convert(data)
            self.assertEqual((r.anim, r.still), (False, True), fmt)
            self.assertIn(r.x, ("png", "jpg"))

    def test_the_work_of_a_big_animation_is_capped(self):
        with self.assertRaises(convert.ConvertError) as caught:
            convert.convert(self.gif(100, size=(1600, 1000), duration=100))      # 100 x 1.6 million pixels
        self.assertIn(caught.exception.code, ("pixels", "bad", "big"))


@needs_pillow
class TheServer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.httpd = server.make("127.0.0.1", 0)
        cls.port = cls.httpd.server_address[1]
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.thread.join(5)

    def post(self, body, path="/convert", headers=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
        c.request("POST", path, body=body, headers=headers or {})
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, r.getheader("Content-Type"), data

    def test_a_good_image_is_a_line_of_json_and_then_the_two_files(self):
        status, kind, data = self.post(save(gradient((300, 200)), "PNG"))
        self.assertEqual((status, kind), (200, "application/octet-stream"))
        line, _, rest = data.partition(b"\n")
        head = json.loads(line)
        self.assertEqual(set(head), {"x", "w", "h", "tw", "th", "anim", "still", "full", "thumb"})
        self.assertEqual(head["full"] + head["thumb"], len(rest))
        self.assertEqual(Image.open(io.BytesIO(rest[: head["full"]])).size, (head["w"], head["h"]))
        self.assertEqual(Image.open(io.BytesIO(rest[head["full"]:])).format, "JPEG")

    def test_a_refused_image_is_a_422_with_one_word(self):
        for body, code in ((b"not an image at all", "type"), (save(noise((50, 50)), "JPEG")[:100], "bad")):
            status, kind, data = self.post(body)
            self.assertEqual((status, kind, json.loads(data)), (422, "application/json", {"error": code}))

    def test_the_lengths_the_path_and_the_methods(self):
        self.assertEqual(self.post(b"", headers={"Content-Length": "0"})[0], 400)
        self.assertEqual(self.post(b"x", path="/other")[0], 404)
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.putrequest("POST", "/convert")
        c.putheader("Content-Length", str(server.MAX_BODY + 1))
        c.endheaders()
        self.assertEqual(c.getresponse().status, 413)
        c.close()
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request("GET", "/health")
        self.assertEqual(c.getresponse().status, 200)
        c.close()

    def test_a_conversion_that_hangs_is_killed_and_one_that_dies_is_one_refused_image(self):
        real = convert.convert
        try:
            convert.convert = lambda data: time.sleep(30)
            started = time.monotonic()
            self.assertEqual(server.run_child(b"x", wall=0.5), ("error", "bad"))
            self.assertLess(time.monotonic() - started, 5)
            self.assertEqual(multiprocessing.active_children(), [], "the child that did not finish is gone when the call is over")
            convert.convert = lambda data: os._exit(3)
            self.assertEqual(server.run_child(b"x"), ("error", "bad"))
        finally:
            convert.convert = real
        status, _, _ = self.post(save(gradient((40, 30)), "PNG"))
        self.assertEqual(status, 200, "the server is still there")


if __name__ == "__main__":
    unittest.main()
