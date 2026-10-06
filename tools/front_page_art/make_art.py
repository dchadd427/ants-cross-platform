#!/usr/bin/env python3
"""Makes the pictures and the font of the front page (web/front/) from the game's own art.

A developer's tool (it needs Pillow: pip install pillow); not part of any build or test. Run it from anywhere:

    python3 tools/front_page_art/make_art.py                                   the pictures that come from the sprites of asset_catalog/, and the font
    python3 tools/front_page_art/make_art.py --ants build/src/ants_app/ants    also the six map previews, from the game's own setup screen
    python3 tools/front_page_art/make_art.py --ants build/src/ants_app/ants --match    also the match picture of the header (a live match: see below)

What it makes (web/front/, or the folder of --out):
  lbl_pickamap.png, lbl_mapinfo.png, qh_quickhelp.png, qh_power.png
                                   the sprites of the game's menu screens (the hand-lettered labels, the two help sheets of the original), cut out of ants.chd's sprites as they are. The front
                                   page's START! button is not one of them: the original's picture is 98 x 27 pixels and broke up when it was shown larger, so web/lobby.html draws a button of
                                   its own in the same teal
  clay.png                         the background tile that every page repeats: the orange of the game's menus with a little noise and dirt (artlib.dirty_clay; the original's own tile is a flat
                                   orange, which looked too clean). It is made, not cut out, and the same every time
  ant_green/red/blue/black.png     the front standing ant of the roster, tinted for each team
  logo.png                         the "ants!" lettering of the title screen without its clay (make_logo.py)
  preview_<map>.png                the setup screen's own map preview of each of the six maps, 300 x 300: the game is run headless on its setup screen with only that map in its Maps folder
  match_view.png                   the map view of a match of the game (Treasure, three bots, zoom 0.5, seed 7, after 20000 frames), 761 x 497. It is a live match, so the picture depends on the
                                   game's version: the file in the repository is the one that was chosen, and is only made again with --match
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

from artlib import CLAY, ROOT, TEAMS, dirty_clay, key_clay, recolor_ant, remove_cursor, save_png, sprite   # noqa: E402
from make_logo import make_logo                                            # noqa: E402

MAPS = ["tiny", "small", "medium", "gauntlet", "treasure", "islands"]      # the six maps of the original, by size (web/lobby.html lists them in this order)
PREVIEW_BOX = (353, 50, 653, 350)                                          # the map inside the frame of the setup screen's preview (960 x 540 screenshot)
MATCH_BOX = (17, 22, 778, 519)                                             # the map view of a match screen (960 x 540 screenshot): everything but the HUD


def close_gap(image, top, resume, gap):
    """The rows of a sprite from 0 to `top`, then `gap` rows of clay, then the rows from `resume` on."""
    image = image.convert("RGB")
    out = Image.new("RGB", (image.width, top + gap + image.height - resume), CLAY)
    out.paste(image.crop((0, 0, image.width, top)), (0, 0))
    out.paste(image.crop((0, resume, image.width, image.height)), (0, top + gap))
    return out


def sprite_pictures():
    pictures = {"clay.png": dirty_clay(),                                  # the tile that every page's background repeats (the original's sprite 2 is a flat orange with a few pink dots)
                "lbl_pickamap.png": key_clay(sprite(274)), "lbl_mapinfo.png": key_clay(sprite(277))}
    for team in TEAMS:
        pictures["ant_%s.png" % team] = recolor_ant(sprite(1481), team)
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


def match_picture(ants):
    arguments = ["--map", "Original-Ants/Maps/TREASURE.LVL", "--bot", "1:hard", "--bot", "2:medium", "--bot", "3:medium", "--name", "Maple", "--seed", "7", "--zoom", "0.5", "--frames", "20000"]
    return {"match_view.png": remove_cursor(run_game(ants, ["treasure"], arguments)).crop(MATCH_BOX)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(ROOT / "web" / "front"), help="where the files go (default: web/front)")
    parser.add_argument("--ants", help="the game program (for example build/src/ants_app/ants): makes the map previews too")
    parser.add_argument("--match", action="store_true", help="with --ants: also make match_view.png (a live match of about a minute)")
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    pictures = sprite_pictures()
    if args.ants:
        ants = Path(args.ants).resolve()
        pictures.update(preview_pictures(ants))
        if args.match:
            pictures.update(match_picture(ants))
    elif args.match:
        sys.exit("--match needs --ants")
    for name, picture in sorted(pictures.items()):
        save_png(picture, out / name)
        print("%-22s %4d x %-4d %7d bytes" % (name, picture.width, picture.height, (out / name).stat().st_size))
    for name in ("LibreFranklin-Medium.ttf", "LibreFranklin-OFL.txt"):
        shutil.copyfile(ROOT / "Original-Ants" / name, out / name)
        print("%-22s %17d bytes" % (name, (out / name).stat().st_size))


if __name__ == "__main__":
    main()
