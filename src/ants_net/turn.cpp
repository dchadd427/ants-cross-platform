#include "ants_net/turn.hpp"

#include <vector>

#include "ants_net/p2p.hpp"
#include "ants_net/ws.hpp"

namespace ants::net {

std::array<uint8_t, 20> hmac_sha1(const uint8_t* key, size_t key_size, const uint8_t* message, size_t message_size) {
    constexpr size_t kBlock = 64;
    std::array<uint8_t, kBlock> k{};
    if (key_size > kBlock) {
        const std::array<uint8_t, 20> digest = ws_sha1(key, key_size);          // (a key longer than a block is hashed first)
        for (size_t i = 0; i < digest.size(); ++i) k[i] = digest[i];
    } else {
        for (size_t i = 0; i < key_size; ++i) k[i] = key[i];
    }
    std::vector<uint8_t> inner(kBlock + message_size);
    for (size_t i = 0; i < kBlock; ++i) inner[i] = static_cast<uint8_t>(k[i] ^ 0x36u);
    for (size_t i = 0; i < message_size; ++i) inner[kBlock + i] = message[i];
    const std::array<uint8_t, 20> inner_digest = ws_sha1(inner.data(), inner.size());
    std::vector<uint8_t> outer(kBlock + inner_digest.size());
    for (size_t i = 0; i < kBlock; ++i) outer[i] = static_cast<uint8_t>(k[i] ^ 0x5Cu);
    for (size_t i = 0; i < inner_digest.size(); ++i) outer[kBlock + i] = inner_digest[i];
    return ws_sha1(outer.data(), outer.size());
}

std::string turn_credential(const std::string& secret, const std::string& username) {
    const std::array<uint8_t, 20> mac = hmac_sha1(reinterpret_cast<const uint8_t*>(secret.data()), secret.size(), reinterpret_cast<const uint8_t*>(username.data()), username.size());
    return ws_base64_encode(mac.data(), mac.size());
}

bool turn_credential_valid(const std::string& secret, const std::string& username, const std::string& credential, uint32_t now) {
    const uint32_t expires = turn_username_expiry(username);
    if (expires == 0 || expires <= now || secret.empty()) return false;
    return credential == turn_credential(secret, username);
}

std::function<std::string(const std::string&)> make_turn_signer(const std::string& secret) {
    if (secret.empty()) return {};
    return [secret](const std::string& username) { return turn_credential(secret, username); };
}

namespace {

// A port in decimal, 1 - 65535, nothing else around it; 0 when it is not one (0 is no port: "0" gives it too)
uint32_t parse_relay_port(const std::string& text) {
    if (text.empty() || text.size() > 5 || text.find_first_not_of("0123456789") != std::string::npos) return 0;
    const unsigned long v = std::stoul(text);
    return v <= 65535 ? static_cast<uint32_t>(v) : 0u;
}

}  // namespace

std::shared_ptr<const TurnSettings> make_turn_settings(const std::string& host, const std::string& port_env, const std::string& secret) {
    if (host.empty() || secret.empty() || host.size() > 100) return nullptr;
    const size_t colon = host.find(':');
    const std::string name = host.substr(0, colon);
    uint32_t port = 3478;
    if (colon != std::string::npos) {
        port = parse_relay_port(host.substr(colon + 1));
        if (port == 0) return nullptr;
    }
    if (!port_env.empty()) {
        const uint32_t env_port = parse_relay_port(port_env);
        if (env_port == 0 || (colon != std::string::npos && env_port != port)) return nullptr;        // (one number: the host's and the environment's agree or there is no relay)
        port = env_port;
    }
    if (name.empty() || name.front() == '.' || name.front() == '-') return nullptr;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!ok) return nullptr;
    }
    auto turn = std::make_shared<TurnSettings>();
    const std::string at = name + ":" + std::to_string(port);
    turn->urls = {"stun:" + at, "turn:" + at + "?transport=udp", "turn:" + at + "?transport=tcp"};
    turn->credential = make_turn_signer(secret);
    return turn;
}

}  // namespace ants::net
