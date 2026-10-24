#include "ants_server/upload_common.hpp"

#include <algorithm>
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

using ctl::HttpRequest;
using ctl::HttpResponse;
using ctl::JsonValue;

constexpr size_t kMaxFindingsKept = 20;
constexpr size_t kMaxFindingChars = 240;           // one finding's message at most (a map full of hills would name every one of them: the answer stays small)

uint32_t version_word(const std::string& bytes) {
    if (bytes.size() < 4) return 0;
    const auto b = [&](size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])); };
    return b(0) | (b(1) << 8) | (b(2) << 16) | (b(3) << 24);
}

// A message cut to kMaxFindingChars bytes, never inside a UTF-8 sequence, with "..." at the cut
std::string shortened(const std::string& text) {
    if (text.size() <= kMaxFindingChars) return text;
    size_t cut = kMaxFindingChars - 3;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "...";
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The identity of one address text, "" when it is none: an IPv4 address as it is, an IPv4-mapped IPv6 address as its IPv4 address, any other IPv6 address as its /64
std::string identity_of(const std::string& text) {
    if (text.empty() || text.size() > 45) return std::string();
    in6_addr v6;
    in_addr v4;
    if (inet_pton(AF_INET, text.c_str(), &v4) == 1) {
        char buf[INET_ADDRSTRLEN];
        return inet_ntop(AF_INET, &v4, buf, sizeof buf) != nullptr ? std::string(buf) : std::string();
    }
    if (inet_pton(AF_INET6, text.c_str(), &v6) == 1) {
        const auto* b = reinterpret_cast<const unsigned char*>(&v6);
        static const unsigned char kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (std::memcmp(b, kMapped, 12) == 0) {
            char buf[INET_ADDRSTRLEN];
            in_addr mapped;
            std::memcpy(&mapped, b + 12, 4);
            return inet_ntop(AF_INET, &mapped, buf, sizeof buf) != nullptr ? std::string(buf) : std::string();
        }
        return "v6:" + hex_encode(std::string(reinterpret_cast<const char*>(b), 8));
    }
    return std::string();        // (anything else a visitor could put in a header is no address)
}

}  // namespace

Failure make_failure(int status, const char* code, const char* message) {
    Failure f;
    f.status = status;
    f.code = code;
    f.message = message;
    return f;
}

// The cheap refusals that a real submission and a test map share: the file is a map of version 8 (Classic) or 9 (Extended), and it is the kind that the request says
bool kind_of_file(const std::string& claimed, const std::string& bytes, std::string& real_kind, Failure& why) {
    const uint32_t version = version_word(bytes);
    if (version != 8 && version != 9) {
        why = make_failure(400, "not_a_map", "This file is not an Ants map.");
        return false;
    }
    real_kind = version == 9 ? "extended" : "classic";
    if (real_kind != claimed) {
        why = make_failure(400, "kind_mismatch", "The file is not the kind of map that the request says.");
        return false;
    }
    return true;
}

// The warnings of a check, as the owner's page shows them (the notes are not kept)
JsonValue kept_findings(const JsonValue& report) {
    JsonValue out = JsonValue::make_array();
    const JsonValue& findings = report.get("findings");
    for (size_t i = 0; i < findings.size() && out.size() < kMaxFindingsKept; ++i) {
        const JsonValue& f = findings.at(i);
        if (f.get("severity").str() == "note") continue;
        JsonValue o = JsonValue::make_object();
        o.set("severity", JsonValue::make_string(f.get("severity").str()));
        o.set("message", JsonValue::make_string(shortened(f.get("message").str())));
        out.push_back(std::move(o));
    }
    return out;
}

bool parse_big_json(const std::string& text, JsonValue& out) {
    ctl::JsonLimits limits;
    limits.max_bytes = 8u * 1024u * 1024u;
    limits.max_elements = 200000;
    return ctl::parse_json(text, out, nullptr, limits);
}

HttpResponse json_reply(int status, const JsonValue& v) {
    HttpResponse r;
    r.status = status;
    r.content_type = "application/json";
    r.body = ctl::to_json(v);
    return r;
}

HttpResponse error_reply(int status, const char* code, const char* message) {
    JsonValue o = JsonValue::make_object();
    o.set("error", JsonValue::make_string(code));
    if (message != nullptr && message[0] != '\0') o.set("message", JsonValue::make_string(message));
    return json_reply(status, o);
}

// The path of a call that does not exist, and the answer to anything of the owner's that a visitor must not learn about
HttpResponse not_found() { return error_reply(404, "not_found", "Not found."); }

HttpResponse failure_reply(const Failure& f) {
    JsonValue o = JsonValue::make_object();
    o.set("error", JsonValue::make_string(f.code));
    o.set("message", JsonValue::make_string(f.message));
    if (f.extra.is_object()) {
        for (size_t i = 0; i < f.extra.size(); ++i) o.set(f.extra.key_at(i), f.extra.at(i));
    }
    return json_reply(f.status, o);
}

bool cross_site(const HttpRequest& r) {
    const std::string* site = r.header("sec-fetch-site");
    return site != nullptr && *site != "same-origin" && *site != "none";
}

// "application/json" or "application/octet-stream", with parameters allowed (charset)
bool content_type_is(const HttpRequest& r, const char* type) {
    const std::string* h = r.header("content-type");
    if (h == nullptr) return false;
    std::string v = h->substr(0, h->find(';'));
    while (!v.empty() && v.back() == ' ') v.pop_back();
    for (char& c : v) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return v == type;
}

bool percent_decode_text(const std::string& in, std::string& out) {
    out.clear();
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '%') {
            out.push_back(in[i]);
            continue;
        }
        if (i + 2 >= in.size()) return false;
        const int hi = hex_value(in[i + 1]);
        const int lo = hex_value(in[i + 2]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
    }
    return true;
}

bool query_value(const std::string& query, const std::string& name, std::string& out) {
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t amp = query.find('&', pos);
        const std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const size_t eq = part.find('=');
        const std::string key = part.substr(0, eq);
        if (key == name) return eq != std::string::npos && percent_decode_text(part.substr(eq + 1), out);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return false;
}

std::string client_identity(const HttpRequest& request, bool trust_header) {
    if (trust_header) {
        const std::string* h = request.header("x-client-address");
        if (h != nullptr) {
            const std::string id = identity_of(*h);
            if (!id.empty()) return id;
        }
    }
    return identity_of(request.peer);
}

bool check_window_open(CheckWindow& window, uint32_t window_ms, uint32_t now_ms, const std::string& identity, size_t per_window, size_t per_address, uint32_t budget_ms) {
    if (!window.started || static_cast<uint32_t>(now_ms - window.start_ms) >= window_ms) {
        window.started = true;
        window.start_ms = now_ms;
        window.count = 0;
        window.spent_ms = 0;
        window.by_address.clear();
    }
    const auto used = window.by_address.find(identity);
    if (used != window.by_address.end() && used->second >= per_address) return false;          // (this address has had its share: it cannot use up the window of the others)
    if (window.count >= per_window || window.spent_ms >= budget_ms) return false;
    ++window.count;
    ++window.by_address[identity];
    return true;
}

}  // namespace ants::server
