# Third-party notices

This file lists the third-party code and fonts that are inside this repository or that the build downloads or links, and where the licence
text of each one can be found. This project's own source code and documents are under the MIT License (`LICENSE`). The original 1998 game's
artwork, sounds, music and maps are not covered by it: they belong to their copyright holders (see the next section).

## The original game's data (not this project's, to be replaced over time)

- Where: in the folder `Original-Ants/` exactly these files of the original game: the data archive `ants.chd` (the artwork, animations and sound
  effects), the map files `Maps/*.LVL` and the music files `*.mp3` and `*.MID`; and the data bundle that the web build packs from that folder.
  The font files `LibreFranklin-Medium.ttf` and `LibreFranklin-OFL.txt` in the same folder are not from the original game (see Libre Franklin below).
- What: the data of the 1998 game *Ants*. This project is a reverse-engineered remake; it does not own the original and cannot license it.
- Not here: the original game's program (`Ants.exe`) and its decompilation are not part of the repository. They are local references for the
  reverse engineering: an owner of the original game keeps a copy at `Original-Ants/Ants.exe` (and `docs/legacy/Ants.exe.c`), which `.gitignore`
  keeps out of every commit; the tests do not need them (see "Reverse Engineering" in the README).
- Why the data is here: so that the remake can run and so that its tests can compare it with the original. Replacing the original artwork with open
  material is planned as a separate, later project.
- Pictures made from it: the sprites in `asset_catalog/sprites/` and the pictures of the front page in `web/front/` (cut out of that artwork and of screenshots of the game by
  `tools/front_page_art/make_art.py`) are the original's artwork too, not this project's.
- Rights holders: if you hold rights to something here and want it removed, open an issue at the project's repository and it will be taken out.

## Included in the repository

### dr_mp3 (MP3 decoder)

- Where: `include/ants_app/dr_mp3.h` (a single-file library, compiled into `src/ants_app/audio_mixer.cpp`).
- What: dr_mp3 v0.7.4 by David Reid, <https://github.com/mackron/dr_libs>. It is based on minimp3 (<https://github.com/lieff/minimp3>).
- Licence: dr_mp3 is available under a choice of the public domain (Unlicense) or MIT No Attribution (MIT-0). The full text of both is at the end
  of `include/ants_app/dr_mp3.h`. The note about minimp3 (dedicated to the public domain, CC0 1.0) is the last comment of the same file.

### Libre Franklin (font)

- Where: `Original-Ants/LibreFranklin-Medium.ttf`, with its licence in `Original-Ants/LibreFranklin-OFL.txt`. The web build preloads both files
  with the rest of the folder `Original-Ants`. The front page serves a copy of the font, with the same licence text beside it, from
  `web/front/` (`LibreFranklin-Medium.ttf`, `LibreFranklin-OFL.txt`); `tools/front_page_art/make_art.py` makes the copy.
- What: Libre Franklin, Copyright 2020 The Libre Franklin Project Authors, <https://github.com/googlefonts/Libre-Franklin>.
- Licence: SIL Open Font License, Version 1.1. The full text is in `Original-Ants/LibreFranklin-OFL.txt`; keep that file together with the font.

## Removed from the repository

- cnc-ddraw (the Windows DirectDraw wrapper <https://github.com/FunkyFr3sh/cnc-ddraw>: `Original-Ants/ddraw.dll`, `Original-Ants/ddraw.ini`,
  `Original-Ants/cnc-ddraw config.exe`) and the libretro GLSL shaders (<https://github.com/libretro/glsl-shaders>: `Original-Ants/Shaders/`) used
  to be in the folder `Original-Ants/`. They only run the original `Ants.exe` on Windows, the remake never used them, and they left the repository
  together with the original program; a local setup of the original keeps them there (`.gitignore` keeps them out of every commit).

## Not in the repository (linked or downloaded by the build)

### SDL2 and SDL2_ttf

- Used by: the game program (`src/ants_app`) and its tests; not by the simulation, network or server libraries, and not by the dedicated server.
- Where they come from: the system or Homebrew packages on macOS and Linux; the official Windows development archives
  (`SDL2-devel-2.30.12-VC.zip` and `SDL2_ttf-devel-2.22.0-VC.zip` from <https://github.com/libsdl-org>) that `CMakeLists.txt` downloads on Windows;
  the Emscripten ports (`-sUSE_SDL=2 -sUSE_SDL_TTF=2`) of the web build.
- Licence: SDL2 and SDL2_ttf are published under the zlib licence (<https://www.libsdl.org/license.php>). SDL2_ttf in turn uses FreeType (and,
  depending on the version and the packaging, HarfBuzz), which come with their own licences; the licence files inside the archives or packages
  you use apply to the binaries you ship. When a binary distribution of the game (for example a Windows zip with the SDL DLLs) is made, include
  the licence files that come with those DLLs.
