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

## Test Results
All test suites have been run and verified with a 100% pass rate:
- **Asset Decoder Suites** (`test_assets.exe`, `test_challenger_m1_*.exe`): 8 suites, 26 test cases, 69,809 assertions - **PASS**
- **Simulation Rules Suites** (`test_sim_rules.exe`, `test_challenger_m2_*.exe`): 13 suites, 68 test cases, 2,222 assertions - **PASS**
- **Application Integration Suites** (`test_app_integration.exe`): 12 suites, 106 test cases, 2,204 assertions - **PASS**
- **Opaque-box E2E Suite** (`e2e_runner.exe --all`): 4 tiers, 506 test cases - **PASS**

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
