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
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/src/ants_app/ants
```
`./start_game.sh` builds (when needed) and launches. If the build stops with an Xcode licence error on macOS, accept the licence (`sudo xcodebuild -license`) or set `DEVELOPER_DIR=/Library/Developer/CommandLineTools` for the shell that builds.

## Test Results
All test suites are run with `./run_tests.sh` (macOS, Apple clang) and have a 100% pass rate at v0.0.45 (the counts of every suite are in the [README](../README.md#what-the-suites-cover-v0045-all-passing)):
- **Asset decoders and movement-table parity**: 8 suites, 69,809 assertions, and 7 suites, 120,582 assertions - **PASS**
- **Simulation**: 13 rule suites (2,252 assertions), the challenger, path planner, movement golden and hill / combat / ability / power-up / food action suites, the command layer (15 tests, 490,504 assertions), the lock-step network core (17 tests, 119,335), the room (8 tests, 37,769) and TCP transport (6 tests, 60,122) - **PASS**
- **Application**: `test_app_integration` 188 tests, 6,529 assertions; render parity 237 checks, HUD layout 530, status messages 255, input model 70, pointer model 329 - **PASS**
- **Opaque-box E2E** (`e2e_runner --all`): 4 tiers, 506 test cases - **PASS**

The Windows commands above (`start_game.bat`, `run_tests.bat`) run the same suites with MSVC.

## Running the Game
To launch the interactive game window:
```cmd
start_game.bat
```
Or directly execute:
```cmd
build\src\ants_app\Release\ants.exe
```
Headless verification with screenshot capture can also be executed:
```cmd
build\src\ants_app\Release\ants.exe --headless --frames 60 --screenshot "output.bmp"
```
