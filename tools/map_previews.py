#!/usr/bin/env python3
"""Pre-rendered preview pictures of the maps of a folder (Community-Maps/ by default), made the way the six originals' web/front/preview_*.png are.

usage: map_previews.py [FOLDER] [--ants PROGRAM] [--out DIR] [--jobs N] [--force]

For every .lvl file of FOLDER the game (--ants, default build/src/ants_app/ants) is run headless on its map-select setup screen with only that map in its Maps folder
(tools/front_page_art/make_art.run_game), the mouse arrow is painted out and the map preview of the screen is cut out (PREVIEW_BOX, 300 x 300) and saved as it is, as a lossy WebP
(QUALITY): DIR/<file name without .lvl>.webp (DIR is FOLDER/previews).

FOLDER/previews.json says what is done: {"<map file>": {"hash": "<FNV-1a 64 of the file, as in maps.json>", "picture": "<file name of the picture>",
"encoding": "<ENCODING: size and quality>"}}. A map whose hash, picture and encoding
are as listed is skipped, so a run is resumable and a repaired or changed map is drawn again; a picture of a map that is gone is removed. --force draws everything again.
The pictures are the same every time (the game is deterministic, the encoder has no randomness).
"""
import argparse
import json
import os
import struct
import sys
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "front_page_art"))
from community_maps import fnv1a64                                         # noqa: E402  (the hash of maps.json and of the network protocol)

SIZE = 300                                                                 # the picture's side: the game's preview as it is (make_art.PREVIEW_BOX); smaller or fewer colours wiped out the small sprites
QUALITY = 85                                                               # lossy WebP: the sprites and the hill art stay clear; the 534 pictures are about 9 MB (q80: 7.4 MB, a PNG of Iceworld alone is 164 KB)
ENCODING = "webp-%d-q%d" % (SIZE, QUALITY)                                 # recorded with every picture: a changed encoding draws the picture again without --force
EXTENSION = ".webp"
INDEX = "previews.json"
FOLDER_DEFAULT = HERE.parent / "Community-Maps"
ANTS_DEFAULT = HERE.parent / "build" / "src" / "ants_app" / "ants"


def level_files(folder):
    """The map files of a folder, sorted (the extension is .lvl in any case, as everywhere in this repository)."""
    return sorted(n for n in os.listdir(folder) if n.lower().endswith(".lvl") and os.path.isfile(os.path.join(folder, n)))


def picture_name(file):
    """The picture of a map file: its name without the .lvl, plus .webp."""
    return file[:-4] + EXTENSION


def file_hash(path):
    with open(path, "rb") as f:
        return fnv1a64(f.read())


def read_index(path):
    """The index of a folder; a missing, unreadable or malformed one is an empty one (everything is drawn again)."""
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {}
    if not isinstance(data, dict):
        return {}
    return {k: v for k, v in data.items() if isinstance(v, dict) and isinstance(v.get("hash"), str) and isinstance(v.get("picture"), str) and isinstance(v.get("encoding"), str)}


def write_index(path, index):
    tmp = str(path) + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(dict(sorted(index.items())), f, indent=1, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, path)


def up_to_date(index, file, digest, out):
    entry = index.get(file)
    return bool(entry) and entry["hash"] == digest and entry["encoding"] == ENCODING and entry["picture"] == picture_name(file) and os.path.isfile(os.path.join(out, entry["picture"]))


def webp_size(path):
    """(width, height) of a WebP file from its header (no Pillow needed: the repository's checks run where there is none); None when it is not a WebP."""
    with open(path, "rb") as f:
        head = f.read(32)
    if len(head) < 30 or head[:4] != b"RIFF" or head[8:12] != b"WEBP":
        return None
    kind, data = head[12:16], head[20:30]
    if kind == b"VP8 " and data[3:6] == b"\x9d\x01\x2a":                         # lossy: the frame header's 14-bit sizes
        return struct.unpack("<HH", data[6:10])[0] & 0x3FFF, struct.unpack("<HH", data[6:10])[1] & 0x3FFF
    if kind == b"VP8L" and data[0] == 0x2F:                                   # lossless: 14 bits each, minus one
        bits = struct.unpack("<I", data[1:5])[0]
        return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
    if kind == b"VP8X":                                                       # extended: 24 bits each, minus one
        return int.from_bytes(head[24:27], "little") + 1, int.from_bytes(head[27:30], "little") + 1
    return None


