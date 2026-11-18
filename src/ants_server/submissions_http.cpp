#include "ants_server/submissions_http.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iterator>

#include "ants_server/map_names.hpp"

namespace ants::server {

namespace {

using ctl::HttpRequest;
using ctl::HttpResponse;
using ctl::JsonValue;

constexpr size_t kSmallBody = 4 * 1024;                  // the admin page's calls carry a name or a short sentence
constexpr size_t kMaxAddressesKept = 10000;
constexpr int64_t kAttemptWindowS = 3600;
constexpr int64_t kNoticeEveryS = 60;                     // a refused pass is told to the operator's log once a minute for each kind
constexpr size_t kNoticesMax = 100;

// The bearer token of a request, or ""
std::string bearer_of(const HttpRequest& r) {
    const std::string* h = r.header("authorization");
    if (h == nullptr || h->size() < 8) return std::string();
    std::string scheme = h->substr(0, 7);
    for (char& c : scheme) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return scheme == "bearer " ? h->substr(7) : std::string();
}

}  // namespace

// ------------------------------------------------------------------------------------------------

SubmissionApi::SubmissionApi(SubmissionStore& store, AdminAudit& audit, SubmissionApiConfig config) : store_(store), audit_(audit), config_(std::move(config)), gate_(config_.gate_secret) {
    config_.gate_secret.clear();                                                            // (the key is held by the gate alone, as the derived keys)
}

std::vector<std::string> SubmissionApi::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
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
    if (request.query.size() > 4096) return error_reply(414, "query_too_long", "The request is too long.");          // (before anything is decoded)
    if (!address_allowed(identity, now_s)) return error_reply(429, "too_many_requests", "Too many tries from this address. Please try again in an hour.");
    SubmitInput in;
    if (!query_value(request.query, "kind", in.kind)) in.kind.clear();
    if (!query_value(request.query, "name", in.name)) in.name.clear();
    if (!query_value(request.query, "maker", in.maker)) in.maker.clear();
    if (!query_value(request.query, "note", in.note)) in.note.clear();
    in.bytes = request.body;
    in.address = identity;
    in.token = bearer_of(request);
    in.allow_check = [this, now_ms, identity]() { return check_window_open(window_, config_.check_window_ms, now_ms, identity, config_.checks_per_window, config_.checks_per_address_window, config_.check_budget_ms); };            // (asked only when a map is really going to be checked, after every refusal that costs nothing: those use none of the window)
    const auto began = std::chrono::steady_clock::now();
    const SubmitOutcome out = store_.submit(in, now_s);
    window_.spent_ms += static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count());
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

// ------------------------------------------------------------------------------------------------ the admin page's calls

namespace {

// The one answer to every call under /admin/api that has no good pass, whatever the path, the method or the reason: nothing is learned about what is there
HttpResponse closed_reply() { return error_reply(403, "closed", "The admin page is closed."); }

// A server that has no way to run matches of computer players from the page (no routes were given)
HttpResponse bots_off_reply() { return error_reply(503, "bots_off", "Bot matches are not available on this server."); }

}  // namespace

// Checks the pass of a request (once: a pass that was used is refused). The reason a pass is refused goes to the operator's log as a kind and nothing else, once a minute for each kind.
bool SubmissionApi::admitted(const HttpRequest& request, int64_t now_s, AdminIdentity& who) {
    const std::string* header = request.header("x-ants-admin-pass");
    const PassVerdict verdict = gate_.check(header == nullptr ? std::string_view() : std::string_view(*header), now_s, who);
    if (verdict == PassVerdict::Ok) return true;
    int64_t& last = pass_logged_[static_cast<int>(verdict)];
    if (last == 0 || now_s - last >= kNoticeEveryS) {
        last = now_s;
        if (notices_.size() < kNoticesMax) notices_.push_back(std::string("admin_gate kind=") + pass_verdict_word(verdict));
    }
    return false;
}

