# Third-party notices

This file lists the third-party code and fonts that are inside this repository or that the build downloads or links, and where the licence
text of each one can be found. This project's own source code and documents are under the MIT License (`LICENSE`). The original 1998 game's
program, artwork, sounds, music and maps are not covered by it: they belong to their copyright holders (see the next section).

## The original game's program and data (not this project's, to be replaced over time)

- Where: the folder `Original-Ants/` (the program `Ants.exe`, the data archive `ants.chd` with the artwork, animations and sound effects, the music
  files, the map files in `Original-Ants/Maps/`, and a few support files), and the data bundle that the web build packs from that folder.
- What: the 1998 game *Ants* and its data. This project is a reverse-engineered remake; it does not own the original and cannot license it.
- Why it is here: so that the remake can run and so that its tests can compare it with the original. Replacing the original artwork with open material is
  planned as a separate, later project.
- Rights holders: if you hold rights to something here and want it removed, open an issue at the project's repository and it will be taken out.

## Included in the repository

### dr_mp3 (MP3 decoder)

- Where: `include/ants_app/dr_mp3.h` (a single-file library, compiled into `src/ants_app/audio_mixer.cpp`).
- What: dr_mp3 v0.7.4 by David Reid, <https://github.com/mackron/dr_libs>. It is based on minimp3 (<https://github.com/lieff/minimp3>).
- Licence: dr_mp3 is available under a choice of the public domain (Unlicense) or MIT No Attribution (MIT-0). The full text of both is at the end
  of `include/ants_app/dr_mp3.h`. The note about minimp3 (dedicated to the public domain, CC0 1.0) is the last comment of the same file.

### Libre Franklin (font)

- Where: `Original-Ants/LibreFranklin-Medium.ttf`, with its licence in `Original-Ants/LibreFranklin-OFL.txt`. The web build preloads both files
  with the rest of the folder `Original-Ants`.
- What: Libre Franklin, Copyright 2020 The Libre Franklin Project Authors, <https://github.com/googlefonts/Libre-Franklin>.
- Licence: SIL Open Font License, Version 1.1. The full text is in `Original-Ants/LibreFranklin-OFL.txt`; keep that file together with the font.

### cnc-ddraw (Windows DirectDraw wrapper, not used by the remake)

- Where: `Original-Ants/ddraw.dll`, `Original-Ants/cnc-ddraw config.exe`, `Original-Ants/ddraw.ini`.
- What: cnc-ddraw (the PE version resource of `ddraw.dll` reads: FileVersion 7.1.0.0, Copyright (c) 2010-2024, github.com/FunkyFr3sh),
  <https://github.com/FunkyFr3sh/cnc-ddraw>. It is only used to run the original `Ants.exe` on Windows; the remake neither loads nor reads it.
- Licence: the licence text is not part of this repository and is not reproduced here. To be added by the owner.

### libretro GLSL shaders (not used by the remake)

- Where: `Original-Ants/Shaders/` (including `shader-package.zip`).
- What: a set of pixel shaders for emulators, <https://github.com/libretro/glsl-shaders>. `Original-Ants/Shaders/readme.txt` says: "Copyrights are
  held by the respective authors." The remake does not load them.
- Licence: differs from shader to shader and is not reproduced here. To be added by the owner.

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