def encode(image):
    """The preview as the picture that is saved: an RGB image of SIZE x SIZE (the game's 300 x 300 preview, not resized)."""
    image = image.convert("RGB")
    if image.size != (SIZE, SIZE):
        raise ValueError("the preview is %d x %d, not %d x %d" % (image.width, image.height, SIZE, SIZE))
    return image


def save(image, path):
    """Lossy WebP at QUALITY (method 6, the slowest and smallest; the same bytes every time)."""
    image.save(path, format="WEBP", quality=QUALITY, method=6)


def game_renderer(ants):
    """render(path) -> the 300 x 300 preview of the map file, from the real game (the method of make_art.preview_pictures)."""
    import make_art
    from artlib import remove_cursor
    ants = Path(ants).resolve()

    def render(path):
        shot = make_art.run_game(ants, [Path(path)], ["--map-select", "--frames", "600"])
        return remove_cursor(shot).crop(make_art.PREVIEW_BOX)
    return render


def update(folder, out, render, jobs=1, force=False, log=None):
    """Draws the pictures that are missing or out of date; returns (drawn, skipped, removed) as lists of map files. The index is saved after every picture, so an interrupted run goes on."""
    folder, out = str(folder), str(out)
    os.makedirs(out, exist_ok=True)
    index_path = os.path.join(folder, INDEX)
    old = read_index(index_path)
    index = {} if force else old
    files = level_files(folder)
    digests = {n: file_hash(os.path.join(folder, n)) for n in files}
    todo = [n for n in files if force or not up_to_date(index, n, digests[n], out)]
    skipped = [n for n in files if n not in todo]
    index = {n: index[n] for n in files if n in index}
    removed = []
    for file, entry in sorted(old.items()):                                # pictures that the index lists for maps that are gone (nothing else in DIR is touched)
        if file not in files and os.path.isfile(os.path.join(out, entry["picture"])):
            os.remove(os.path.join(out, entry["picture"]))
            removed.append(entry["picture"])
    lock = threading.Lock()

    def one(name):
        picture = encode(render(os.path.join(folder, name)))
        target = os.path.join(out, picture_name(name))
        save(picture, target + ".tmp")
        os.replace(target + ".tmp", target)
        with lock:
            index[name] = {"hash": digests[name], "picture": picture_name(name), "encoding": ENCODING}
            write_index(index_path, index)
            if log:
                log("%s  %d bytes" % (picture_name(name), os.path.getsize(target)))
        return name

    with ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        drawn = list(pool.map(one, todo))
    write_index(index_path, index)
    return drawn, skipped, removed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", nargs="?", default=str(FOLDER_DEFAULT), help="the folder of the .lvl files (default: Community-Maps)")
    parser.add_argument("--ants", default=str(ANTS_DEFAULT), help="the game program (default: build/src/ants_app/ants)")
    parser.add_argument("--out", help="where the pictures go (default: FOLDER/previews)")
    parser.add_argument("--jobs", type=int, default=1, help="games run at the same time (default 1)")
    parser.add_argument("--force", action="store_true", help="draw every picture again")
    args = parser.parse_args(argv)
    if not os.path.isfile(args.ants):
        sys.exit("the game program %s does not exist (build it: cmake --build build --target ants)" % args.ants)
    out = args.out or os.path.join(args.folder, "previews")
    drawn, skipped, removed = update(args.folder, out, game_renderer(args.ants), args.jobs, args.force, log=lambda line: print(line, flush=True))
    print("%d drawn, %d up to date, %d removed" % (len(drawn), len(skipped), len(removed)))


if __name__ == "__main__":
    main()
