#include "ants_server/like_pass.hpp"

#include <algorithm>

#include "ants_ctl/http.hpp"
#include "ants_ctl/json.hpp"
#include "ants_server/crypto.hpp"

namespace ants::server {

namespace {

constexpr const char* kPassLabel = "ants-like-pass-v1";
constexpr const char* kCsrfLabel = "ants-like-csrf-v1";
constexpr size_t kMaxPassBytes = 512;                  // a real pass is about 150 bytes
constexpr size_t kMaxPayloadBytes = 256;
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

}  // namespace

LikeGate::LikeGate(std::string_view setting, size_t max_remembered) : max_remembered_(max_remembered) {
    const std::string secret = cut_blanks(setting);
    if (!gate_secret_is_good(secret)) return;
    pass_key_ = hmac_sha256(secret, kPassLabel);                      // (the raw digest: key = the secret, message = the label)
    csrf_key_ = hmac_sha256(secret, kCsrfLabel);
    open_ = true;
}

PassVerdict LikeGate::check(std::string_view header, int64_t now, PersonIdentity& out) {
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
    if (v.size() != 3) return PassVerdict::BadFields;                  // (exactly three members; a member that is missing is null below, which is of no kind that is asked for)
    if (!v.get("exp").is_int() || !v.get("nonce").is_string() || !v.get("ref").is_string()) return PassVerdict::BadFields;

    const int64_t exp = v.get("exp").as_int_or(0);
    if (exp <= now) return PassVerdict::Expired;
    if (exp > now + kPassSeconds + kPassSlackSeconds) return PassVerdict::BadFields;                 // (the hub does not make a pass that lasts this long)
    const std::string& ref = v.get("ref").str();
    const std::string& nonce = v.get("nonce").str();
    if (!all_lower_hex(ref, kReferenceChars) || !all_lower_hex(nonce, kNonceChars)) return PassVerdict::BadFields;

    while (!by_age_.empty() && by_age_.front().first <= now) {          // (a pass that has ended is refused above whatever is remembered: its nonce is not needed any more)
        seen_.erase(by_age_.front().second);
        by_age_.pop_front();
    }
    if (seen_.count(nonce) != 0) return PassVerdict::Replayed;
    if (seen_.size() >= max_remembered_) return PassVerdict::Full;
    seen_.insert(nonce);
    by_age_.emplace_back(exp, nonce);

    out.ref = ref;
    out.nonce = nonce;
    out.exp = exp;
    return PassVerdict::Ok;
}

std::string LikeGate::csrf_token(std::string_view ref) const {
    if (!open_) return std::string();
    return hex_encode(hmac_sha256(csrf_key_, std::string("csrf1\0", 6) + std::string(ref))).substr(0, 32);
}

}  // namespace ants::server
