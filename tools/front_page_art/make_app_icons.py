#!/usr/bin/env python3
"""Makes the icons of the installable web app (web/app/) from the site's own art.

A developer's tool (it needs Pillow: pip install pillow); not part of any build or test. Run it from anywhere:

    python3 tools/front_page_art/make_app_icons.py [--out DIR] [--grain] [--preview DIR]

Every icon is the whole "ants!" word of web/front/logo.png (only its solid pixels: the logo's soft shadow is left out) on the orange of the game's menus (artlib.CLAY, flat; --grain uses the
tile web/front/clay.png instead, which makes the files several times bigger) with the flat red shadow that the site's buttons have, down and right. Nothing else is used: no other picture.

  icon-192.png, icon-512.png     the icons of the manifest: rounded corners (transparent), the thin frame of the pages (green, dark green, red)
  icon-maskable-512.png          the icon that Android cuts into a circle, a rounded square or a squircle: orange to every edge, and the word and its shadow inside the circle of 80% of the
                                 width (the safe zone of the maskable icon rule), as large as that allows
  apple-touch-icon.png           180 x 180 for the iPhone and iPad, orange to every edge and no transparency (iOS rounds it itself)
  --preview DIR                  also writes a picture of the icons at real size (48, 96, 192, 512; the maskable one inside a circle and a rounded square), for a person to look at

Every file is saved as small as PNG goes (a palette of at most 255 colors). The pages and the manifest name each file with ?v= and the first 8 hex digits of its sha256, because the server lets a
browser keep a .png for a week: after new icons, put the new digits in the manifest and in the head of every page (tests/scripts/test_web_app_manifest.py says where).
"""
import argparse
import hashlib
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from PIL import Image, ImageDraw, ImageFont   # noqa: E402

from artlib import CLAY, ROOT                  # noqa: E402

LOGO = ROOT / "web" / "front" / "logo.png"
CLAY_TILE = ROOT / "web" / "front" / "clay.png"
FONT = ROOT / "web" / "front" / "LibreFranklin-Medium.ttf"
SHADOW = (0x9D, 0x0D, 0x17)                    # the red shadow of the site's buttons
FRAME = ((12, (0x2B, 0x5F, 0x43)), (5, (0x13, 0x63, 0x4B)), (6, (0x9D, 0x0D, 0x17)))   # width at 512 and color: the outer green, the dark green, the red line
RADIUS = 92                                    # of the rounded corners at 512
SHADOW_DX, SHADOW_DY = 10, 12                  # at 512, down and right
SAFE = 0.40                                    # the maskable safe zone: a circle of this radius (a share of the width) around the middle
WIDTH_SHARE = {"plain": 0.90, "apple": 0.94}   # the share of the width that the word may fill where nothing cuts the icon (inside the frame, for the plain one)
SS = 4                                         # the corners and the frame are drawn four times as large and shrunk

ICONS = (("icon-192.png", "plain", 192), ("icon-512.png", "plain", 512), ("icon-maskable-512.png", "maskable", 512), ("apple-touch-icon.png", "apple", 180))


def word():
    """The word of the logo: the solid pixels only, cropped to them (RGBA)."""
    logo = Image.open(LOGO).convert("RGBA")
    solid = logo.split()[3].point(lambda a: 255 if a >= 128 else 0)
    box = solid.getbbox()
    logo.putalpha(solid)
    return logo.crop(box)


def background(size, grain):
    if not grain:
        return Image.new("RGB", (size, size), CLAY)
    tile = Image.open(CLAY_TILE).convert("RGB")
    out = Image.new("RGB", (size, size))
    for y in range(0, size, tile.height):
        for x in range(0, size, tile.width):
            out.paste(tile, (x, y))
    return out


def fit_maskable(word_size, size, dx, dy):
    """The scale of the word (a number) with which the word and its shadow, as a box, lie inside the safe circle."""
    w, h = word_size
    best = 1.0
    for _ in range(60):
        total_w, total_h = w * best + dx, h * best + dy
        corner = math.hypot(total_w / 2, total_h / 2)
        best *= (SAFE * size * 0.97) / corner
        if abs(corner - SAFE * size * 0.97) < 0.01:
            break
    return best


