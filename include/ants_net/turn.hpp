#pragma once

// The relay's credentials (coturn's shared-secret mode, docs/P2P_HINTS.md): the username is "<expiry unix time>:<random label>" and the password is base64(HMAC-SHA1(secret, username)). The relay checks it
// with the same secret, so nothing is stored: a credential is good until its expiry and for nobody else. Native builds only (the browser build is never the one that makes them).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "ants_net/p2p.hpp"

namespace ants::net {

/// HMAC-SHA1 (RFC 2104) over SHA-1 (ws_sha1): the code of a credential
std::array<uint8_t, 20> hmac_sha1(const uint8_t* key, size_t key_size, const uint8_t* message, size_t message_size);
/// base64(HMAC-SHA1(secret, username)) as coturn computes it
std::string turn_credential(const std::string& secret, const std::string& username);
/// What coturn decides: the username is "<expiry>:<label>", the expiry has not passed (`now` is unix seconds) and the credential is the one of `secret`
bool turn_credential_valid(const std::string& secret, const std::string& username, const std::string& credential, uint32_t now);
/// The function that TurnSettings::credential holds: it signs a username with `secret` and keeps the secret to itself. An empty secret makes an empty function (no relay).
std::function<std::string(const std::string&)> make_turn_signer(const std::string& secret);
/// What the server tells its players about its relay, from the relay's name (`host`: "HOST" or "HOST:PORT": letters, digits, '.', '-'), the port of the environment (`port_env`, ANTS_TURN_PORT: empty, none) and
/// the secret. The port is the one the host names, else the environment's, else 3478 (TURN's own); it is ONE number: a host that names a port and an environment that names another is refused, as is
/// a port that is not 1 - 65535, a name that is not a name or an empty secret. Null when refused: no relay, and no player is given a credential. The addresses are stun:HOST:PORT and turn:HOST:PORT with
/// ?transport=udp and ?transport=tcp.
std::shared_ptr<const TurnSettings> make_turn_settings(const std::string& host, const std::string& port_env, const std::string& secret);

}  // namespace ants::net
