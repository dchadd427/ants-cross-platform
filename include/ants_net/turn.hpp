#pragma once

// The relay's credentials (coturn's shared-secret mode, docs/P2P_HINTS.md): the username is "<expiry unix time>:<random label>" and the password is base64(HMAC-SHA1(secret, username)). The relay checks it
// with the same secret, so nothing is stored: a credential is good until its expiry and for nobody else. Native builds only (the browser build is never the one that makes them).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ants::net {

/// HMAC-SHA1 (RFC 2104) over SHA-1 (ws_sha1): the code of a credential
std::array<uint8_t, 20> hmac_sha1(const uint8_t* key, size_t key_size, const uint8_t* message, size_t message_size);
/// base64(HMAC-SHA1(secret, username)) as coturn computes it
std::string turn_credential(const std::string& secret, const std::string& username);
/// What coturn decides: the username is "<expiry>:<label>", the expiry has not passed (`now` is unix seconds) and the credential is the one of `secret`
bool turn_credential_valid(const std::string& secret, const std::string& username, const std::string& credential, uint32_t now);
/// The function that TurnSettings::credential holds: it signs a username with `secret` and keeps the secret to itself. An empty secret makes an empty function (no relay).
std::function<std::string(const std::string&)> make_turn_signer(const std::string& secret);

}  // namespace ants::net
