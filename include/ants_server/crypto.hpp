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
/// Lower-case hex of the bytes
std::string hex_encode(std::string_view bytes);
/// Base64url without padding (RFC 4648 section 5)
std::string base64url_encode(std::string_view bytes);
/// The bytes of a base64url text without padding; false for any other character, a padding sign or a length that no encoding makes
bool base64url_decode(std::string_view text, std::string& out);

}  // namespace ants::server
