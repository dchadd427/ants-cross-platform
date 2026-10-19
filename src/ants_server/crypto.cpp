#include "ants_server/crypto.hpp"

#include <array>
#include <cstring>

namespace ants::server {

namespace {

constexpr std::array<uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

void compress(std::array<uint32_t, 8>& h, const unsigned char* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t{block[4 * i]} << 24) | (uint32_t{block[4 * i + 1]} << 16) | (uint32_t{block[4 * i + 2]} << 8) | uint32_t{block[4 * i + 3]};
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = hh + s1 + ch + kRound[static_cast<size_t>(i)] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

}  // namespace

std::string sha256(std::string_view data) {
    std::array<uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    size_t whole = data.size() / 64;
    for (size_t i = 0; i < whole; ++i) compress(h, bytes + 64 * i);
    unsigned char tail[128];
    const size_t rest = data.size() - whole * 64;
    if (rest > 0) std::memcpy(tail, bytes + whole * 64, rest);
    tail[rest] = 0x80;
    const size_t padded = rest < 56 ? 64 : 128;
    std::memset(tail + rest + 1, 0, padded - rest - 1);
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8u;
    for (int i = 0; i < 8; ++i) tail[padded - 1 - static_cast<size_t>(i)] = static_cast<unsigned char>(bits >> (8 * i));
    compress(h, tail);
    if (padded == 128) compress(h, tail + 64);
    std::string out(32, '\0');
    for (size_t i = 0; i < 8; ++i) {
        for (size_t j = 0; j < 4; ++j) out[4 * i + j] = static_cast<char>(h[i] >> (24 - 8 * j));
    }
    return out;
}

std::string hmac_sha256(std::string_view key, std::string_view data) {
    std::string k = key.size() > 64 ? sha256(key) : std::string(key);
    k.resize(64, '\0');
    std::string inner(64, '\0'), outer(64, '\0');
    for (size_t i = 0; i < 64; ++i) {
        inner[i] = static_cast<char>(k[i] ^ 0x36);
        outer[i] = static_cast<char>(k[i] ^ 0x5c);
    }
    inner.append(data);
    outer += sha256(inner);
    return sha256(outer);
}

namespace {

using Sha256State = std::array<uint32_t, 8>;

constexpr Sha256State kSha256Start = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

// SHA-256 of "<what was already hashed in whole blocks> + tail", given the state after those blocks and their length in bytes (a multiple of 64). Used by PBKDF2 below to continue from the
// state after the HMAC key block, so that an iteration costs two compressions instead of four.
std::string sha256_continue(Sha256State h, size_t already, std::string_view tail) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(tail.data());
    const size_t whole = tail.size() / 64;
    for (size_t i = 0; i < whole; ++i) compress(h, bytes + 64 * i);
    unsigned char last[128];
    const size_t rest = tail.size() - whole * 64;
    if (rest > 0) std::memcpy(last, bytes + whole * 64, rest);
    last[rest] = 0x80;
    const size_t padded = rest < 56 ? 64 : 128;
    std::memset(last + rest + 1, 0, padded - rest - 1);
    const uint64_t bits = static_cast<uint64_t>(already + tail.size()) * 8u;
    for (int i = 0; i < 8; ++i) last[padded - 1 - static_cast<size_t>(i)] = static_cast<unsigned char>(bits >> (8 * i));
    compress(h, last);
    if (padded == 128) compress(h, last + 64);
    std::string out(32, '\0');
    for (size_t i = 0; i < 8; ++i) {
        for (size_t j = 0; j < 4; ++j) out[4 * i + j] = static_cast<char>(h[i] >> (24 - 8 * j));
    }
    return out;
}

}  // namespace

bool pbkdf2_hmac_sha256(std::string_view password, std::string_view salt, uint32_t iterations, size_t length, std::string& out) {
    out.clear();
    if (iterations == 0 || length == 0 || length > 1024) return false;
    // HMAC's two key blocks, hashed once: every HMAC of the loop continues from these states (RFC 2104; RFC 8018 section 5.2)
    std::string key = password.size() > 64 ? sha256(password) : std::string(password);
    key.resize(64, '\0');
    unsigned char inner_block[64], outer_block[64];
    for (size_t i = 0; i < 64; ++i) {
        inner_block[i] = static_cast<unsigned char>(key[i] ^ 0x36);
        outer_block[i] = static_cast<unsigned char>(key[i] ^ 0x5c);
    }
    Sha256State inner = kSha256Start, outer = kSha256Start;
    compress(inner, inner_block);
    compress(outer, outer_block);
    const size_t blocks = (length + 31) / 32;
    for (size_t b = 1; b <= blocks; ++b) {
        std::string first(salt);
        first.push_back(static_cast<char>((b >> 24) & 0xFF));
        first.push_back(static_cast<char>((b >> 16) & 0xFF));
        first.push_back(static_cast<char>((b >> 8) & 0xFF));
        first.push_back(static_cast<char>(b & 0xFF));
        std::string u = sha256_continue(outer, 64, sha256_continue(inner, 64, first));
        std::string t = u;
        for (uint32_t i = 1; i < iterations; ++i) {
            u = sha256_continue(outer, 64, sha256_continue(inner, 64, u));
            for (size_t k = 0; k < 32; ++k) t[k] = static_cast<char>(t[k] ^ u[k]);
        }
        out += t;
    }
    out.resize(length);
    return true;
}

std::string hex_encode(std::string_view bytes) {
    static const char* const kHex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const auto u = static_cast<unsigned char>(c);
        out.push_back(kHex[u >> 4]);
        out.push_back(kHex[u & 15]);
    }
    return out;
}

std::string base64url_encode(std::string_view bytes) {
    static const char* const kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((bytes.size() * 4 + 2) / 3);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = (uint32_t{static_cast<unsigned char>(bytes[i])} << 16) | (uint32_t{static_cast<unsigned char>(bytes[i + 1])} << 8) | uint32_t{static_cast<unsigned char>(bytes[i + 2])};
        out.push_back(kAlphabet[v >> 18]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
        out.push_back(kAlphabet[v & 63]);
    }
    if (bytes.size() - i == 1) {
        const uint32_t v = uint32_t{static_cast<unsigned char>(bytes[i])} << 16;
        out.push_back(kAlphabet[v >> 18]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
    } else if (bytes.size() - i == 2) {
        const uint32_t v = (uint32_t{static_cast<unsigned char>(bytes[i])} << 16) | (uint32_t{static_cast<unsigned char>(bytes[i + 1])} << 8);
        out.push_back(kAlphabet[v >> 18]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
    }
    return out;
}

bool base64url_decode(std::string_view text, std::string& out) {
    out.clear();
    if (text.size() % 4 == 1) return false;
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        uint32_t v;
        if (c >= 'A' && c <= 'Z') v = static_cast<uint32_t>(c - 'A');
        else if (c >= 'a' && c <= 'z') v = static_cast<uint32_t>(c - 'a' + 26);
        else if (c >= '0' && c <= '9') v = static_cast<uint32_t>(c - '0' + 52);
        else if (c == '-') v = 62;
        else if (c == '_') v = 63;
        else return false;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

}  // namespace ants::server
