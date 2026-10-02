// The secret of the control interface. It comes from the environment (ANTS_SERVER_SECRET). When there is none and the server was given a place to keep one, the server
// makes a random secret the first time it starts, stores it in a file and uses the same secret at every later start: a container needs no setup (docs/NETWORK_PORT.md).
// A secret in the environment always wins and never touches the file.
//
// The file that the server makes: on POSIX only its owner can read it (mode 600; a umask can only make that stricter); on Windows it takes the permissions of the
// folder that holds it (the server sets none), so that folder must be private. It appears all at once and complete (see resolve_secret), never half written.
//
// A file that already exists is used as it is, whoever made it: its owner and its mode are the operator's responsibility, the server never changes them and does
// not check them. A symbolic link at the path is followed (a mounted secret is often one); a link whose target does not exist is an error, never a place for a new file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ants::server {

enum class SecretSource { Environment, File, Generated };

struct SecretResult {
    bool ok{false};
    std::string secret;                          // when ok
    SecretSource source{SecretSource::Environment};
    std::string path;                            // the file that holds it (File, Generated)
    std::string error;                           // when not ok: one sentence for the operator
};

/// `from_env` is the value of ANTS_SERVER_SECRET (null or empty: not set). `file` is where a secret may be kept ("" = nowhere: then a missing secret is an error).
/// A file that exists must be a regular file (or a link to one) that holds a usable secret (one line of 32 to 256 visible ASCII characters, no spaces; a line end,
/// a carriage return and trailing blanks are not part of it); at most 1025 bytes of it are ever read. A file that is not usable is never overwritten: the server stops
/// with an error that says how to fix it.
///
/// A secret that is made is written completely to a temporary file in the folder of `file` (named after it, with a random part, so that a stale one never blocks a start),
/// and that file is then given its final name with a hard link (POSIX) or a move that may not replace (Windows). Starters that run at the same time therefore all end up
/// with the same complete secret, exactly one of them gets SecretSource::Generated, and a crash leaves at most a stale temporary file, never a half written secret
/// file. The folder of `file` must be on a file system that supports hard links (a POSIX requirement); otherwise the error says so.
SecretResult resolve_secret(const char* from_env, const std::string& file);

/// Fills out[0 .. n) with random bytes of the operating system's generator (/dev/urandom, BCryptGenRandom): the generator that makes the control secret, and the one that the server gives
/// the rooms to make the keys of the seats (ants_net itself never reads the operating system's generator: the library is built for the web too). True when all n bytes are written;
/// false when the generator fails, and then what `out` holds is not to be used. n = 0 is true and writes nothing.
bool random_bytes(uint8_t* out, size_t n);

/// 32 random bytes of the operating system's generator as 64 lower-case hex digits. Empty when the generator fails.
std::string generate_secret_text();

/// The rule for a secret kept in a file: 32 to 256 characters, all of them visible ASCII (33 .. 126). It is the rule of the control interface's HTTP server
/// (a secret travels in a header), so a usable secret is always accepted by it.
bool usable_secret_text(const std::string& text);

}  // namespace ants::server
