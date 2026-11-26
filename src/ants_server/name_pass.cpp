#include "ants_server/name_pass.hpp"

#include "ants_ctl/http.hpp"
#include "ants_net/name_rules.hpp"
#include "ants_server/admin_pass.hpp"
#include "ants_server/crypto.hpp"

namespace ants::server {

namespace {

constexpr const char* kPassLabel = "ants-name-pass-v1";
constexpr size_t kMacChars = 32;

// What Python's str.strip() cuts from the ends of the hub's setting, as far as ASCII goes (the key itself is printable ASCII, so these are all that can be there)
bool is_blank(char c) { return c == ' ' || (c >= '\t' && c <= '\r') || (c >= 0x1c && c <= 0x1f); }

std::string cut_blanks(std::string_view text) {
    size_t a = 0, b = text.size();
    while (a < b && is_blank(text[a])) ++a;
    while (b > a && is_blank(text[b - 1])) --b;
    return std::string(text.substr(a, b - a));
}

std::string mac_of(const std::string& key, std::string_view exp_text, std::string_view name) {
    std::string message = std::string("name1\0", 6);
    message.append(exp_text);
    message.push_back('\0');
    message.append(name);
    return hex_encode(hmac_sha256(key, message)).substr(0, kMacChars);
}

}  // namespace

NameGate::NameGate(std::string_view setting) {
    const std::string secret = cut_blanks(setting);
    if (!gate_secret_is_good(secret)) return;
    key_ = hmac_sha256(secret, kPassLabel);                           // (the raw digest: key = the secret, message = the label)
    open_ = true;
}

bool NameGate::check(std::string_view name, std::string_view pass, int64_t now) const {
    if (!open_ || name.empty() || !net::valid_pass_shape(pass)) return false;
    const size_t dot = pass.find('.');
    const std::string_view exp_text = pass.substr(0, dot);
    const std::string_view mac = pass.substr(dot + 1);
    if (!ctl::constant_time_equal(std::string(mac), mac_of(key_, exp_text, name))) return false;          // (before anything else is read)
    int64_t exp = 0;
    for (const char c : exp_text) exp = exp * 10 + (c - '0');                                             // (at most 12 digits: valid_pass_shape)
    return exp > now && exp <= now + kNamePassSeconds + kNamePassSlackSeconds;
}

std::string NameGate::make(std::string_view name, int64_t exp) const {
    if (!open_ || name.empty() || exp <= 0) return std::string();
    const std::string exp_text = std::to_string(exp);
    return exp_text + "." + mac_of(key_, exp_text, name);
}

}  // namespace ants::server
