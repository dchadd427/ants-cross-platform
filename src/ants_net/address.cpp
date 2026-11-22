// Addresses as text (see ants_net/address.hpp): the wss URL, and the numeric addresses that plain TCP may still go to. Pure code, in every build.
#include "ants_net/address.hpp"

#include <cctype>
#include <cstring>

namespace ants::net {

namespace {

constexpr size_t kMaxUrlChars = 512;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool host_char(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-'; }

// "a.b.c.d" with each part 0 - 255 and no leading garbage; at most 3 digits a part
bool parse_ipv4(const std::string& text, uint8_t out[4]) {
    size_t i = 0;
    for (int part = 0; part < 4; ++part) {
        if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
        unsigned value = 0;
        size_t digits = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            value = value * 10 + static_cast<unsigned>(text[i] - '0');
            ++i;
            if (++digits > 3 || value > 255) return false;
        }
        out[part] = static_cast<uint8_t>(value);
        if (part < 3) {
            if (i >= text.size() || text[i] != '.') return false;
            ++i;
        }
    }
    return i == text.size();
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// An IPv6 address: groups of 1 - 4 hex digits, one "::" at most, the last two groups may be a dotted IPv4 address
bool parse_ipv6(const std::string& text, uint8_t out[16]) {
    if (text.empty() || text.size() > 45) return false;
    uint16_t head[8] = {0};
    uint16_t tail[8] = {0};
    int head_count = 0;
    int tail_count = 0;
    bool compressed = false;
    size_t i = 0;
    if (text.compare(0, 2, "::") == 0) {
        compressed = true;
        i = 2;
    } else if (text[0] == ':') {
        return false;
    }
    bool expecting_group = i < text.size();
    while (i < text.size()) {
        uint16_t* groups = compressed ? tail : head;
        int& count = compressed ? tail_count : head_count;
        // a dotted IPv4 address ends the text
        const size_t dot = text.find('.', i);
        const size_t colon = text.find(':', i);
        if (dot != std::string::npos && (colon == std::string::npos || dot < colon)) {
            uint8_t v4[4];
            if (!parse_ipv4(text.substr(i), v4) || count + 2 > 8) return false;
            groups[count++] = static_cast<uint16_t>((v4[0] << 8) | v4[1]);
            groups[count++] = static_cast<uint16_t>((v4[2] << 8) | v4[3]);
            i = text.size();
            expecting_group = false;
            break;
        }
        unsigned value = 0;
        size_t digits = 0;
        while (i < text.size() && hex_value(text[i]) >= 0) {
            value = value * 16 + static_cast<unsigned>(hex_value(text[i]));
            ++i;
            if (++digits > 4) return false;
        }
        if (digits == 0 || count >= 8) return false;
        groups[count++] = static_cast<uint16_t>(value);
        expecting_group = false;
        if (i == text.size()) break;
        if (text[i] != ':') return false;
        ++i;
        if (i < text.size() && text[i] == ':') {                                  // "::"
            if (compressed) return false;
            compressed = true;
            ++i;
            expecting_group = false;
            if (i == text.size()) break;
        } else {
            expecting_group = true;
            if (i == text.size()) return false;                                   // a single colon at the end
        }
    }
    if (expecting_group) return false;
    const int total = head_count + tail_count;
    if (compressed ? total > 7 : total != 8) return false;
    uint16_t groups[8] = {0};
    for (int k = 0; k < head_count; ++k) groups[k] = head[k];
    for (int k = 0; k < tail_count; ++k) groups[8 - tail_count + k] = tail[k];
    for (int k = 0; k < 8; ++k) {
        out[2 * k] = static_cast<uint8_t>(groups[k] >> 8);
        out[2 * k + 1] = static_cast<uint8_t>(groups[k] & 0xFFu);
    }
    return true;
}

bool local_v4(const uint8_t* a) {
    const unsigned first = a[0];
    const unsigned second = a[1];
    return (first == 0 && a[1] == 0 && a[2] == 0 && a[3] == 0) || first == 127 || first == 10 || (first == 172 && second >= 16 && second <= 31) || (first == 192 && second == 168) ||
           (first == 169 && second == 254) || (first == 100 && second >= 64 && second <= 127);
}

}  // namespace

bool is_local_address(const std::string& numeric_in) {
    std::string numeric = numeric_in;
    const size_t scope = numeric.find('%');
    if (scope != std::string::npos) {
        if (scope + 1 >= numeric.size()) return false;                           // fe80::1%: a scope that says nothing
        numeric.resize(scope);                                                   // fe80::1%2: the interface does not change what the address is
    }
    if (numeric.empty()) return false;
    uint8_t b[16];
    if (numeric.find(':') == std::string::npos) {
        uint8_t v4[4];
        return parse_ipv4(numeric, v4) && local_v4(v4);
    }
    if (!parse_ipv6(numeric, b)) return false;
    bool zeros = true;
    for (int i = 0; i < 15; ++i) zeros = zeros && b[i] == 0;
    if (zeros && (b[15] == 0 || b[15] == 1)) return true;                        // :: and ::1
    if (b[0] == 0xFE && (b[1] & 0xC0u) == 0x80u) return true;                    // fe80::/10
    if ((b[0] & 0xFEu) == 0xFCu) return true;                                    // fc00::/7
    bool mapped = true;                                                          // ::ffff:a.b.c.d is the IPv4 address a.b.c.d
    for (int i = 0; i < 10; ++i) mapped = mapped && b[i] == 0;
    return mapped && b[10] == 0xFF && b[11] == 0xFF && local_v4(b + 12);
}

// ------------------------------------------------------------------------------------------------
// The wss URL

bool parse_wss_url(const std::string& text, WssUrl& out, std::string& why) {
    if (text.size() > kMaxUrlChars) {
        why = "The address is too long.";
        return false;
    }
    const std::string head = lower(text.substr(0, 6));
    if (head != "wss://") {
        why = lower(text.substr(0, 5)) == "ws://" ? "ws:// is not encrypted: write wss:// (the game sends no chat line or seat key over an unencrypted WebSocket)."
                                                  : "The address must start with wss://.";
        return false;
    }
    size_t i = 6;
    WssUrl url;
    if (i < text.size() && text[i] == '[') {                                    // an IPv6 address in brackets
        const size_t close = text.find(']', i);
        if (close == std::string::npos || close == i + 1) {
            why = "The address has a [ without its ].";
            return false;
        }
        for (size_t k = i + 1; k < close; ++k) {
            const char c = text[k];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' || c == '.')) {
                why = "The address in brackets is not an IPv6 address.";
                return false;
            }
        }
        url.host = lower(text.substr(i + 1, close - i - 1));
        i = close + 1;
    } else {
        const size_t begin = i;
        while (i < text.size() && host_char(text[i])) ++i;
        if (i == begin || text[begin] == '.' || text[begin] == '-') {
            why = "The address has no usable host name.";
            return false;
        }
        url.host = lower(text.substr(begin, i - begin));
        if (url.host.size() > 253) {
            why = "The host name is too long.";
            return false;
        }
    }
    if (i < text.size() && text[i] == ':') {                                    // an optional port: 1 - 5 digits, 1 - 65535
        ++i;
        const size_t begin = i;
        unsigned long port = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && i - begin < 6) port = port * 10 + static_cast<unsigned long>(text[i++] - '0');
        if (i == begin || port == 0 || port > 65535) {
            why = "The port must be a number from 1 to 65535.";
            return false;
        }
        url.port = static_cast<uint16_t>(port);
    }
    if (i < text.size() && text[i] != '/' && text[i] != '?') {                  // nothing else may follow the authority (no user information, no fragment)
        why = "The address has something after the host that is not a port, a path or a query.";
        return false;
    }
    std::string target = text.substr(i);
    for (const char ch : target) {
        const auto u = static_cast<unsigned char>(ch);
        if (u <= 0x20 || u >= 0x7f || u == '#') {
            why = "The path of the address has a blank, a control character, a '#' or a character that is not ASCII.";
            return false;
        }
    }
    if (target.empty()) target = "/";
    else if (target[0] == '?') target = "/" + target;
    url.target = target;
    out = url;
    return true;
}

std::string wss_url_text(const WssUrl& url) {
    const bool v6 = url.host.find(':') != std::string::npos;
    return "wss://" + (v6 ? "[" + url.host + "]" : url.host) + (url.port != kWssDefaultPort ? ":" + std::to_string(url.port) : std::string()) + url.target;
}

}  // namespace ants::net
