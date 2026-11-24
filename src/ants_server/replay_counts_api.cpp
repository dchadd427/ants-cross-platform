#include "ants_server/replay_counts_api.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>

#include "ants_ctl/json.hpp"

namespace ants::server {

namespace {

using ctl::HttpRequest;
using ctl::HttpResponse;
using ctl::JsonValue;

constexpr int64_t kNoticeEveryS = 60;                     // a refused pass is told to the operator's log once a minute for each kind
constexpr size_t kNoticesMax = 20;
constexpr size_t kMaxQueryBytes = 4096;

HttpResponse json_reply(int status, const JsonValue& value) {
    HttpResponse r;
    r.status = status;
    r.content_type = "application/json";
    r.body = ctl::to_json(value);
    return r;
}

HttpResponse error_reply(int status, const std::string& word) {
    JsonValue o = JsonValue::make_object();
    o.set("error", JsonValue::make_string(word));
    return json_reply(status, o);
}

HttpResponse no_content() {
    HttpResponse r;
    r.status = 204;
    return r;
}

// The ids of `ids=a,b,c` (the query of GET /counts/mine): the value is the ids with commas between, every id of the shape of a match id; no other key. False for anything else.
bool ids_of(const std::string& query, std::vector<std::string>& ids) {
    ids.clear();
    if (query.empty()) return true;
    if (query.compare(0, 4, "ids=") != 0) return false;
    size_t at = 4;
    while (at <= query.size()) {
        const size_t comma = query.find(',', at);
        const std::string id = query.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!valid_history_id(id)) return false;
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        if (ids.size() > ReplayCountsApi::kMaxIds) return false;
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return true;
}

const char* kind_word(PassVerdict v) {
    switch (v) {
        case PassVerdict::NoKey: return "no_key";
        case PassVerdict::Full: return "full";
        default: return "bad_pass";
    }
}

}  // namespace

ReplayCountsApi::ReplayCountsApi(HistoryStore& history, const ReplayStore* replays, ReplayCountsConfig config)
    : history_(history), replays_(replays), clock_s_(std::move(config.clock_s)), gate_(config.gate_secret) {
    if (!clock_s_) clock_s_ = []() { return static_cast<int64_t>(std::time(nullptr)); };
}

std::vector<std::string> ReplayCountsApi::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    return out;
}

void ReplayCountsApi::notice(const std::string& kind, int64_t now_s, const std::string& text) {
    int64_t& at = notice_at_[kind];
    if (at != 0 && now_s - at < kNoticeEveryS) return;
    at = now_s;
    if (notices_.size() < kNoticesMax) notices_.push_back(text);
}

bool ReplayCountsApi::playable(const std::string& id) const {
    const ReplayEntry* e = replays_ != nullptr ? replays_->find(history_recording_file(id)) : nullptr;
    return e != nullptr && (e->pending || (e->readable && entry_plays_here(*e)));        // (a file whose head is not read yet, in the first minutes after a start, is there: the list shows it as one that can be watched)
}

bool ReplayCountsApi::take_play(uint32_t now_ms) {
    if (!plays_armed_) {
        plays_armed_ = true;
        plays_.tokens = kPlayBurst;
        plays_.at_ms = now_ms;
    }
    const uint32_t elapsed = now_ms - plays_.at_ms;                                        // (unsigned: a clock that wraps still gives the time that passed)
    plays_.at_ms = now_ms;
    plays_.tokens = std::min(kPlayBurst, plays_.tokens + elapsed * kPlaysPerSecond / 1000.0);
    if (plays_.tokens < 1) return false;
    plays_.tokens -= 1;
    return true;
}

bool ReplayCountsApi::take_person(const std::string& ref, uint32_t now_ms) {
    if (persons_.size() >= kPersonsKept && persons_.count(ref) == 0) {
        for (auto it = persons_.begin(); it != persons_.end();) {                           // (a person whose budget is full again is as good as unknown)
            const double back = std::min(kPersonBurst, it->second.tokens + (now_ms - it->second.at_ms) * kPersonPerSecond / 1000.0);
            it = back >= kPersonBurst ? persons_.erase(it) : std::next(it);
        }
        if (persons_.size() >= kPersonsKept) return false;                                  // (that many people at once is a flood: nobody new is let in until some budgets are full again)
    }
    auto it = persons_.find(ref);
    if (it == persons_.end()) it = persons_.emplace(ref, Bucket{kPersonBurst, now_ms}).first;
    Bucket& b = it->second;
    const uint32_t elapsed = now_ms - b.at_ms;
    b.at_ms = now_ms;
    b.tokens = std::min(kPersonBurst, b.tokens + elapsed * kPersonPerSecond / 1000.0);
    if (b.tokens < 1) return false;
    b.tokens -= 1;
    return true;
}