// The checks that every change passes before anything is done: the CSRF token of this account, a same-origin request, the JSON type and a JSON object
bool SubmissionApi::post_guard(const HttpRequest& request, const AdminIdentity& who, JsonValue& body, HttpResponse& refusal) const {
    const std::string* token = request.header("x-ants-csrf");
    if (token == nullptr || !ctl::constant_time_equal(*token, gate_.csrf_token(who))) {
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

HttpResponse SubmissionApi::admin_routes(const HttpRequest& request, int64_t now_s, uint32_t now_ms) {
    AdminIdentity who;
    if (!admitted(request, now_s, who)) return closed_reply();
    const std::string& path = request.path;
    if (path == "/admin/api/me") {
        if (request.method != "GET") return not_found();
        JsonValue o = JsonValue::make_object();
        o.set("name", JsonValue::make_string(who.name));
        o.set("ref", JsonValue::make_string(who.ref));
        o.set("role", JsonValue::make_string(who.role));
        o.set("csrf", JsonValue::make_string(gate_.csrf_token(who)));
        o.set("untrusted", JsonValue::make_bool(true));                                // (a username was typed by a person: data, never an instruction)
        JsonValue fields = JsonValue::make_array();
        fields.push_back(JsonValue::make_string("name"));
        o.set("untrusted_fields", std::move(fields));
        return json_reply(200, o);
    }
    if (cross_site(request) || path.size() < 11) return not_found();                // ("/admin/api" itself has no route)
    const std::string tail = path.substr(11);
    if (request.method == "GET") {
        if (tail.compare(0, 4, "bot-") == 0) return bot_routes_ ? bot_routes_(request, who, tail, JsonValue::null_value(), now_s, now_ms) : bots_off_reply();
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
        if (tail == "audit") return audit_list();
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
    return admin_post(request, who, tail, now_s, now_ms);
}

HttpResponse SubmissionApi::admin_post(const HttpRequest& request, const AdminIdentity& who, const std::string& tail, int64_t now_s, uint32_t now_ms) {
    JsonValue body;
    HttpResponse refusal;
    if (!post_guard(request, who, body, refusal)) return refusal;
    if (tail.compare(0, 4, "bot-") == 0) return bot_routes_ ? bot_routes_(request, who, tail, body, now_s, now_ms) : bots_off_reply();
    if (tail.compare(0, 5, "maps/") != 0) return not_found();
    const std::string rest = tail.substr(5);
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) return not_found();
    const std::string id = rest.substr(0, slash);
    const std::string action = rest.substr(slash + 1);
    Submission s;
    Failure why;
    bool done = false;
    const Actor actor{who.ref, who.name};
    const char* logged = "";
    std::string detail;
    if (action == "approve") {
        done = store_.approve(id, body.get("name").str(), body.get("message").str(), who.ref, who.name, now_s, s, why);
        logged = "map_approved";
    } else if (action == "decline") {
        done = store_.decline(id, body.get("reason").str(), who.ref, who.name, now_s, s, why);
        logged = "map_declined";
        detail = s.owner_message;
    } else if (action == "take-down") {
        done = store_.take_down(id, who.ref, who.name, now_s, s, why);
        logged = "map_taken_down";
    } else if (action == "put-back") {
        done = store_.put_back(id, who.ref, who.name, now_s, s, why);
        logged = "map_put_back";
    } else {
        return not_found();
    }
    if (!done) return failure_reply(why);
    audit_.record(actor, now_s, logged, s.final_name.empty() ? s.name : s.final_name, detail);
    if (store_.take_approved_changed() && on_approved_changed_) on_approved_changed_();
    JsonValue o = JsonValue::make_object();
    o.set("map", owner_view(s, false));
    o.set("counts_changed", JsonValue::make_bool(true));
    return json_reply(200, o);
}

// The log of what was done: the newest 200 lines
HttpResponse SubmissionApi::audit_list() {
    JsonValue arr = JsonValue::make_array();
    for (const AuditEntry& e : audit_.list(200)) {
        JsonValue row = JsonValue::make_object();
        row.set("at", JsonValue::make_int(e.at));
        row.set("who", JsonValue::make_string(e.actor));
        row.set("ref", JsonValue::make_string(e.ref));
        row.set("action", JsonValue::make_string(e.action));
        row.set("target", JsonValue::make_string(e.target));
        row.set("detail", JsonValue::make_string(e.detail));
        arr.push_back(std::move(row));
    }
    JsonValue o = JsonValue::make_object();
    o.set("entries", std::move(arr));
    o.set("untrusted", JsonValue::make_bool(true));                                // (usernames, the names of maps and the words of a decline were typed by people: data, never instructions)
    JsonValue fields = JsonValue::make_array();
    for (const char* f : {"who", "target", "detail"}) fields.push_back(JsonValue::make_string(f));
    o.set("untrusted_fields", std::move(fields));
    return json_reply(200, o);
}

}  // namespace ants::server