def draw(kind, size, grain):
    scale = size / 512
    base = word()
    dx, dy = round(SHADOW_DX * scale), round(SHADOW_DY * scale)
    inner = size
    if kind == "plain":
        inner = size - 2 * round(sum(w for w, _ in FRAME) * scale)
    if kind == "maskable":
        factor = fit_maskable(base.size, size, dx, dy)
    else:
        factor = (WIDTH_SHARE[kind] * inner - dx) / base.width        # the word and its shadow together fill the share
    w, h = max(1, round(base.width * factor)), max(1, round(base.height * factor))
    resized = base.resize((w, h), Image.LANCZOS)
    shadow = Image.new("RGBA", resized.size, SHADOW + (0,))
    shadow.putalpha(resized.split()[3])
    left = round((size - (w + dx)) / 2)
    top = round((size - (h + dy)) / 2)
    canvas = background(size, grain).convert("RGBA")
    canvas.alpha_composite(shadow, (left + dx, top + dy))
    canvas.alpha_composite(resized, (left, top))
    if kind != "plain":
        return canvas.convert("RGB")
    big = size * SS                                              # the rounded corners and the frame
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big - 1, big - 1), radius=round(RADIUS * scale * SS), fill=255)
    frame = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    inset = 0
    drawer = ImageDraw.Draw(frame)
    for width, color in FRAME:
        width = round(width * scale * SS)
        outer = round(RADIUS * scale * SS) - inset
        drawer.rounded_rectangle((inset, inset, big - 1 - inset, big - 1 - inset), radius=max(1, outer), outline=color + (255,), width=width)
        inset += width
    layer = canvas.resize((big, big), Image.NEAREST)
    layer.alpha_composite(frame)
    layer.putalpha(Image.composite(layer.split()[3], Image.new("L", (big, big), 0), mask))
    return layer.resize((size, size), Image.LANCZOS)


def save(image, path):
    """A palette of at most 255 colors (with transparency), optimised."""
    image = image.convert("RGBA") if "A" in image.getbands() and image.getchannel("A").getextrema()[0] < 255 else image.convert("RGB")
    if image.mode == "RGBA":
        packed = image.quantize(colors=255, method=Image.FASTOCTREE, dither=Image.NONE)
    else:
        packed = image.quantize(colors=255, method=Image.MEDIANCUT, dither=Image.NONE)
    packed.save(path, optimize=True)


def digits(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()[:8]


def preview(out_dir, folder):
    """A picture of the icons at real size, and the maskable one inside a circle and a rounded square."""
    folder.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(FONT), 15)
    sheet = Image.new("RGB", (1180, 760), (38, 40, 44))
    d = ImageDraw.Draw(sheet)
    icon = Image.open(out_dir / "icon-512.png").convert("RGBA")
    x = 20
    d.text((x, 8), "real size: 48, 96, 192, 512", fill=(220, 220, 220), font=font)
    for s in (48, 96, 192):
        sheet.paste(icon.resize((s, s), Image.LANCZOS), (x, 40), icon.resize((s, s), Image.LANCZOS))
        x += s + 20
    sheet.paste(icon, (20, 250), icon)
    mask = Image.open(out_dir / "icon-maskable-512.png").convert("RGB")
    x = 560
    d.text((x, 8), "maskable 512: safe circle, then circle and rounded square (192)", fill=(220, 220, 220), font=font)
    marked = mask.copy()
    ImageDraw.Draw(marked).ellipse((512 * 0.1, 512 * 0.1, 512 * 0.9, 512 * 0.9), outline=(255, 255, 255), width=2)
    sheet.paste(marked.resize((192, 192), Image.LANCZOS), (x, 40))
    for i, shape in enumerate(("circle", "rounded")):
        m = Image.new("L", (768, 768), 0)
        if shape == "circle":
            ImageDraw.Draw(m).ellipse((0, 0, 767, 767), fill=255)
        else:
            ImageDraw.Draw(m).rounded_rectangle((0, 0, 767, 767), radius=150, fill=255)
        m = m.resize((192, 192), Image.LANCZOS)
        small = mask.resize((192, 192), Image.LANCZOS)
        sheet.paste(small, (x + (i + 1) * 212, 40), m)
    apple = Image.open(out_dir / "apple-touch-icon.png").convert("RGB")
    d.text((560, 250), "Apple 180 (square), and as iOS rounds it", fill=(220, 220, 220), font=font)
    sheet.paste(apple, (560, 280))
    m = Image.new("L", (180, 180), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, 179, 179), radius=40, fill=255)
    sheet.paste(apple, (780, 280), m)
    sheet.save(folder / "icons_real_size.png")


def main():
    parser = argparse.ArgumentParser(description="Makes the icons of the web app (web/app/).")
    parser.add_argument("--out", default=str(ROOT / "web" / "app"))
    parser.add_argument("--grain", action="store_true", help="the clay tile of the pages instead of a flat orange (several times bigger files)")
    parser.add_argument("--preview", help="also write a picture of the icons at real size into this folder")
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    total = 0
    for name, kind, size in ICONS:
        save(draw(kind, size, args.grain), out / name)
        total += (out / name).stat().st_size
        print("%-24s %4d x %-4d %7d bytes  ?v=%s" % (name, size, size, (out / name).stat().st_size, digits(out / name)))
    print("together %d bytes" % total)
    if args.preview:
        preview(out, Path(args.preview))


if __name__ == "__main__":
    main()