bool ReplayCountsApi::person_of(const HttpRequest& request, int64_t now_s, PersonIdentity& who, HttpResponse& refusal) {
    const std::string* header = request.header("x-ants-like-pass");
    const PassVerdict verdict = gate_.check(header != nullptr ? std::string_view(*header) : std::string_view(), now_s, who);
    if (verdict == PassVerdict::Ok) return true;
    if (verdict == PassVerdict::NoKey) {
        notice("no_key", now_s, "replay counts: likes are OFF (ANTS_ADMIN_GATE_SECRET is not set, or is not 32 to 256 printable characters without a blank, or is the sample key of the documents)");
        refusal = error_reply(503, "likes_off");
    } else if (verdict == PassVerdict::Full) {
        notice("full", now_s, "replay counts: the table of used passes is full, no pass is accepted until some have ended");
        refusal = error_reply(503, "busy");
    } else {
        if (verdict != PassVerdict::NoPass) notice(kind_word(verdict), now_s, std::string("replay counts: a pass was refused (") + pass_verdict_word(verdict) + ")");
        refusal = error_reply(401, "not_signed_in");
    }
    return false;
}

HttpResponse ReplayCountsApi::handle(const HttpRequest& request, uint32_t now_ms) {
    const std::string& path = request.path;
    static const std::string kPrefix = "/counts/";
    if (path.compare(0, kPrefix.size(), kPrefix) != 0) return error_reply(404, "not found");
    const std::string rest = path.substr(kPrefix.size());
    const int64_t now_s = clock_s_();
    if (rest == "mine") {
        if (request.method != "GET") return error_reply(405, "method");
        if (!request.body.empty() || request.query.size() > kMaxQueryBytes) return error_reply(400, "request");
        return mine(request, now_s);
    }
    // everything else is a POST of nothing: no query, no body, and never a request that says it came from another site
    const size_t slash = rest.find('/');
    const std::string kind = rest.substr(0, slash);
    const std::string id = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
    const bool known = kind == "play" || kind == "like" || kind == "unlike" || kind == "forget";
    if (!known || (kind == "forget") != (slash == std::string::npos)) return error_reply(404, "not found");
    if (!request.query.empty() || (kind != "forget" && !valid_history_id(id))) return error_reply(404, "not found");
    if (request.method != "POST") return error_reply(405, "method");
    if (!request.body.empty()) return error_reply(400, "request");
    const std::string* site = request.header("sec-fetch-site");
    if (site != nullptr && *site != "same-origin" && *site != "none") return error_reply(403, "cross_site");
    if (!history_.available()) return error_reply(503, "unavailable");
    if (kind == "play") return play(id, now_ms);
    return change(request, kind, id, now_s, now_ms);
}

HttpResponse ReplayCountsApi::play(const std::string& id, uint32_t now_ms) {
    if (!take_play(now_ms)) return error_reply(429, "rate");
    if (!playable(id)) return error_reply(404, "not found");
    switch (history_.add_play(id)) {
        case HistoryStore::Counted::Done: return no_content();
        case HistoryStore::Counted::Failed: return error_reply(503, "unavailable");
        default: return error_reply(404, "not found");
    }
}

HttpResponse ReplayCountsApi::mine(const HttpRequest& request, int64_t now_s) {
    PersonIdentity who;
    HttpResponse refusal;
    if (!person_of(request, now_s, who, refusal)) return refusal;
    std::vector<std::string> ids;
    if (!ids_of(request.query, ids)) return error_reply(400, "ids");
    if (!history_.available()) return error_reply(503, "unavailable");
    std::vector<std::string> liked;
    if (!history_.liked_among(who.ref, ids, liked)) return error_reply(503, "unavailable");
    JsonValue o = JsonValue::make_object();
    o.set("signed_in", JsonValue::make_bool(true));
    JsonValue list = JsonValue::make_array();
    for (const std::string& id : liked) list.push_back(JsonValue::make_string(id));
    o.set("liked", std::move(list));
    o.set("csrf", JsonValue::make_string(gate_.csrf_token(who.ref)));
    return json_reply(200, o);
}

HttpResponse ReplayCountsApi::change(const HttpRequest& request, const std::string& kind, const std::string& id, int64_t now_s, uint32_t now_ms) {
    PersonIdentity who;
    HttpResponse refusal;
    if (!person_of(request, now_s, who, refusal)) return refusal;
    const std::string* token = request.header("x-ants-csrf");
    if (token == nullptr || !ctl::constant_time_equal(*token, gate_.csrf_token(who.ref))) return error_reply(403, "csrf");
    if (kind == "forget") {
        size_t removed = 0;
        if (!history_.forget_likes(who.ref, removed)) return error_reply(503, "unavailable");
        JsonValue o = JsonValue::make_object();
        o.set("removed", JsonValue::make_int(static_cast<int64_t>(removed)));
        return json_reply(200, o);
    }
    if (!take_person(who.ref, now_ms)) return error_reply(429, "rate");
    const bool liking = kind == "like";
    if (liking && !playable(id)) return error_reply(404, "not found");                      // (nothing new is liked on a recording that is gone or that this version cannot play; a like can always be taken back)
    uint32_t likes = 0;
    const HistoryStore::Counted done = liking ? history_.like(id, who.ref, likes) : history_.unlike(id, who.ref, likes);
    switch (done) {
        case HistoryStore::Counted::Done: break;
        case HistoryStore::Counted::Limit: return error_reply(409, "limit");
        case HistoryStore::Counted::Failed: return error_reply(503, "unavailable");
        case HistoryStore::Counted::Unknown: return error_reply(404, "not found");
    }
    JsonValue o = JsonValue::make_object();
    o.set("liked", JsonValue::make_bool(liking));
    o.set("likes", JsonValue::make_int(likes));
    return json_reply(200, o);
}

}  // namespace ants::server
