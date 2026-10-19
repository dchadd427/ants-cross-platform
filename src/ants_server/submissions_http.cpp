#include "ants_server/submissions_http.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iterator>

#include "ants_server/admin_accounts.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/map_names.hpp"
#include "ants_server/secret.hpp"

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

constexpr size_t kSmallBody = 4 * 1024;                  // the owner's calls carry a name or a short sentence
constexpr size_t kMaxAddressesKept = 10000;
constexpr int64_t kAttemptWindowS = 3600;
constexpr int64_t kSessionSlackS = 60;                    // a cookie that claims to last longer than the session length (plus this) is not one that this server made
constexpr const char* kCookieName = "ants_admin";
constexpr const char* kCookiePrefix = "v2.";

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

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

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

namespace {

// The bearer token of a request, or ""
std::string bearer_of(const HttpRequest& r) {
    const std::string* h = r.header("authorization");
    if (h == nullptr || h->size() < 8) return std::string();
    std::string scheme = h->substr(0, 7);
    for (char& c : scheme) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return scheme == "bearer " ? h->substr(7) : std::string();
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

}  // namespace

// ------------------------------------------------------------------------------------------------ small public helpers

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

namespace {

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

// ------------------------------------------------------------------------------------------------

SubmissionApi::SubmissionApi(SubmissionStore& store, AdminAccounts& accounts, SubmissionApiConfig config) : store_(store), accounts_(accounts), config_(std::move(config)) {
    if (config_.session_secret.size() < 32) config_.session_secret.clear();               // (a short secret is none: the owner's API stays off)
}

size_t SubmissionApi::max_body() { return SubmissionStore::max_upload_bytes("extended"); }

size_t SubmissionApi::body_limit(const HttpRequest& head) {
    if (head.method == "POST" && head.path == "/submissions") {
        std::string kind;
        return query_value(head.query, "kind", kind) && kind == "extended" ? SubmissionStore::max_upload_bytes("extended") : SubmissionStore::max_upload_bytes("classic");
    }
    return kSmallBody;
}

bool SubmissionApi::address_allowed(const std::string& identity, int64_t now_s) {
    if (attempts_.size() >= kMaxAddressesKept) {
        for (auto it = attempts_.begin(); it != attempts_.end();) {
            auto& times = it->second;
            times.erase(std::remove_if(times.begin(), times.end(), [&](int64_t t) { return t <= now_s - kAttemptWindowS; }), times.end());
            it = times.empty() ? attempts_.erase(it) : std::next(it);
        }
        if (attempts_.size() >= kMaxAddressesKept) return false;           // (that many addresses at once is a flood: nobody new is let in until some time has passed)
    }
    std::vector<int64_t>& times = attempts_[identity];
    times.erase(std::remove_if(times.begin(), times.end(), [&](int64_t t) { return t <= now_s - kAttemptWindowS; }), times.end());
    if (times.size() >= config_.attempts_per_hour) return false;
    times.push_back(now_s);
    return true;
}

bool SubmissionApi::check_window_open(uint32_t now_ms, const std::string& identity) {
    if (!window_started_ || static_cast<uint32_t>(now_ms - window_start_ms_) >= config_.check_window_ms) {
        window_started_ = true;
        window_start_ms_ = now_ms;
        window_count_ = 0;
        window_spent_ms_ = 0;
        window_by_address_.clear();
    }
    const auto used = window_by_address_.find(identity);
    if (used != window_by_address_.end() && used->second >= config_.checks_per_address_window) return false;          // (this address has had its share: it cannot use up the window of the others)
    if (window_count_ >= config_.checks_per_window || window_spent_ms_ >= config_.check_budget_ms) return false;
    ++window_count_;
    ++window_by_address_[identity];
    return true;
}

HttpResponse SubmissionApi::handle(const HttpRequest& request, int64_t now_s, uint32_t now_ms) {
    const std::string& path = request.path;
    if (path == "/admin/api" || path.compare(0, 11, "/admin/api/") == 0) return admin_routes(request, now_s, now_ms);
    if (path == "/submissions" && request.method == "POST") return submit(request, now_s, now_ms);
    if (path == "/submissions" || path.compare(0, 13, "/submissions/") == 0) return maker_routes(request, now_s, now_ms);
    return not_found();
}

// ------------------------------------------------------------------------------------------------ the player's calls

HttpResponse SubmissionApi::submit(const HttpRequest& request, int64_t now_s, uint32_t now_ms) {
    if (cross_site(request)) return error_reply(403, "cross_site", "This request must come from the site's own page.");
    if (!content_type_is(request, "application/octet-stream")) return error_reply(415, "bad_content_type", "Send the map file with the content type application/octet-stream.");
    const std::string identity = client_identity(request, config_.trust_client_header);
    if (identity.empty()) return error_reply(400, "no_address", "The server cannot tell where this request came from.");
    if (!address_allowed(identity, now_s)) return error_reply(429, "too_many_requests", "Too many tries from this address. Please try again in an hour.");
    SubmitInput in;
    if (!query_value(request.query, "kind", in.kind)) in.kind.clear();
    if (!query_value(request.query, "name", in.name)) in.name.clear();
    if (!query_value(request.query, "maker", in.maker)) in.maker.clear();
    if (!query_value(request.query, "note", in.note)) in.note.clear();
    if (request.query.size() > 4096) return error_reply(414, "query_too_long", "The request is too long.");
    in.bytes = request.body;
    in.address = identity;
    in.token = bearer_of(request);
    in.allow_check = [this, now_ms, identity]() { return check_window_open(now_ms, identity); };            // (asked only when a map is really going to be checked, after every refusal that costs nothing: those use none of the window)
    const auto began = std::chrono::steady_clock::now();
    const SubmitOutcome out = store_.submit(in, now_s);
    window_spent_ms_ += static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count());
    if (!out.ok) return failure_reply(out.fail);
    JsonValue o = JsonValue::make_object();
    o.set("submission", maker_view(out.submission));
    o.set("name_taken", JsonValue::make_bool(out.name_taken));
    o.set("token_is_new", JsonValue::make_bool(out.token_is_new));
    if (out.token_is_new) o.set("token", JsonValue::make_string(out.token));
    return json_reply(201, o);
}

HttpResponse SubmissionApi::maker_routes(const HttpRequest& request, int64_t now_s, uint32_t) {
    if (cross_site(request)) return error_reply(403, "cross_site", "This request must come from the site's own page.");
    const std::string& path = request.path;
    const std::string token = bearer_of(request);
    Failure why;
    if (path == "/submissions") {
        if (request.method != "GET") return error_reply(405, "method_not_allowed", "This address takes GET and POST.");
        std::vector<Submission> list;
        if (!store_.list_for_token(token, list, why)) return failure_reply(why);
        JsonValue arr = JsonValue::make_array();
        for (const Submission& s : list) arr.push_back(maker_view(s));
        JsonValue o = JsonValue::make_object();
        o.set("submissions", std::move(arr));
        return json_reply(200, o);
    }
    const std::string rest = path.substr(13);
    const size_t slash = rest.find('/');
    const std::string id = rest.substr(0, slash);
    const std::string action = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
    Submission s;
    if (action.empty()) {
        if (request.method != "GET") return error_reply(405, "method_not_allowed", "This address takes GET.");
        if (!store_.get_for_token(id, token, s, why)) return failure_reply(why);
        JsonValue o = JsonValue::make_object();
        o.set("submission", maker_view(s));
        return json_reply(200, o);
    }
    if (action == "withdraw") {
        if (request.method != "POST") return error_reply(405, "method_not_allowed", "This address takes POST.");
        if (!store_.withdraw(id, token, now_s, s, why)) return failure_reply(why);
        JsonValue o = JsonValue::make_object();
        o.set("submission", maker_view(s));
        return json_reply(200, o);
    }
    return not_found();
}

// ------------------------------------------------------------------------------------------------ the owner's session

namespace {

// The body of a call that names no path, and the answer to anything of the owner's that a visitor must not learn about, with the status of a refusal: a signed-in Moderator who asks for
// something of an Admin's gets exactly the bytes of a missing path (so the page cannot tell what an Admin may do), only the status is 403
HttpResponse forbidden_like_missing() {
    HttpResponse r = not_found();
    r.status = 403;
    return r;
}

std::string cookie_attributes(bool secure, int64_t max_age) {
    return "; Path=/admin; Max-Age=" + std::to_string(max_age) + "; HttpOnly; SameSite=Strict" + (secure ? "; Secure" : "");
}

bool all_digits(const std::string& s) { return !s.empty() && s.size() <= 18 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }

JsonValue user_json(const User& u, bool you) {
    JsonValue o = JsonValue::make_object();
    o.set("id", JsonValue::make_int(u.id));
    o.set("name", JsonValue::make_string(u.name));
    o.set("role", JsonValue::make_string(u.role));
    o.set("disabled", JsonValue::make_bool(u.disabled));
    o.set("has_not_chosen_password", JsonValue::make_bool(u.must_change));
    o.set("created_at", JsonValue::make_int(u.created_at));
    o.set("last_login_at", JsonValue::make_int(u.last_login_at));
    o.set("created_by", JsonValue::make_string(u.created_by));
    o.set("you", JsonValue::make_bool(you));
    return o;
}

}  // namespace

std::string SubmissionApi::make_cookie(const User& user, int64_t now_s, std::string& csrf) const {
    uint8_t raw[12];
    if (!random_bytes(raw, sizeof raw)) return std::string();
    const std::string nonce = base64url_encode(std::string_view(reinterpret_cast<const char*>(raw), sizeof raw));
    JsonValue p = JsonValue::make_object();
    p.set("u", JsonValue::make_int(user.id));
    p.set("g", JsonValue::make_int(user.session_gen));
    p.set("exp", JsonValue::make_int(now_s + config_.session_seconds));
    p.set("n", JsonValue::make_string(nonce));
    const std::string signed_part = std::string(kCookiePrefix) + base64url_encode(ctl::to_json(p));
    csrf = csrf_for(nonce, user);
    return std::string(kCookieName) + "=" + signed_part + "." + hex_encode(hmac_sha256(config_.session_secret, signed_part)) + cookie_attributes(config_.cookie_secure, config_.session_seconds);
}

std::string SubmissionApi::clear_cookie() const { return std::string(kCookieName) + "=" + cookie_attributes(config_.cookie_secure, 0); }

std::string SubmissionApi::csrf_for(const std::string& nonce, const User& user) const {
    return hex_encode(hmac_sha256(config_.session_secret, "csrf2|" + nonce + "|" + std::to_string(user.id) + "|" + std::to_string(user.session_gen))).substr(0, 32);
}

SubmissionApi::Session SubmissionApi::session_of(const HttpRequest& request, int64_t now_s) const {
    Session none;
    if (!owner_api_on()) return none;
    const std::string* cookie = request.header("cookie");
    if (cookie == nullptr || cookie->size() > 2048) return none;
    std::string value;
    size_t pos = 0;
    const std::string wanted = std::string(kCookieName) + "=";
    while (pos < cookie->size()) {
        size_t end = cookie->find(';', pos);
        if (end == std::string::npos) end = cookie->size();
        size_t b = pos;
        while (b < end && (*cookie)[b] == ' ') ++b;
        if (cookie->compare(b, wanted.size(), wanted) == 0) {
            value = cookie->substr(b + wanted.size(), end - b - wanted.size());
            break;
        }
        pos = end + 1;
    }
    if (value.compare(0, 3, kCookiePrefix) != 0) return none;
    const size_t dot = value.rfind('.');
    if (dot == std::string::npos || dot < 3) return none;
    const std::string signed_part = value.substr(0, dot);                         // "v2.<payload>"
    const std::string mac = value.substr(dot + 1);
    const std::string expected = hex_encode(hmac_sha256(config_.session_secret, signed_part));
    if (!ctl::constant_time_equal(mac, expected)) return none;
    std::string payload;
    JsonValue v;
    if (!base64url_decode(signed_part.substr(3), payload) || payload.size() > 512 || !ctl::parse_json(payload, v, nullptr)) return none;
    const int64_t exp = v.get("exp").as_int_or(0);
    if (exp <= now_s || exp > now_s + config_.session_seconds + kSessionSlackS) return none;
    const int64_t uid = v.get("u").as_int_or(0);
    const int64_t gen = v.get("g").as_int_or(0);
    const std::string nonce = v.get("n").str();
    if (uid <= 0 || gen <= 0 || nonce.empty() || nonce.size() > 64) return none;
    Session s;
    if (!accounts_.find(uid, s.user) || s.user.disabled || s.user.session_gen != gen) return none;          // (a reset, a disable, a removal or a new password made the cookie old)
    s.nonce = nonce;
    s.valid = true;
    return s;
}

Actor SubmissionApi::actor_of(const Session& who, const HttpRequest& request) const {
    Actor a;
    a.id = who.user.id;
    a.name = who.user.name;
    a.address = client_identity(request, config_.trust_client_header);
    return a;
}

// The checks that every change of a signed-in user passes before anything is done: the CSRF token of this very session, a same-origin request, the JSON type and a JSON object
bool SubmissionApi::post_guard(const HttpRequest& request, const Session& who, JsonValue& body, HttpResponse& refusal) const {
    const std::string* token = request.header("x-ants-csrf");
    if (token == nullptr || !ctl::constant_time_equal(*token, csrf_for(who.nonce, who.user))) {
        refusal = error_reply(403, "csrf", "The page's token is missing or old. Reload the page.");
        return false;
    }
    const std::string* site = request.header("sec-fetch-site");
    if (site != nullptr && *site != "same-origin") {
        refusal = error_reply(403, "csrf", "The page's token is missing or old. Reload the page.");
        return false;
    }
    if (!content_type_is(request, "application/json")) {
        refusal = error_reply(415, "bad_content_type", "Send JSON.");
        return false;
    }
    body = JsonValue::make_object();
    if (!request.body.empty() && (!ctl::parse_json(request.body, body, nullptr) || !body.is_object())) {
        refusal = error_reply(400, "bad_json", "The request is not a JSON object.");
        return false;
    }
    return true;
}

HttpResponse SubmissionApi::login(const HttpRequest& request, int64_t now_s, uint32_t now_ms) {
    const std::string* site = request.header("sec-fetch-site");
    if (site != nullptr && *site != "same-origin") return error_reply(403, "cross_site", "This request must come from the site's own page.");
    if (!content_type_is(request, "application/json")) return error_reply(415, "bad_content_type", "Send JSON.");
    const std::string identity = client_identity(request, config_.trust_client_header);
    if (identity.empty()) return error_reply(400, "no_address", "The server cannot tell where this request came from.");
    JsonValue body;
    if (!ctl::parse_json(request.body, body, nullptr) || !body.is_object()) return error_reply(400, "bad_json", "The request is not a JSON object.");
    const std::string username = body.get("username").str();
    const std::string password = body.get("password").str();
    const HttpResponse wrong = error_reply(401, "bad_login", "Wrong username or password.");
    if (username.size() > 64 || password.size() > kPasswordMaxBytes) return wrong;                  // (no name and no password of such a size exists: nothing is checked)
    User user;
    switch (accounts_.sign_in(username, password, identity, now_s, now_ms, user)) {
        case Attempt::Ok: break;
        case Attempt::Wrong: return wrong;
        case Attempt::Locked: return error_reply(429, "locked", "Too many tries. Wait a few minutes.");
        case Attempt::Busy: return error_reply(503, "busy", "The server is busy. Please try again in a minute.");
        case Attempt::Error: return error_reply(503, "storage_error", "The server could not check this just now. Please try again later.");
    }
    std::string csrf;
    const std::string cookie = make_cookie(user, now_s, csrf);
    if (cookie.empty()) return error_reply(503, "no_random", "The server could not start a session just now.");
    JsonValue o = JsonValue::make_object();
    o.set("signed_in", JsonValue::make_bool(true));
    o.set("name", JsonValue::make_string(user.name));
    o.set("role", JsonValue::make_string(user.role));
    o.set("must_change", JsonValue::make_bool(user.must_change));
    o.set("csrf", JsonValue::make_string(csrf));
    HttpResponse r = json_reply(200, o);
    r.set_cookies.push_back(cookie);
    return r;
}

HttpResponse SubmissionApi::logout(const HttpRequest& request, const Session& who) {
    const std::string* site = request.header("sec-fetch-site");
    if (site != nullptr && *site != "same-origin") return error_reply(403, "cross_site", "This request must come from the site's own page.");
    if (who.valid) {
        const std::string* token = request.header("x-ants-csrf");
        if (token == nullptr || !ctl::constant_time_equal(*token, csrf_for(who.nonce, who.user))) return error_reply(403, "csrf", "The page's token is missing or old. Reload the page.");
    }
    JsonValue o = JsonValue::make_object();
    o.set("signed_in", JsonValue::make_bool(false));
    HttpResponse r = json_reply(200, o);
    r.set_cookies.push_back(clear_cookie());
    return r;
}

HttpResponse SubmissionApi::change_password(const HttpRequest& request, const Session& who, int64_t now_s, uint32_t now_ms) {
    JsonValue body;
    HttpResponse refusal;
    if (!post_guard(request, who, body, refusal)) return refusal;
    const std::string identity = client_identity(request, config_.trust_client_header);
    if (identity.empty()) return error_reply(400, "no_address", "The server cannot tell where this request came from.");
    const std::string current = body.get("current").str();
    const std::string fresh = body.get("new").str();
    const auto bad_new = [](const std::string& reason) {
        HttpResponse r = error_reply(400, "bad_new_password", reason == "too_short" ? "The new password needs at least 12 characters." : reason == "same_as_name" ? "The new password cannot be your username." :
                                                              reason == "same_as_old" ? "Choose a password that is not the old one." : "That password cannot be used.");
        JsonValue o;
        ctl::parse_json(r.body, o, nullptr);
        o.set("reason", JsonValue::make_string(reason));
        r.body = ctl::to_json(o);
        return r;
    };
    const std::string problem = password_problem(fresh, who.user.name);
    if (!problem.empty()) return bad_new(problem);
    if (!who.user.must_change) {                                                   // (the old password proves who is typing; the starting password needs no proof, the sign-in a moment ago was it)
        if (current.size() > kPasswordMaxBytes) return error_reply(403, "bad_password", "That is not your current password.");
        switch (accounts_.confirm_password(who.user, current, identity, now_s, now_ms)) {
            case Attempt::Ok: break;
            case Attempt::Wrong: return error_reply(403, "bad_password", "That is not your current password.");
            case Attempt::Locked: return error_reply(429, "locked", "Too many tries. Wait a few minutes.");
            case Attempt::Busy: return error_reply(503, "busy", "The server is busy. Please try again in a minute.");
            case Attempt::Error: return error_reply(503, "storage_error", "The server could not check this just now. Please try again later.");
        }
        if (current == fresh) return bad_new("same_as_old");
    } else if (accounts_.password_is(who.user, fresh)) {
        return bad_new("same_as_old");                                             // (not the starting password)
    }
    int64_t gen = 0;
    Failure why;
    if (!accounts_.change_password(actor_of(who, request), who.user.id, fresh, now_s, gen, why)) return failure_reply(why);
    User renewed = who.user;
    renewed.session_gen = gen;
    renewed.must_change = false;
    std::string csrf;
    const std::string cookie = make_cookie(renewed, now_s, csrf);                  // (every older session of this user ended with the change; this one goes on with a new cookie)
    JsonValue o = JsonValue::make_object();
    o.set("ok", JsonValue::make_bool(true));
    o.set("must_change", JsonValue::make_bool(false));
    o.set("csrf", JsonValue::make_string(csrf));
    HttpResponse r = json_reply(200, o);
    r.set_cookies.push_back(cookie.empty() ? clear_cookie() : cookie);
    return r;
}

HttpResponse SubmissionApi::admin_routes(const HttpRequest& request, int64_t now_s, uint32_t now_ms) {
    if (!owner_api_on()) return not_found();
    const std::string& path = request.path;
    if (path == "/admin/api/login") return request.method == "POST" ? login(request, now_s, now_ms) : not_found();
    const Session who = session_of(request, now_s);
    if (path == "/admin/api/me") {
        if (request.method != "GET") return not_found();
        JsonValue o = JsonValue::make_object();
        o.set("signed_in", JsonValue::make_bool(who.valid));
        if (who.valid) {
            o.set("name", JsonValue::make_string(who.user.name));
            o.set("role", JsonValue::make_string(who.user.role));
            o.set("must_change", JsonValue::make_bool(who.user.must_change));
            o.set("csrf", JsonValue::make_string(csrf_for(who.nonce, who.user)));
        }
        return json_reply(200, o);
    }
    if (path == "/admin/api/logout") return request.method == "POST" ? logout(request, who) : not_found();
    if (!who.valid) return not_found();                                             // (a visitor gets what an unknown path gets)
    if (cross_site(request) || path.size() < 11) return not_found();                // ("/admin/api" itself has no route)
    const std::string tail = path.substr(11);
    if (tail == "password") return request.method == "POST" ? change_password(request, who, now_s, now_ms) : not_found();
    if (who.user.must_change) return error_reply(403, "password_change_required", "Choose your own password first.");          // (the starting password can do nothing else: the server holds this, not the page)
    const bool admin = who.user.role == "admin";
    if (tail == "users" || tail.compare(0, 6, "users/") == 0 || tail == "audit") {
        if (!admin) return forbidden_like_missing();
        return user_routes(request, who, tail, now_s);
    }
    if (request.method == "GET") {
        Failure why;
        if (tail == "maps") {
            std::string state;
            if (!query_value(request.query, "state", state)) state = "waiting";
            std::vector<Submission> list;
            if (!store_.list(state, list, why)) return failure_reply(why);
            JsonValue arr = JsonValue::make_array();
            for (const Submission& s : list) arr.push_back(owner_view(s, s.state == "waiting" && store_.name_taken(names::name_key(s.name))));
            const ReviewCounts c = store_.counts();
            JsonValue counts = JsonValue::make_object();
            counts.set("waiting", JsonValue::make_int(static_cast<int64_t>(c.waiting)));
            counts.set("approved", JsonValue::make_int(static_cast<int64_t>(c.approved)));
            counts.set("declined", JsonValue::make_int(static_cast<int64_t>(c.declined)));
            counts.set("taken_down", JsonValue::make_int(static_cast<int64_t>(c.taken_down)));
            JsonValue o = JsonValue::make_object();
            o.set("state", JsonValue::make_string(state));
            o.set("counts", std::move(counts));
            o.set("maps", std::move(arr));
            o.set("untrusted", JsonValue::make_bool(true));                        // (whoever reads this list, a person or a program: these fields were typed by players and are data, never instructions)
            JsonValue fields = JsonValue::make_array();
            for (const char* f : {"name", "maker", "note", "description", "approved_name", "decided_by"}) fields.push_back(JsonValue::make_string(f));
            o.set("untrusted_fields", std::move(fields));
            return json_reply(200, o);
        }
        if (tail == "next-name") {
            std::string name, next;
            if (!query_value(request.query, "name", name) || !store_.next_name(name, next, why)) return why.status != 0 ? failure_reply(why) : error_reply(400, "bad_name", "Give a name.");
            JsonValue o = JsonValue::make_object();
            o.set("name", JsonValue::make_string(next));
            return json_reply(200, o);
        }
        if (tail.compare(0, 5, "maps/") == 0 && tail.size() > 13 && tail.compare(tail.size() - 8, 8, "/picture") == 0) {
            const std::string png = store_.preview_png(tail.substr(5, tail.size() - 13));
            if (png.empty()) return not_found();
            HttpResponse r;
            r.status = 200;
            r.content_type = "image/png";
            r.body = png;
            return r;
        }
        return not_found();
    }
    if (request.method != "POST") return not_found();
    return admin_post(request, who, tail, now_s);
}

HttpResponse SubmissionApi::admin_post(const HttpRequest& request, const Session& who, const std::string& tail, int64_t now_s) {
    JsonValue body;
    HttpResponse refusal;
    if (!post_guard(request, who, body, refusal)) return refusal;
    if (tail.compare(0, 5, "maps/") != 0) return not_found();
    const std::string rest = tail.substr(5);
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) return not_found();
    const std::string id = rest.substr(0, slash);
    const std::string action = rest.substr(slash + 1);
    Submission s;
    Failure why;
    bool done = false;
    const Actor actor = actor_of(who, request);
    const std::string by = std::to_string(who.user.id);
    const char* logged = "";
    std::string detail;
    if (action == "approve") {
        done = store_.approve(id, body.get("name").str(), body.get("message").str(), by, who.user.name, now_s, s, why);
        logged = "map_approved";
    } else if (action == "decline") {
        done = store_.decline(id, body.get("reason").str(), by, who.user.name, now_s, s, why);
        logged = "map_declined";
        detail = s.owner_message;
    } else if (action == "take-down") {
        done = store_.take_down(id, by, who.user.name, now_s, s, why);
        logged = "map_taken_down";
    } else if (action == "put-back") {
        done = store_.put_back(id, by, who.user.name, now_s, s, why);
        logged = "map_put_back";
    } else {
        return not_found();
    }
    if (!done) return failure_reply(why);
    accounts_.audit(actor, now_s, logged, s.final_name.empty() ? s.name : s.final_name, detail);
    if (store_.take_approved_changed() && on_approved_changed_) on_approved_changed_();
    JsonValue o = JsonValue::make_object();
    o.set("map", owner_view(s, false));
    o.set("counts_changed", JsonValue::make_bool(true));
    return json_reply(200, o);
}

