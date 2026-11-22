#include "ants_server/address_limits.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "ants_server/crypto.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace ants::server {

namespace {

constexpr uint32_t kSecondMs = 1000;
constexpr uint32_t kMinuteMs = 60u * 1000u;
constexpr uint32_t kHourMs = 3600u * 1000u;
constexpr size_t kWrongCodeOverCap = 10;        // the minute's count of wrong codes is kept this far past the allowance: the wait is already at its longest there

// The 16 bytes of an address text, IPv4 as an IPv4-mapped IPv6 address; false when the text is no address
bool address_bytes(const std::string& text, std::array<uint8_t, 16>& out) {
    if (text.empty() || text.size() > 45 || text.find_first_not_of("0123456789abcdefABCDEF:.") != std::string::npos) return false;      // (no scope id: some systems' inet_pton accept "fe80::1%2", others do not)
    in_addr v4;
    in6_addr v6;
    if (inet_pton(AF_INET, text.c_str(), &v4) == 1) {
        out.fill(0);
        out[10] = 0xFF;
        out[11] = 0xFF;
        std::memcpy(out.data() + 12, &v4, 4);
        return true;
    }
    if (inet_pton(AF_INET6, text.c_str(), &v6) == 1) {
        std::memcpy(out.data(), &v6, 16);
        return true;
    }
    return false;
}

bool is_mapped(const std::array<uint8_t, 16>& b) {
    static const unsigned char kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    return std::memcmp(b.data(), kMapped, 12) == 0;
}

}  // namespace

// An IPv4 address as it is, an IPv4-mapped IPv6 address as its IPv4 address, any other IPv6 address as its /64 (one person's machines share a /64, so a /64 is one address)
std::string address_identity(const std::string& text) {
    std::array<uint8_t, 16> b;
    if (!address_bytes(text, b)) return std::string();      // (anything else that a visitor could put in a header is no address)
    if (is_mapped(b)) {
        char buf[INET_ADDRSTRLEN];
        in_addr v4;
        std::memcpy(&v4, b.data() + 12, 4);
        return inet_ntop(AF_INET, &v4, buf, sizeof buf) != nullptr ? std::string(buf) : std::string();
    }
    return "v6:" + hex_encode(std::string(reinterpret_cast<const char*>(b.data()), 8));
}

bool set_address_limit(AddressLimits& limits, const std::string& key, int64_t value, std::string& why) {
    for (const AddressLimitField& f : kAddressLimitFields) {
        if (key != f.key) continue;
        if (value != 0 && (value < static_cast<int64_t>(f.min) || value > static_cast<int64_t>(f.max))) {
            why = key + " is 0 (no limit) or " + std::to_string(f.min) + " - " + std::to_string(f.max);
            return false;
        }
        limits.*f.member = static_cast<uint32_t>(value);
        return true;
    }
    why = "'" + key.substr(0, 64) + "' is no limit";
    return false;
}

TrustedProxies TrustedProxies::standard() {
    TrustedProxies p;
    std::string why;
    parse("127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, ::1, fc00::/7", p, why);
    return p;
}

bool TrustedProxies::parse(const std::string& text, TrustedProxies& out, std::string& why) {
    std::vector<std::string> tokens;
    std::string token;
    for (const char c : text) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!token.empty()) tokens.push_back(std::move(token));
            token.clear();
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) tokens.push_back(std::move(token));
    TrustedProxies result;
    if (tokens.size() == 1) {
        std::string lower = tokens[0];
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "none") {
            out = result;
            return true;
        }
    }
    for (const std::string& t : tokens) {
        const size_t slash = t.find('/');
        Net net;
        if (!address_bytes(t.substr(0, slash), net.bytes)) {
            why = "'" + t.substr(0, 64) + "' is no address or network";
            return false;
        }
        const bool v4 = is_mapped(net.bytes) && t.find(':') == std::string::npos;       // (a text with a colon is an IPv6 address, even one that has the IPv4-mapped form)
        const unsigned whole = v4 ? 32u : 128u;
        unsigned bits = whole;
        if (slash != std::string::npos) {
            const std::string digits = t.substr(slash + 1);
            if (digits.empty() || digits.size() > 3 || digits.find_first_not_of("0123456789") != std::string::npos) {
                why = "'" + t.substr(0, 64) + "' has no valid prefix length";
                return false;
            }
            bits = static_cast<unsigned>(std::stoul(digits));
            if (bits > whole) {
                why = "'" + t.substr(0, 64) + "' has a prefix longer than its address";
                return false;
            }
        }
        net.prefix = v4 ? bits + 96u : bits;
        result.nets_.push_back(net);
    }
    out = std::move(result);
    return true;
}

bool TrustedProxies::contains(const std::string& address) const {
    std::array<uint8_t, 16> b;
    if (nets_.empty() || !address_bytes(address, b)) return false;
    for (const Net& net : nets_) {
        const unsigned whole_bytes = net.prefix / 8u;
        const unsigned rest = net.prefix % 8u;
        if (std::memcmp(b.data(), net.bytes.data(), whole_bytes) != 0) continue;
        if (rest != 0) {
            const unsigned mask = (0xFFu << (8u - rest)) & 0xFFu;
            if ((b[whole_bytes] & mask) != (net.bytes[whole_bytes] & mask)) continue;
        }
        return true;
    }
    return false;
}

