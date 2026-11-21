"""The image converter's work (docs/HUB_IMAGES.md): the bytes of an uploaded image in, the bytes of two NEW files out (the image and a small thumbnail), or an error code.

Every byte that comes out was written by the encoder in this file: nothing of the original is kept, so there is nothing to hide in. The output is built from the decoded pixels only (a fresh
image that has no `info`): no EXIF (so no location, camera or time), no color profile, no XMP, no comment or text chunk, no embedded thumbnail, nothing after the end of the file. A polyglot (a
PNG that is also a ZIP, a GIF with a script in its comment, a JPEG with a payload after its end marker) comes out as a plain image.

  still image   PNG when it has transparency or at most 256 colors, else JPEG (quality 85); longest side at most 2048 px; at most 1.5 MiB
  animated GIF  kept animated as a GIF, at most 100 frames and 15 seconds, longest side at most 400 px, at most 1.5 MiB (an animated WebP or PNG becomes its first frame)
  thumbnail     always a JPEG of the first frame, longest side 360 px (an animated image's still: the log shows it while the image is off screen, paused, or the reader turned animation off)

The header's size is read before any pixel is decoded: over `MAX_PIXELS` is `pixels` with nothing decoded. A JPEG is decoded at a reduced scale (`draft`), so a photo of 24 megapixels never exists at full
size in memory. The caller (imgconv/server.py) runs this in a child process with a time limit and a memory limit; this file has no clock of its own.
Errors (ConvertError.code): `type` (not PNG, JPEG, GIF or WebP by its first bytes), `pixels` (more than MAX_PIXELS, or an animation whose frames together are too much work), `bad` (cannot be read,
truncated, damaged), `frames` (an animation of more than 100 frames or 15 seconds), `big` (cannot be made small enough).
"""
import collections
import io
import warnings

from PIL import Image, ImageOps

MAX_PIXELS = 24_000_000                 # decoded size of the input, from its header
MAX_SIDE = 2048                         # longest side of a still image that is kept
FULL_MAX = 1_572_864                    # 1.5 MiB: the most that a kept image may be
THUMB_SIDE = 360
THUMB_QUALITY = 78
JPEG_QUALITY = 85
JPEG_STEPS = (85, 78, 70)               # the qualities that are tried, in order, before an image is made smaller
ANIM_FRAMES = 100
ANIM_MS = 15_000                        # the length of an animation in all
ANIM_MIN_FRAME_MS = 20                  # a browser shows a frame of less than this as 100 ms: so it is counted as this
ANIM_SIDE = 400
ANIM_WORK_PIXELS = 150_000_000          # frames x width x height of the input: the work of decoding an animation
BACKGROUND = (10, 16, 22)               # what a thumbnail's transparent pixels are laid over: the chat dock's own dark
SIGNATURES = (("png", b"\x89PNG\r\n\x1a\n"), ("jpeg", b"\xff\xd8\xff"), ("gif", b"GIF87a"), ("gif", b"GIF89a"))
FORMATS = {"png": ["PNG"], "jpeg": ["JPEG"], "gif": ["GIF"], "webp": ["WEBP"]}

Result = collections.namedtuple("Result", "x full thumb w h tw th anim still")      # x: `jpg`, `png` or `gif`; anim: the image is animated; still: it was animated and only its first frame is kept


class ConvertError(Exception):
    def __init__(self, code):
        super().__init__(code)
        self.code = code


def sniff(data):
    """`png`, `jpeg`, `gif` or `webp` by the first bytes, else None. Nothing else is looked at before the decoder is asked, and no other format is ever opened."""
    for kind, signature in SIGNATURES:
        if data.startswith(signature):
            return kind
    if len(data) >= 12 and data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "webp"
    return None


def fit(size, side):
    """`size` made to fit in a square of `side` pixels (never larger than it is), keeping its shape; at least 1 x 1."""
    w, h = size
    longest = max(w, h)
    if longest <= side:
        return w, h
    return max(1, round(w * side / longest)), max(1, round(h * side / longest))


def fresh(im):
    """The same pixels in a new image that carries nothing: no `info`, so no encoder can find a profile, an EXIF block or a comment to write."""
    return Image.frombytes(im.mode, im.size, im.tobytes())


def has_alpha(im):
    if im.mode == "RGBA":
        return im.getextrema()[3][0] < 255             # (no copy of the channel: a 24 megapixel image has 96 MB of pixels)
    if im.mode in ("LA", "PA"):
        return im.getchannel("A").getextrema()[0] < 255
    return im.mode == "P" and "transparency" in im.info


def flatten(im):
    """RGB: transparent pixels laid over the dock's dark color."""
    if im.mode != "RGBA":
        im = im.convert("RGBA")
    base = Image.new("RGBA", im.size, BACKGROUND + (255,))
    base.alpha_composite(im)
    return base.convert("RGB")


def jpeg_bytes(im, quality):
    out = io.BytesIO()
    im.save(out, "JPEG", quality=quality, optimize=True, progressive=False, subsampling="4:2:0")
    return out.getvalue()


def png_bytes(im):
    out = io.BytesIO()
    im.save(out, "PNG", optimize=True)
    return out.getvalue()


def thumbnail(im):
    """(bytes, width, height) of the JPEG thumbnail of a decoded image."""
    rgb = flatten(im) if has_alpha(im) or im.mode not in ("RGB", "L") else im.convert("RGB")
    small = rgb.resize(fit(rgb.size, THUMB_SIDE), Image.LANCZOS) if max(rgb.size) > THUMB_SIDE else rgb
    return jpeg_bytes(fresh(small), THUMB_QUALITY), small.size[0], small.size[1]


