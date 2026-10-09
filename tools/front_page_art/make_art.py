#!/usr/bin/env python3
"""Makes the pictures and the font of the front page (web/front/) from the game's own art.

A developer's tool (it needs Pillow: pip install pillow); not part of any build or test. Run it from anywhere:

    python3 tools/front_page_art/make_art.py                                   the pictures that come from the sprites of asset_catalog/ and Original-Ants/ants.chd, and the font
    python3 tools/front_page_art/make_art.py --ants build/src/ants_app/ants    also the six map previews, from the game's own setup screen

What it makes (web/front/, or the folder of --out):
  qh_quickhelp.png, qh_power.png   the two help sheets of the original, cut out of ants.chd's sprites as they are. The front page's START! button is not a picture of the game: the original's
                                   is 98 x 27 pixels and broke up when it was shown larger, so web/lobby.html draws a button of its own in the same teal
  clay.png                         the background tile that every page repeats: the orange of the game's menus with a little noise and dirt (artlib.dirty_clay; the original's own tile is a flat
                                   orange, which looked too clean). It is made, not cut out, and the same every time. The pages name it with ?v= and the first 8 hex digits of its sha256, because the
                                   server lets a browser keep a .png for a week: after a new tile, put its hash in the seven places (tests/scripts/test_web_front.py says which)
  ants.png                         the standing ant of the game's start screen in the colours that the game gives each team (ants.chd's palette indices, shifted by 60, 40, 20 and 0 colours:
                                   artlib.team_ant): one row for each colour (green, red, blue, black), the seven pictures of the ant's 12 steps (sprites 1481 - 1487) left to right, a cell of
                                   24 x 41 pixels with the ant's origin in the same place in every cell, 168 x 164 in all. The lobby's colour cards play it as a sprite sheet (web/lobby.html)
  logo.png                         the "ants!" lettering of the title screen without its clay (make_logo.py)
  preview_<map>.png                the setup screen's own map preview of each of the six maps, 300 x 300: the game is run headless on its setup screen with only that map in its Maps folder
  LibreFranklin-Medium.ttf, LibreFranklin-OFL.txt   the game's own font and its licence (SIL OFL 1.1), copied from Original-Ants/
Every picture is saved as small as PNG goes without changing a pixel (a palette when 256 colours are enough).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from PIL import Image                                                      # noqa: E402

from artlib import CLAY, ROOT, TEAMS, chd_sprite, dirty_clay, remove_cursor, save_png, sprite, team_ant   # noqa: E402
from make_logo import make_logo                                            # noqa: E402

MAPS = ["tiny", "small", "medium", "gauntlet", "treasure", "islands"]      # the six maps of the original, by size (web/lobby.html lists them in this order)
PREVIEW_BOX = (353, 50, 653, 350)                                          # the map inside the frame of the setup screen's preview (960 x 540 screenshot)
# The seven pictures of the standing ant (the 12 steps of the game's animation use them in this order: 1481 1482 1483 1484 1485 1486 1481 1482 1487 1483 1484 1486) and where each is drawn
# from the ant's origin, (x, y) in pixels: the numbers of the animation's steps (the sprite table of ants.chd)
STAND = {1481: (-11, -28), 1482: (-11, -28), 1483: (-11, -29), 1484: (-11, -29), 1485: (-10, -28), 1486: (-10, -28), 1487: (-11, -29)}


def close_gap(image, top, resume, gap):
    """The rows of a sprite from 0 to `top`, then `gap` rows of clay, then the rows from `resume` on."""
    image = image.convert("RGB")
    out = Image.new("RGB", (image.width, top + gap + image.height - resume), CLAY)
    out.paste(image.crop((0, 0, image.width, top)), (0, 0))
    out.paste(image.crop((0, resume, image.width, image.height)), (0, top + gap))
    return out


def ants_sheet():
    """The standing ant in every team's colours: a row for each team (green, red, blue, black), the pictures of STAND left to right, every cell as big as the pictures together are."""
    ids = list(STAND)
    sizes = {number: chd_sprite(number)[1:3] for number in ids}
    left = min(STAND[number][0] for number in ids)
    top = min(STAND[number][1] for number in ids)
    width = max(STAND[number][0] + sizes[number][0] for number in ids) - left
    height = max(STAND[number][1] + sizes[number][1] for number in ids) - top
    sheet = Image.new("RGBA", (width * len(ids), height * len(TEAMS)), (0, 0, 0, 0))
    for row, team in enumerate(TEAMS):
        for column, number in enumerate(ids):
            ant = team_ant(number, team)
            ant.putdata([pixel if pixel[3] else (0, 0, 0, 0) for pixel in ant.getdata()])       # (the transparent pixels of every team are the same ones)
            sheet.paste(ant, (column * width + STAND[number][0] - left, row * height + STAND[number][1] - top))
    return sheet


