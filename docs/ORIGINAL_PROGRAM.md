# The original program and reverse engineering

How this remake is tied to the 1998 game *Ants*: how its rules are found in the original program, what the remake adds that the original does not have, and where your own copy of the original program goes. The original program and its decompilation are not in the repository.

## What this project is

This project is an educational remake and historical preservation effort.

Its method is reverse engineering of the original 1998 program (`Ants.exe`): Capstone disassembly for the exact instructions, and a Ghidra C decompilation for readable logic and constants, always cross-referenced with each other. [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md#20-dual-primary-reference-methodology) section 2.0 ("Dual Primary Reference Methodology") describes it. Rule 4 of [`AGENTS.md`](../AGENTS.md#4-mandatory-capstone-reverse-engineering--original-logic-parity) requires that the game's logic, timings and behaviour are checked against the original this way.

## What comes from the original and what does not

- **Read from the original's data files:** the artwork, animations and sounds of `Original-Ants/ants.chd` and the maps `Original-Ants/Maps/*.LVL` ([`ASSET_CATALOG.md`](ASSET_CATALOG.md) shows what is inside the archive).
- **Re-derived from the original:** the simulation's mechanics, timings and constants, to match the original exactly where that can be checked. This was done one system at a time over many releases, not in one step; [`../CHANGELOG.md`](../CHANGELOG.md) and [`CHANGELOG_ARCHIVE.md`](CHANGELOG_ARCHIVE.md) say which release did which.
- **This project's own additions:** network play (lock-step turns, state hashes, the dedicated server), computer players, widescreen, zoom and touch control.
- **Network play:** the original's game is not lock-step. The remake keeps its lobby flow, texts and rules, but not its transport, trust model or sync model ([`NETWORK_PORT.md`](NETWORK_PORT.md#what-the-original-does); the evidence is section 5.46 of [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md)).
- **Music:** the original plays four MIDI pieces. The remake plays MP3 renders of them, because the timbre of the 1998 General MIDI synthesiser cannot be reproduced ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) section 5.24e).
- **Other differences made on purpose** are listed in [`AUDIT_ONE_TO_ONE.md`](AUDIT_ONE_TO_ONE.md#3b-deliberate-differences-requested-by-the-owner-tweaks), so that they are not mistaken for deviations.

## The original program is a local reference, not part of the repository

Neither `Ants.exe` nor its Ghidra decompilation (`Ants.exe.c`) is in the repository. Comments and documents cite the decompilation as "Ants.exe.c line NNNN" and the program by address (virtual addresses, image base `0x01000000`).

If you own the original game, put your own copy of `Ants.exe` in `Original-Ants/` (and a decompilation, if you have one, at `docs/legacy/Ants.exe.c`). `.gitignore` keeps both out of every commit, together with the other local files of an installation of the original (cnc-ddraw, its shaders, `chat.txt`). Rule 4 of [`AGENTS.md`](../AGENTS.md#4-mandatory-capstone-reverse-engineering--original-logic-parity) covers these local copies for contributors.

A WebAssembly build made outside Docker (`build_web.sh`) packs the whole folder `Original-Ants/` into its data bundle, so it would carry your copy of `Ants.exe`. The Docker image build leaves the program out (`.dockerignore`).

The data files of the original game that the repository does hold, and the terms that apply to them, are described under [License](../README.md#license) and in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

## The tools

The reverse-engineering tools read the program. When it is missing they stop with "put your own copy of the original Ants.exe in Original-Ants/" (exit status 1; `analyze_binary.py` adds "and run this script from the repository root"):

| Tool | Needs | Does |
|---|---|---|
| `tools/analyze_binary.py` | `Original-Ants/Ants.exe` (pefile, capstone) | writes the function map `docs/ORIGINAL_BINARY_MAP.md` and `docs/binary_analysis.json` |
| `tools/extract_movement_tables.py` | `Original-Ants/Ants.exe`, `Original-Ants/ants.chd` (pefile) | writes `src/ants_sim/movement_tables_data.inc`, or checks it (`--check`; `--stdout` prints it instead) |
| `tools/movement_reference_model.py` | the same two files (pefile) | prints the golden timings of `tests/test_sim/test_movement_golden.cpp` (`--trace` also prints every pixel move) |

`Original-Ants/ants.chd` is in the repository, so only `Ants.exe` has to come from you; no tool or test reads the decompilation, it is for following the citations. If the Python module `pefile` (all three tools) or `capstone` (`analyze_binary.py`) is not installed, a tool stops earlier, with an error about that module (`pip install pefile capstone`).

`extract_movement_tables.py` checks the machine code that uses each table before it extracts anything, so a copy that is not the supported build of the program stops it instead of giving wrong data. The header of `src/ants_sim/movement_tables_data.inc` records the size and SHA-256 of the two original files that the tables came from.

## Tests and the original program's bytes

The test suites do not need the program. The two that compare the remake with static tables inside it, 1.1 `test_movement_tables` and 2.15 `test_movement_differential` (the suite ids of `./run_tests.sh`, see [`TESTING.md`](TESTING.md)), pin every byte of `Ants.exe` that they read: the address, size and SHA-256 digest of each region are fixed in `tests/common/original_program_bytes.hpp`.

- **With your copy** in `Original-Ants/`, they check its layout and every region against the digests and read the bytes from it. A copy of a different build fails that check (test case 5.0 of `test_movement_tables`, 0.1 of `test_movement_differential`).
- **Without it**, they lay the remake's own generated tables out like the program's, check that those bytes hash to the same digests (computed from the original program, so a match means the bytes are the original's, byte for byte) and read them instead. Each test prints which source it used ("Bytes of the original program: ...").
- **What is skipped without the program** is only what needs the program itself: test case 5.1 of `test_movement_tables` (its PE header) and, in its test case 5.3, the reading of the original's terrain-pair and tile-flag lists (`FUN_0100724c`). The remake keeps the per-tile result of those lists, not the lists, so 5.3 checks that result against pinned digests instead.
- A pinned digest is changed only with the program present: the run with the program is what verifies it.

## Where the findings are

- [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md): the specification, with the disassembly addresses, opcode traces and formulas, system by system. Older text there counts as unverified until it is checked against the program again (rule 5 of [`AGENTS.md`](../AGENTS.md)); a section whose heading says "Capstone-Verified" records such a check.
- [`ORIGINAL_BINARY_MAP.md`](ORIGINAL_BINARY_MAP.md): the function map that `tools/analyze_binary.py` writes, with the calls, strings and sound ids of each function. Its classification column is a guess, as the page says.
- [`reverse_engineering/movement/README.md`](reverse_engineering/movement/README.md): the reports behind the movement ground truth (section 5.32 of the specification).
- [`AUDIT_ONE_TO_ONE.md`](AUDIT_ONE_TO_ONE.md): the audit of the remake against the original, made from static evidence (it did not run the original): what differed, what was fixed and, in section 6, what only the real game can settle.
- [`TABLE4_ANIMATION_REFERENCE.md`](TABLE4_ANIMATION_REFERENCE.md): the 1,344 animation sequences of `Original-Ants/ants.chd`, with timings, sounds and motion. `tools/dump_table4.py` writes it from the archive, so it needs no copy of the program.
