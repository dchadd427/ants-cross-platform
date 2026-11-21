# Mbed TLS (vendored)

The TLS and X.509 library of the desktop game's encrypted link to a game server (`src/ants_net/tls.cpp`, `docs/NETWORK_PORT.md` "The desktop game's encrypted link") and of the tests' own TLS terminator and certificates (`tests/common/tls_test_server.hpp`).

- Version: 4.1.2 (released 2026-11-16), the 4.1 long-term-support branch of Mbed TLS (it includes TF-PSA-Crypto, the cryptography part).
- Source: <https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.2/mbedtls-4.1.2.tar.bz2>
- SHA-256 of the tarball: `392215e5e4846df04e2f4c4516079303bd73edde837d8f3e68fb4d4d7ddf0126` (the same as the `mbedtls-4.1.2-sha256sum.txt` of that release)
- Licence: Apache License 2.0 (the project uses the Apache-2.0 option of the dual licence "Apache-2.0 OR GPL-2.0-or-later"); the text is in `LICENSE`. Listed in `THIRD_PARTY_NOTICES.md`.
- Files: these folders of the release, byte for byte (`.gitattributes` keeps their bytes as they are): `include/mbedtls/`, `library/` (without `net_sockets.c` and `timing.c`: the game has its own sockets and clock), and of `tf-psa-crypto/`: `core/`, `dispatch/`, `extras/`, `include/`, `platform/`, `utilities/`, `drivers/builtin/{include,src}/` and `LICENSE`. The generated sources of the release (`error.c`, `version_features.c`, `ssl_debug_helpers_generated.c`, the driver wrappers and the configuration checks) are among them, so the build needs no Python or Perl. Not vendored: the tests, programs, documents, scripts and CMake files of the release, the Everest and p256-m drivers (off in the default configuration) and the test framework.
- Configuration: the release's default (`include/mbedtls/mbedtls_config.h` and `tf-psa-crypto/include/psa/crypto_config.h`, unchanged). The code that uses it (`src/ants_net/tls.cpp`) sets TLS 1.2 as the lowest version, turns session tickets off and verifies the server's certificate always.
- Build: `CMakeLists.txt` here (ours, not the release's) makes the static library `ants_mbedtls` from those sources, with the library's own warnings off (third-party code). Native builds only: the web build has the browser's TLS.

## Update

1. Pick a release of the long-term-support branch that is at least a month old, unless it fixes a security advisory (a TLS library is updated for those at once): <https://github.com/Mbed-TLS/mbedtls/releases>.
2. Download its `.tar.bz2` and compare its SHA-256 with the release's `-sha256sum.txt`; write the version, the date and the hash above.
3. Replace the folders listed above with those of the tarball (the same subset, nothing edited) and delete the files of the release's own build system that came with them.
4. Build with `-DANTS_WERROR=ON` and run `./run_tests.sh --fast` and the suites `test_wss` (`build/tests/test_net/test_wss`) and `test_wss_match` (`build/tests/test_server/test_wss_match`); then run the mutants of `tests/data/wss_link_mutants.json`.