def sprite_pictures():
    pictures = {"clay.png": dirty_clay(),                                  # the tile that every page's background repeats (the original's sprite 2 is a flat orange with a few pink dots)
                "ants.png": ants_sheet()}
    pictures["qh_quickhelp.png"] = close_gap(sprite(232), 27, 41, 6)       # the original's quick help; the line under its title that names the old publisher (rows 27 - 40) is cut out
    pictures["qh_power.png"] = close_gap(sprite(231), 26, 44, 10)          # its game instructions: the power-ups and the five special ants (the gap under the title is made smaller)
    pictures["logo.png"] = make_logo()
    return pictures


def run_game(ants, maps, arguments):
    """Runs the game headless in a scratch folder whose Original-Ants has only the listed maps (the setup screen highlights the first one) and returns its 960 x 540 screenshot."""
    with tempfile.TemporaryDirectory(prefix="ants_front_art.") as work:
        work = Path(work)
        (work / "Original-Ants" / "Maps").mkdir(parents=True)
        for entry in (ROOT / "Original-Ants").iterdir():
            if entry.name != "Maps" and entry.suffix.lower() not in (".exe", ".dll"):
                (work / "Original-Ants" / entry.name).symlink_to(entry)
        for name in maps:
            (work / "Original-Ants" / "Maps" / (name.upper() + ".LVL")).symlink_to(ROOT / "Original-Ants" / "Maps" / (name.upper() + ".LVL"))
        shot = work / "shot.bmp"
        command = [str(ants), "--headless", "--aspect", "16:9", "--settings", str(work / "settings.txt"), *arguments, "--screenshot", str(shot)]
        environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        done = subprocess.run(command, cwd=work, env=environment, capture_output=True, text=True, timeout=1800)
        if done.returncode != 0 or not shot.exists():
            sys.exit("the game did not make its screenshot (%s): %s" % (" ".join(arguments), (done.stdout + done.stderr).strip()[-400:]))
        image = Image.open(shot).convert("RGB")
        if image.size != (960, 540):
            sys.exit("the screenshot is %d x %d, not 960 x 540: the layout that the crop boxes of this tool expect is gone" % image.size)
        return image


def preview_pictures(ants):
    pictures = {}
    for name in MAPS:
        shot = run_game(ants, [name], ["--map-select", "--frames", "600"])
        pictures["preview_%s.png" % name] = remove_cursor(shot).crop(PREVIEW_BOX)
    return pictures


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(ROOT / "web" / "front"), help="where the files go (default: web/front)")
    parser.add_argument("--ants", help="the game program (for example build/src/ants_app/ants): makes the map previews too")
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    pictures = sprite_pictures()
    if args.ants:
        ants = Path(args.ants).resolve()
        pictures.update(preview_pictures(ants))
    for name, picture in sorted(pictures.items()):
        save_png(picture, out / name)
        print("%-22s %4d x %-4d %7d bytes" % (name, picture.width, picture.height, (out / name).stat().st_size))
    for name in ("LibreFranklin-Medium.ttf", "LibreFranklin-OFL.txt"):
        shutil.copyfile(ROOT / "Original-Ants" / name, out / name)
        print("%-22s %17d bytes" % (name, (out / name).stat().st_size))


if __name__ == "__main__":
    main()