std::string door_address(const std::string& peer, const std::string& header, const TrustedProxies& proxies) {
    if (!proxies.contains(peer)) return peer;                                     // (a header from anyone else is no news)
    return address_identity(header).empty() ? peer : header;                     // (a proxy that cannot say whose it is: the proxy's own address, one count for all such connections)
}

size_t SlidingWindow::count(uint32_t now_ms, uint32_t span_ms) {
    while (!times_.empty() && static_cast<uint32_t>(now_ms - times_.front()) >= span_ms) times_.pop_front();
    return times_.size();
}

void SlidingWindow::add(uint32_t now_ms, size_t keep) {
    times_.push_back(now_ms);
    while (times_.size() > std::max<size_t>(keep, 1)) times_.pop_front();
}

AddressGuard::Seen& AddressGuard::seen(const std::string& id, uint32_t now_ms) {
    auto it = seen_.find(id);
    if (it == seen_.end()) {
        if (seen_.size() >= kMaxTrackedAddresses) {
            sweep(now_ms);
            if (seen_.size() >= kMaxTrackedAddresses) {                           // (every one is busy: the one that was seen the longest ago gives its place up)
                auto oldest = seen_.begin();
                for (auto k = seen_.begin(); k != seen_.end(); ++k) {
                    if (static_cast<uint32_t>(now_ms - k->second.last_ms) > static_cast<uint32_t>(now_ms - oldest->second.last_ms)) oldest = k;
                }
                seen_.erase(oldest);
            }
        }
        it = seen_.emplace(id, Seen()).first;
    }
    it->second.last_ms = now_ms;
    return it->second;
}

bool AddressGuard::allow_connection(const std::string& id, uint32_t now_ms) {
    if (id.empty() || limits_.connections_per_second == 0) return true;
    Seen& s = seen(id, now_ms);
    if (s.connections.count(now_ms, kSecondMs) >= limits_.connections_per_second) {
        ++refusals_.connections;
        return false;
    }
    s.connections.add(now_ms, limits_.connections_per_second);
    return true;
}

AddressGuard::RoomVerdict AddressGuard::allow_room(const std::string& id, size_t open_now, uint32_t now_ms) {
    if (id.empty()) return RoomVerdict::Allowed;
    if (limits_.rooms_open != 0 && open_now >= limits_.rooms_open) {
        ++refusals_.rooms_open;
        return RoomVerdict::TooMany;
    }
    if (limits_.rooms_per_minute != 0 && seen(id, now_ms).rooms.count(now_ms, kMinuteMs) >= limits_.rooms_per_minute) {
        ++refusals_.rooms_rate;
        return RoomVerdict::TooFast;
    }
    return RoomVerdict::Allowed;
}

void AddressGuard::note_room_made(const std::string& id, uint32_t now_ms) {
    if (id.empty() || limits_.rooms_per_minute == 0) return;
    seen(id, now_ms).rooms.add(now_ms, limits_.rooms_per_minute);
}

// How long an answer waits when the address has said `before` wrong codes in the last minute
static uint32_t hold_after(size_t before, uint32_t allowance) {
    if (before < allowance) return 0;
    const size_t over = before - allowance + 1;
    return static_cast<uint32_t>(std::min<size_t>(kWrongCodeMaxHoldMs, over * kWrongCodeStepMs));
}

uint32_t AddressGuard::wrong_code(const std::string& id, uint32_t now_ms) {
    if (id.empty() || limits_.wrong_codes_per_minute == 0) return 0;
    Seen& s = seen(id, now_ms);
    const size_t before = s.wrong.count(now_ms, kMinuteMs);
    s.wrong.add(now_ms, static_cast<size_t>(limits_.wrong_codes_per_minute) + kWrongCodeOverCap + 1);
    return hold_after(before, limits_.wrong_codes_per_minute);
}

uint32_t AddressGuard::hold_now(const std::string& id, uint32_t now_ms) {
    if (id.empty() || limits_.wrong_codes_per_minute == 0) return 0;
    const auto found = seen_.find(id);
    return found == seen_.end() ? 0u : hold_after(found->second.wrong.count(now_ms, kMinuteMs), limits_.wrong_codes_per_minute);
}

bool AddressGuard::take_relay(const std::string& id, uint32_t now_ms) {
    if (id.empty() || limits_.relay_per_hour == 0) return true;
    Seen& s = seen(id, now_ms);
    if (s.relay.count(now_ms, kHourMs) >= limits_.relay_per_hour) {
        ++refusals_.relay;
        return false;
    }
    s.relay.add(now_ms, limits_.relay_per_hour);
    return true;
}

void AddressGuard::sweep(uint32_t now_ms) {
    for (auto it = seen_.begin(); it != seen_.end();) {
        Seen& s = it->second;
        const size_t left = s.connections.count(now_ms, kSecondMs) + s.rooms.count(now_ms, kMinuteMs) + s.wrong.count(now_ms, kMinuteMs) + s.relay.count(now_ms, kHourMs);
        if (left == 0) it = seen_.erase(it);
        else ++it;
    }
}

}  // namespace ants::server