def replaced(old, new):
    """`new` in the place of `old`, whose pixels are let go at once when it was a copy (a 24 megapixel image has 96 MB of them, and the converter's container has little more than that to give)."""
    if new is not old:
        old.close()
    return new


def turned(im):
    """The image in its real orientation: the EXIF orientation is applied only when it says something (a turn is a copy; no turn, no copy). The metadata that says it is dropped later."""
    try:
        if im.getexif().get(0x0112, 1) in (2, 3, 4, 5, 6, 7, 8):
            return replaced(im, ImageOps.exif_transpose(im))
    except Exception:
        pass                                           # (an EXIF block that cannot be read says nothing of orientation: the pixels as they are)
    return im


def decode_still(im, kind):
    """The first frame as an RGB or RGBA image of its real orientation, no larger than MAX_SIDE (a JPEG is decoded at a reduced scale first). Each step lets go of the one before when it made a copy, so
    that at most two full-size copies exist at one time (the least that a conversion needs); the original is closed here."""
    if kind == "jpeg":
        w, h = im.size
        im.draft("RGB", fit((w, h), MAX_SIDE))
    im.load()
    im = turned(im)
    mode = "RGBA" if has_alpha(im) else "RGB"
    if im.mode != mode:
        im = replaced(im, im.convert(mode))
    if max(im.size) >= 2 * MAX_SIDE:
        im = replaced(im, im.reduce(max(im.size) // MAX_SIDE))      # (a box average of whole pixels first: the resize below of a big RGBA image makes two more full-size copies of it, to premultiply the alpha)
    if max(im.size) > MAX_SIDE:
        im = replaced(im, im.resize(fit(im.size, MAX_SIDE), Image.LANCZOS))
    return fresh(im)


def encode_still(im):
    """(extension, bytes, image that was encoded) of a still image: PNG for transparency or few colors, else JPEG; made smaller in steps until it fits in FULL_MAX, else `big`."""
    alpha = im.mode == "RGBA"
    few = not alpha and im.getcolors(256) is not None
    for _ in range(4):
        if alpha or few:
            data = png_bytes(im)
            if len(data) <= FULL_MAX:
                return "png", data, im
            if not alpha:
                few = False                            # (a drawing that is too big as a PNG is tried as a JPEG)
                continue
        else:
            for quality in JPEG_STEPS:
                data = jpeg_bytes(im, quality)
                if len(data) <= FULL_MAX:
                    return "jpg", data, im
        size = fit(im.size, max(8, int(max(im.size) * 0.75)))
        im = fresh(im.resize(size, Image.LANCZOS))
    raise ConvertError("big")


def convert_animation(im):
    """An animated GIF kept animated: every frame composited and made small, then written again with its own timing."""
    frames_in = im.n_frames
    w, h = im.size
    if frames_in > ANIM_FRAMES:
        raise ConvertError("frames")
    if frames_in * w * h > ANIM_WORK_PIXELS:
        raise ConvertError("pixels")
    size = fit((w, h), ANIM_SIDE)
    frames, durations, total, alpha = [], [], 0, False
    for index in range(frames_in):
        im.seek(index)
        if im.size != (w, h):                          # (a frame that lies outside the screen of the file makes Pillow's canvas larger: the sizes that were checked above are not the ones that would be decoded)
            raise ConvertError("pixels")
        duration = max(ANIM_MIN_FRAME_MS, int(im.info.get("duration") or 100))
        total += duration
        if total > ANIM_MS:
            raise ConvertError("frames")
        frame = im.convert("RGBA")
        if size != frame.size:
            frame = frame.resize(size, Image.LANCZOS)
        alpha = alpha or frame.getchannel("A").getextrema()[0] < 255
        frames.append(frame)
        durations.append(duration)
    first = fresh(frames[0])
    thumb, tw, th = thumbnail(first)
    for _ in range(3):
        out_frames = [fresh(f if alpha else f.convert("RGB")) for f in frames]
        out = io.BytesIO()
        out_frames[0].save(out, "GIF", save_all=True, append_images=out_frames[1:], duration=durations, loop=0, disposal=2, optimize=False)
        data = out.getvalue()
        if len(data) <= FULL_MAX:
            return Result("gif", data, thumb, size[0], size[1], tw, th, True, False)
        size = fit(size, max(8, int(max(size) * 0.75)))
        frames = [f.resize(size, Image.LANCZOS) for f in frames]
    raise ConvertError("big")


def convert(data):
    """The Result for the uploaded bytes, or a ConvertError."""
    kind = sniff(data)
    if kind is None:
        raise ConvertError("type")
    warnings.simplefilter("error", Image.DecompressionBombWarning)
    Image.MAX_IMAGE_PIXELS = MAX_PIXELS * 2             # (a second guard under the explicit one below)
    try:
        im = Image.open(io.BytesIO(data), formats=FORMATS[kind])
        w, h = im.size
        if w < 1 or h < 1 or w * h > MAX_PIXELS:
            raise ConvertError("pixels")
        if kind == "gif" and getattr(im, "n_frames", 1) > 1:
            return convert_animation(im)
        still = getattr(im, "n_frames", 1) > 1          # (an animated WebP or PNG: its first frame only)
        im.seek(0)
        base = decode_still(im, kind)
        thumb, tw, th = thumbnail(base)
        x, full, kept = encode_still(base)
        return Result(x, full, thumb, kept.size[0], kept.size[1], tw, th, False, still)
    except ConvertError:
        raise
    except (MemoryError, Image.DecompressionBombError, Image.DecompressionBombWarning):
        raise ConvertError("pixels")
    except Exception:                                   # (a damaged, truncated or hostile file: whatever the decoder said, the answer is the same and says nothing of the file)
        raise ConvertError("bad")
