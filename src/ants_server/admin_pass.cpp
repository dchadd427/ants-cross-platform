#include "ants_server/admin_pass.hpp"

#include <algorithm>

#include "ants_ctl/http.hpp"
#include "ants_ctl/json.hpp"
#include "ants_server/crypto.hpp"

namespace ants::server {

namespace {

constexpr const char* kPassLabel = "ants-admin-pass-v1";
constexpr const char* kCsrfLabel = "ants-admin-csrf-server-v1";
constexpr size_t kMaxPassBytes = 1024;                 // a real pass is about 190 bytes
constexpr size_t kMaxPayloadBytes = 512;
constexpr size_t kMaxNameBytes = 64;                   // a Discord username has 32 characters at the most
constexpr size_t kMacChars = 64;
constexpr size_t kReferenceChars = 16;
constexpr size_t kNonceChars = 16;

// What Python's str.strip() cuts from the ends of the hub's setting, as far as ASCII goes (the key itself is printable ASCII, so these are all that can be there)
bool is_blank(char c) { return c == ' ' || (c >= '\t' && c <= '\r') || (c >= 0x1c && c <= 0x1f); }

std::string cut_blanks(std::string_view text) {
    size_t a = 0, b = text.size();
    while (a < b && is_blank(text[a])) ++a;
    while (b > a && is_blank(text[b - 1])) --b;
    return std::string(text.substr(a, b - a));
}

bool all_lower_hex(std::string_view s, size_t length) {
    if (s.size() != length) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// A name that is only digits and as long as a Discord id is never shown as a name (an id is like a password): the reference stands in for it
bool looks_like_an_id(const std::string& s) {
    return s.size() >= 15 && s.size() <= 20 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool plain_name(const std::string& s) {
    if (s.size() > kMaxNameBytes || !ctl::is_valid_utf8(s)) return false;
    return std::none_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; });
}

}  // namespace

const char* pass_verdict_word(PassVerdict verdict) {
    switch (verdict) {
        case PassVerdict::Ok: return "ok";
        case PassVerdict::NoKey: return "no_key";
        case PassVerdict::NoPass: return "no_pass";
        case PassVerdict::Malformed: return "malformed";
        case PassVerdict::BadPass: return "bad_pass";
        case PassVerdict::Expired: return "expired";
        case PassVerdict::BadFields: return "bad_fields";
        case PassVerdict::BadRole: return "bad_role";
        case PassVerdict::Replayed: return "replay";
        case PassVerdict::Full: return "full";
    }
    return "malformed";
}

bool gate_secret_is_good(std::string_view cut_setting) {
    if (cut_setting.size() < kGateSecretMinChars || cut_setting.size() > kGateSecretMaxChars) return false;
    bool seen[256] = {false};
    size_t distinct = 0;
    for (const char c : cut_setting) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x21 || u > 0x7e) return false;                       // (printable ASCII, no blank)
        if (!seen[u]) {
            seen[u] = true;
            ++distinct;
        }
    }
    return distinct >= kGateSecretMinDistinct;
}

AdminGate::AdminGate(std::string_view setting, size_t max_remembered) : max_remembered_(max_remembered) {
    const std::string secret = cut_blanks(setting);
    given_ = !secret.empty();
    if (!gate_secret_is_good(secret)) return;
    pass_key_ = hmac_sha256(secret, kPassLabel);                      // (the raw digest: key = the secret, message = the label)
    csrf_key_ = hmac_sha256(secret, kCsrfLabel);
    open_ = true;
}

PassVerdict AdminGate::check(std::string_view header, int64_t now, AdminIdentity& out) {
    if (!open_) return PassVerdict::NoKey;
    if (header.empty()) return PassVerdict::NoPass;
    if (header.size() > kMaxPassBytes) return PassVerdict::Malformed;
    // v1.<payload>.<mac>
    const size_t first = header.find('.');
    const size_t last = header.rfind('.');
    if (first == std::string_view::npos || first == last || header.substr(0, first) != "v1") return PassVerdict::Malformed;
    const std::string_view signed_part = header.substr(0, last);       // "v1.<payload>"
    const std::string_view payload_text = header.substr(first + 1, last - first - 1);
    const std::string_view mac = header.substr(last + 1);
    if (mac.size() != kMacChars || payload_text.empty() || payload_text.find('.') != std::string_view::npos) return PassVerdict::Malformed;
    const std::string expected = hex_encode(hmac_sha256(pass_key_, signed_part));
    if (!ctl::constant_time_equal(std::string(mac), expected)) return PassVerdict::BadPass;          // (before anything else is read)

    std::string payload;
    ctl::JsonValue v;
    if (!base64url_decode(payload_text, payload) || payload.size() > kMaxPayloadBytes || !ctl::parse_json(payload, v, nullptr) || !v.is_object()) return PassVerdict::Malformed;
    if (v.size() != 5) return PassVerdict::BadFields;                  // (exactly five members; a member that is missing is null below, which is of no kind that is asked for)
    if (!v.get("exp").is_int() || !v.get("name").is_string() || !v.get("nonce").is_string() || !v.get("ref").is_string() || !v.get("role").is_string()) return PassVerdict::BadFields;

    const int64_t exp = v.get("exp").as_int_or(0);
    if (exp <= now) return PassVerdict::Expired;
    if (exp > now + kPassSeconds + kPassSlackSeconds) return PassVerdict::BadFields;                 // (the hub does not make a pass that lasts this long)
    const std::string& role = v.get("role").str();
    if (role != "admin" && role != "owner") return PassVerdict::BadRole;
    const std::string& ref = v.get("ref").str();
    const std::string& nonce = v.get("nonce").str();
    const std::string& name = v.get("name").str();
    if (!all_lower_hex(ref, kReferenceChars) || !all_lower_hex(nonce, kNonceChars) || !plain_name(name)) return PassVerdict::BadFields;

    while (!by_age_.empty() && by_age_.front().first <= now) {          // (a pass that has ended is refused above whatever is remembered: its nonce is not needed any more)
        seen_.erase(by_age_.front().second);
        by_age_.pop_front();
    }
    if (seen_.count(nonce) != 0) return PassVerdict::Replayed;
    if (seen_.size() >= max_remembered_) return PassVerdict::Full;
    seen_.insert(nonce);
    by_age_.emplace_back(exp, nonce);

    out.ref = ref;
    out.name = name.empty() || looks_like_an_id(name) ? ref : name;
    out.role = role;
    out.nonce = nonce;
    out.exp = exp;
    return PassVerdict::Ok;
}

std::string AdminGate::csrf_token(const AdminIdentity& who) const {
    if (!open_) return std::string();
    return hex_encode(hmac_sha256(csrf_key_, std::string("csrf1\0", 6) + who.ref)).substr(0, 32);
}

}  // namespace ants::server
