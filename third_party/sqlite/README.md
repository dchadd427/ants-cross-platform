# SQLite (vendored)

The database library of the game server (`ants_db`, `docs/SERVER.md` "The database").

- Version: 3.53.4 (released 2026-07-24)
- Files: `sqlite3.c` and `sqlite3.h` of the official amalgamation, unchanged. No `shell.c`, no `sqlite3ext.h` (extensions are switched off).
- Source: <https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip> (from <https://www.sqlite.org/download.html>)
- SHA-256 of the zip: `1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d`
- SHA3-256 of the zip: `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e` (computed on the download: the download page lists SHA3-256 only for the current release)
- Licence: public domain (the author disclaims copyright; see the blessing at the top of `sqlite3.h`). Listed in `THIRD_PARTY_NOTICES.md`.
- Build: `CMakeLists.txt` here makes the static library `ants_sqlite` with the compile definitions explained in it (not threadsafe, no extensions, foreign keys on, no double-quoted strings).

## Update

1. Pick a release at least a month old from <https://www.sqlite.org/chronology.html>; download its `sqlite-amalgamation-<NNNNNNN>.zip` from the download page.
2. Compare the SHA3-256 of the zip with the one on the download page when the release is the current one, and write both hashes and the version above.
3. Replace `sqlite3.c` and `sqlite3.h` with the two files of the zip, nothing else; do not edit them (`.gitattributes` keeps their bytes as they are).
4. Build with `-DANTS_WERROR=ON`, run `./run_tests.sh --fast` (the suite `test_db`) and the server part `options` of `tests/scripts/test_ants_server.sh`.