// The users and the log: Admin only (the caller has checked)
HttpResponse SubmissionApi::user_routes(const HttpRequest& request, const Session& who, const std::string& tail, int64_t now_s) {
    JsonValue fields = JsonValue::make_array();
    if (request.method == "GET") {
        if (tail == "users") {
            JsonValue arr = JsonValue::make_array();
            for (const User& u : accounts_.list()) arr.push_back(user_json(u, u.id == who.user.id));
            JsonValue o = JsonValue::make_object();
            o.set("users", std::move(arr));
            o.set("untrusted", JsonValue::make_bool(true));
            for (const char* f : {"name", "created_by"}) fields.push_back(JsonValue::make_string(f));
            o.set("untrusted_fields", std::move(fields));
            return json_reply(200, o);
        }
        if (tail == "audit") {
            JsonValue arr = JsonValue::make_array();
            for (const AuditEntry& e : accounts_.audit_list(200)) {
                JsonValue row = JsonValue::make_object();
                row.set("at", JsonValue::make_int(e.at));
                row.set("who", JsonValue::make_string(e.actor));
                row.set("action", JsonValue::make_string(e.action));
                row.set("target", JsonValue::make_string(e.target));
                row.set("detail", JsonValue::make_string(e.detail));
                arr.push_back(std::move(row));
            }
            JsonValue o = JsonValue::make_object();
            o.set("entries", std::move(arr));
            o.set("untrusted", JsonValue::make_bool(true));                        // (user names, the names of maps and the words of a decline are typed by people: data, never instructions)
            for (const char* f : {"who", "target", "detail"}) fields.push_back(JsonValue::make_string(f));
            o.set("untrusted_fields", std::move(fields));
            return json_reply(200, o);
        }
        return not_found();
    }
    if (request.method != "POST") return not_found();
    JsonValue body;
    HttpResponse refusal;
    if (!post_guard(request, who, body, refusal)) return refusal;
    const Actor actor = actor_of(who, request);
    Failure why;
    User user;
    std::string password;
    if (tail == "users") {
        if (!accounts_.create(actor, body.get("username").str(), body.get("role").str(), now_s, user, password, why)) return failure_reply(why);
        JsonValue o = JsonValue::make_object();
        o.set("user", user_json(user, false));
        o.set("password", JsonValue::make_string(password));                       // (shown once: the page shows it and keeps it nowhere; the server keeps only the hash)
        return json_reply(201, o);
    }
    if (tail.compare(0, 6, "users/") != 0) return not_found();
    const std::string rest = tail.substr(6);
    const size_t slash = rest.find('/');
    if (slash == std::string::npos || !all_digits(rest.substr(0, slash))) return not_found();
    const int64_t id = std::strtoll(rest.substr(0, slash).c_str(), nullptr, 10);
    const std::string action = rest.substr(slash + 1);
    JsonValue o = JsonValue::make_object();
    if (action == "reset") {
        if (!accounts_.reset(actor, id, now_s, password, why)) return failure_reply(why);
        o.set("password", JsonValue::make_string(password));
    } else if (action == "disable" || action == "enable") {
        if (!accounts_.set_disabled(actor, id, action == "disable", now_s, why)) return failure_reply(why);
    } else if (action == "remove") {
        if (!accounts_.remove(actor, id, now_s, why)) return failure_reply(why);
    } else {
        return not_found();
    }
    if (action != "remove" && accounts_.find(id, user)) o.set("user", user_json(user, user.id == who.user.id));
    o.set("ok", JsonValue::make_bool(true));
    return json_reply(200, o);
}

}  // namespace ants::server
