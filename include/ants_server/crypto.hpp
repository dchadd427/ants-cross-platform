#pragma once

// Small pieces of cryptography for the map submissions (docs/MAP_SUBMISSIONS.md): SHA-256 (to keep a maker's token only as a hash), HMAC-SHA-256 (the owner's session cookie that the login
// helper signs, and the key of the address counter), the two text encodings that go with them, (Two secrets are compared with ants::ctl::constant_time_equal, ants_ctl/http.hpp.)
// Plain functions of their arguments; no state, no allocation beyond the result.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ants::server {

/// SHA-256 of the bytes as 32 raw bytes
std::string sha256(std::string_view data);
/// HMAC-SHA-256 (RFC 2104) of `data` under `key` (any length) as 32 raw bytes
std::string hmac_sha256(std::string_view key, std::string_view data);
/// PBKDF2 with HMAC-SHA-256 (RFC 8018 section 5.2; the vectors of RFC 7914 section 11 and 6070's scheme are in tests/test_server/test_map_names.cpp): `length` bytes (1 to 1024) of the key made from
/// `password` and `salt` by `iterations` rounds (at least 1). False, and `out` empty, for a count or a length out of range. Built on hmac_sha256's own SHA-256; no new primitive: the loop is the RFC's.
/// It is slow on purpose (a password hash, docs/MAP_SUBMISSIONS.md "Accounts"): 600,000 rounds take a few tenths of a second.
bool pbkdf2_hmac_sha256(std::string_view password, std::string_view salt, uint32_t iterations, size_t length, std::string& out);
/// How many times pbkdf2_hmac_sha256 has run since the program began (a test holds that a sign-in with an unknown name does the same work as one with a known name, by this count)
uint64_t pbkdf2_runs();
/// Whether two byte strings are equal, looking at every byte of the longer one whatever the answer is (no early exit), so that the time says nothing about where they differ. `steps` (optional) receives
/// how many byte pairs were looked at: a test holds the property by it (equal to the longer length for every pair), since a clock cannot.
bool equal_constant_time(std::string_view a, std::string_view b, size_t* steps = nullptr);
/// How many times equal_constant_time has run (a test holds that a password hash is compared with it and not with ==, which no answer could show)
uint64_t equal_constant_time_calls();
/// Lower-case hex of the bytes
std::string hex_encode(std::string_view bytes);
/// Base64url without padding (RFC 4648 section 5)
std::string base64url_encode(std::string_view bytes);
/// The bytes of a base64url text without padding; false for any other character, a padding sign or a length that no encoding makes
bool base64url_decode(std::string_view text, std::string& out);

}  // namespace ants::server
