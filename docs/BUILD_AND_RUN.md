# Build and Run Guide

## Overview
This document tracks the verified build environments, configuration steps, and test executions for the Ants remake engine.

## Environment Details
- **Operating System**: Windows 11 (x64)
- **Compiler**: Visual C++ / MSVC 19.44.35228.0 (Build Tools 2022 v17.14)
- **Build System**: CMake (v3.31.6) via Visual Studio 2022 Build Tools
- **Audio / Media**: SDL2 (v2.30.12 VC prebuilt package via CMake FetchContent), WinMM, WS2_32

## Build Configuration & Execution
1. **CMake Configuration**:
   ```cmd
   cmake -B build -G "Visual Studio 17 2022" -A x64
   ```
2. **Release Compilation**:
   ```cmd
   cmake --build build --config Release -j8
   ```
3. **E2E Test Suite Compilation**:
   ```cmd
   cmake -S tests/e2e -B build_e2e -G "Visual Studio 17 2022" -A x64
   cmake --build build_e2e --config Release -j8
   ```

## Automated Toolchain Detection
`start_game.bat` and `run_tests.bat` automatically detect the local CMake binary using `vswhere.exe -find "**\cmake.exe"` if `cmake` is not already configured in the user's `PATH`.

## macOS and Linux
```bash
brew install cmake sdl2 sdl2_ttf          # macOS (Linux: cmake g++ libsdl2-dev libsdl2-ttf-dev)
# (macOS: Homebrew's sdl2 is now sdl2-compat, SDL2 on top of SDL3; the game builds with it but eight test programs fail. The project is developed with real SDL2 2.32.x:
#  README.md, "Prerequisites & Dependencies", has the steps that build it from the release sources.)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/src/ants_app/ants
```
`./build/src/ants_app/ants` with no options opens the desktop start menu (Single player with computer players, Join with a code, Host an online match, Quit; README "Start Menu"); an option that chooses a mode (`--map-select` for the setup screen at once, `--map`, `--host`, `--join`, `--headless` ...) skips it. `./start_game.sh` builds (when needed) and launches (four windows in one match by default; `./start_game.sh --single` is one plain game with the menu). If the build stops with an Xcode licence error on macOS, accept the licence (`sudo xcodebuild -license`) or set `DEVELOPER_DIR=/Library/Developer/CommandLineTools` for the shell that builds.

## Testing
All test suites are run with `./run_tests.sh` (macOS, Apple clang). The suites, what each one checks and their current assertion counts are listed in the README under [Testing & Verification](../README.md#testing--verification).

On Windows, `run_tests.bat` runs only part of them: the asset decoder suites, the first simulation suites (`test_sim_rules`, the two challengers), `test_app_integration` and the E2E runner. The other suites are built by the same CMake project; `ctest -N` in the build folder lists all of them and `ctest -C Release --output-on-failure` runs them (the Windows jobs of CI, `.github/workflows/ci.yml`, build with Visual Studio 2022 and 2026 and run exactly this on every push; the README's "Continuous Integration" lists what runs where).

## Running the Game
To launch the interactive game window:
```cmd
start_game.bat
```
Or directly execute:
```cmd
build\src\ants_app\Release\ants.exe
```
With no options the game opens the start menu (`start_game.bat --single` does the same; the default four-window rig of `start_game.bat` starts each window in its room and shows no menu). An option that chooses a mode, such as `--map-select`, skips the menu.
Headless verification with screenshot capture can also be executed:
```cmd
build\src\ants_app\Release\ants.exe --headless --frames 60 --screenshot "output.bmp"
```
