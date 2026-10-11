// The routes of the match history (docs/REPLAYS.md "The match history", control.hpp): GET /history and GET /history/<id> on both doors, DELETE /history/<id> on the control door only.
#include <algorithm>
#include <cctype>
#include <set>

#include "ants_replay/replay.hpp"
#include "ants_server/control.hpp"

namespace ants::server {

using ctl::JsonValue;

namespace {

constexpr size_t kMaxHistoryOffset = 1000000;
constexpr size_t kMaxHistoryText = 40;               // the search text, decoded

ctl::HttpResponse reply(int status, const JsonValue& value) {
    ctl::HttpResponse r;
    r.status = status;
    r.content_type = "application/json";
    r.body = ctl::to_json(value);
    return r;
}

ctl::HttpResponse error_reply(int status, const std::string& message) {
    JsonValue o = JsonValue::make_object();
    o.set("error", JsonValue::make_string(message));
    return reply(status, o);
}

// The database does not answer: 503. The owner is told why (SQLite's words); the public door only that it is so.
ctl::HttpResponse unavailable(const HistoryStore& history, bool owner) {
    return error_reply(503, owner ? "the match history is unavailable: " + history.unavailable_reason() : std::string("the match history is unavailable"));
}

Recording recording_of(const ReplayStore* replays, const std::string& id) {
    const ReplayEntry* e = replays != nullptr ? replays->find(history_recording_file(id)) : nullptr;
    if (e == nullptr || !e->readable) return Recording::Removed;
    return entry_plays_here(*e) ? Recording::Watch : Recording::Old;
}

bool all_digits_up_to(const std::string& text, size_t max_chars) { return !text.empty() && text.size() <= max_chars && text.find_first_not_of("0123456789") == std::string::npos; }

// A value of the query as the browser sent it: percent escapes and '+' for a space. False for an escape that is not one or a character that no search text holds (the names are printable ASCII).
bool decode_query_value(const std::string& raw, std::string& out) {
    out.clear();
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '%') {
            if (i + 2 >= raw.size()) return false;
            const auto hex = [](char h) { return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1; };
            const int hi = hex(raw[i + 1]);
            const int lo = hex(raw[i + 2]);
            if (hi < 0 || lo < 0) return false;
            c = static_cast<char>(hi * 16 + lo);
            i += 2;
        } else if (c == '+') {
            c = ' ';
        }
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return false;
        out.push_back(c);
    }
    return true;
}

}  // namespace

// The query of GET /history: limit (1 - 100, default 50), offset, sort, format, result, who, rec, colour, mode, q. Every key once, every value one of its words; anything else is an error that names the key.
bool parse_history_query(const std::string& raw, HistoryQuery& q, std::string& error) {
    q = HistoryQuery();
    std::set<std::string> seen;
    size_t at = 0;
    while (at < raw.size()) {
        const size_t amp = raw.find('&', at);
        const std::string pair = raw.substr(at, amp == std::string::npos ? std::string::npos : amp - at);
        at = amp == std::string::npos ? raw.size() : amp + 1;
        if (pair.empty()) continue;
        const size_t eq = pair.find('=');
        const std::string key = pair.substr(0, eq);
        std::string value;
        if (eq == std::string::npos || !decode_query_value(pair.substr(eq + 1), value)) {
            error = "\"" + key + "\" must be written key=value, with the value as a browser writes it";
            return false;
        }
        if (!seen.insert(key).second) {
            error = "\"" + key + "\" is given twice";
            return false;
        }
        const auto bad = [&](const std::string& what) {
            error = "\"" + key + "\" must be " + what;
            return false;
        };
        if (key == "limit") {
            if (!all_digits_up_to(value, 4) || std::stoul(value) < 1 || std::stoul(value) > HistoryQuery::kMaxLimit) return bad("1 to " + std::to_string(HistoryQuery::kMaxLimit));
            q.limit = std::stoul(value);
        } else if (key == "offset") {
            if (!all_digits_up_to(value, 7) || std::stoul(value) > kMaxHistoryOffset) return bad("0 to " + std::to_string(kMaxHistoryOffset));
            q.offset = std::stoul(value);
        } else if (key == "sort") {
            if (value == "new") q.sort = HistoryQuery::Sort::New;
            else if (value == "old") q.sort = HistoryQuery::Sort::Old;
            else if (value == "score") q.sort = HistoryQuery::Sort::Score;
            else if (value == "kills") q.sort = HistoryQuery::Sort::Kills;
            else if (value == "hatched") q.sort = HistoryQuery::Sort::Hatched;
            else if (value == "long") q.sort = HistoryQuery::Sort::Long;
            else if (value == "short") q.sort = HistoryQuery::Sort::Short;
            else return bad("new, old, score, kills, hatched, long or short");
        } else if (key == "format") {
            if (value == "all") q.format = HistoryQuery::Format::All;
            else if (value == "1v1") q.format = HistoryQuery::Format::OneVsOne;
            else if (value == "team") q.format = HistoryQuery::Format::Teams;
            else if (value == "ffa") q.format = HistoryQuery::Format::Free;
            else return bad("all, 1v1, team or ffa");
        } else if (key == "result") {
            if (value == "all") q.result = HistoryQuery::Result::All;
            else if (value == "decided") q.result = HistoryQuery::Result::Decided;
            else if (value == "draw") q.result = HistoryQuery::Result::Draw;
            else if (value == "cut") q.result = HistoryQuery::Result::Cut;
            else return bad("all, decided, draw or cut");
        } else if (key == "who") {
            if (value == "all") q.who = HistoryQuery::Who::Anyone;
            else if (value == "people") q.who = HistoryQuery::Who::People;
            else if (value == "computers") q.who = HistoryQuery::Who::Computers;
            else return bad("all, people or computers");
        } else if (key == "rec") {
            if (value == "all") q.rec = HistoryQuery::Rec::All;
            else if (value == "watch") q.rec = HistoryQuery::Rec::Watch;
            else if (value == "removed") q.rec = HistoryQuery::Rec::Removed;
            else if (value == "old") q.rec = HistoryQuery::Rec::Old;
            else return bad("all, watch, removed or old");
        } else if (key == "mode") {
            if (value == "all") q.mode = HistoryQuery::Mode::All;
            else if (value == kHistoryModeScore) q.mode = HistoryQuery::Mode::HighestScore;
            else if (value == kHistoryMode187) q.mode = HistoryQuery::Mode::Kills187;
            else return bad("all, highest-score or 187");
        } else if (key == "colour") {
            std::string word = value;
            std::transform(word.begin(), word.end(), word.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (word == "all" || word == "any") q.colour = -1;
            else if (word == "green") q.colour = 0;
            else if (word == "red") q.colour = 1;
            else if (word == "blue") q.colour = 2;
            else if (word == "black") q.colour = 3;
            else return bad("all, green, red, blue or black");
        } else if (key == "q") {
            if (value.size() > kMaxHistoryText) return bad("at most " + std::to_string(kMaxHistoryText) + " characters");
            q.text = value;
        } else {
            error = "unknown parameter \"" + key + "\"; they are limit, offset, sort, format, result, who, rec, colour, mode and q";
            return false;
        }
    }
    return true;
}

namespace {

ctl::HttpResponse history_list_response(const HistoryStore& history, const ReplayStore* replays, const ctl::HttpRequest& request, bool owner) {
    HistoryQuery q;
    std::string error;
    if (!parse_history_query(request.query, q, error)) return error_reply(400, error);
    const HistoryPage page = history.query(q, [replays](const std::string& id) { return recording_of(replays, id); });
    if (!page.ok) return unavailable(history, owner);
    JsonValue list = JsonValue::make_array();
    for (const HistoryRecord& r : page.items) {
        JsonValue e = history_record_to_json(r, false);
        const Recording state = recording_of(replays, r.id);
        e.set("recording", JsonValue::make_string(recording_name(state)));
        if (state != Recording::Removed) e.set("file", JsonValue::make_string(history_recording_file(r.id)));
        list.push_back(std::move(e));
    }
    JsonValue o = JsonValue::make_object();
    o.set("history", std::move(list));
    o.set("count", JsonValue::make_int(static_cast<int64_t>(page.count)));
    o.set("total", JsonValue::make_int(static_cast<int64_t>(history.count())));
    o.set("offset", JsonValue::make_int(static_cast<int64_t>(q.offset)));
    o.set("limit", JsonValue::make_int(static_cast<int64_t>(q.limit)));
    o.set("keep_days", JsonValue::make_int(replays != nullptr ? replays->config().keep_days : 0));
    o.set("sim_rules", JsonValue::make_int(replay::kSimRules));
    return reply(200, o);
}

ctl::HttpResponse history_item_response(const HistoryStore& history, const ReplayStore* replays, const std::string& id, bool owner) {
    HistoryRecord record;
    if (!valid_history_id(id) || !history.find(id, record)) return history.available() ? error_reply(404, "no such match") : unavailable(history, owner);
    JsonValue o = history_record_to_json(record, true);
    const Recording state = recording_of(replays, id);
    o.set("recording", JsonValue::make_string(recording_name(state)));
    if (state != Recording::Removed) o.set("file", JsonValue::make_string(history_recording_file(id)));
    o.set("keep_days", JsonValue::make_int(replays != nullptr ? replays->config().keep_days : 0));
    return reply(200, o);
}

// GET /history[?query] and GET /history/<id>, on both doors (`owner`: the control door, which also says 405 where the public one says 404)
ctl::HttpResponse history_read(const RoomManager& rooms, const ctl::HttpRequest& request, bool owner) {
    const HistoryStore* history = rooms.history_store();
    const ReplayStore* replays = rooms.replay_store();
    if (history == nullptr) return error_reply(404, "this server keeps no match history");
    if (request.path == "/history") {
        if (request.method != "GET") return error_reply(owner ? 405 : 404, owner ? "method not allowed" : "not found");
        if (!history->available()) return unavailable(*history, owner);
        return history_list_response(*history, replays, request, owner);
    }
    static const std::string kPrefix = "/history/";
    if (request.path.compare(0, kPrefix.size(), kPrefix) != 0) return error_reply(404, "not found");
    const std::string id = request.path.substr(kPrefix.size());
    if (!request.query.empty() || !valid_history_id(id)) return error_reply(404, "no such match");     // (a name that cannot be an id is 404 before anything is looked up)
    if (request.method == "GET") {
        if (!history->available()) return unavailable(*history, owner);
        return history_item_response(*history, replays, id, owner);
    }
    return error_reply(owner ? 405 : 404, owner ? "method not allowed" : "not found");
}

}  // namespace

ctl::HttpResponse handle_public_history(const RoomManager& rooms, const ctl::HttpRequest& request) { return history_read(rooms, request, false); }

// GET /history[?query], GET /history/<id> and DELETE /history/<id> of the control interface (the secret is checked before this is called)
ctl::HttpResponse handle_history(RoomManager& rooms, const ctl::HttpRequest& request) {
    static const std::string kPrefix = "/history/";
    if (request.method != "DELETE" || request.path.compare(0, kPrefix.size(), kPrefix) != 0) return history_read(rooms, request, true);
    HistoryStore* history = rooms.history_store();
    if (history == nullptr) return error_reply(404, "this server keeps no match history");
    if (!history->available()) return unavailable(*history, true);
    const std::string id = request.path.substr(kPrefix.size());
    if (!request.query.empty() || !valid_history_id(id)) return error_reply(404, "no such match");
    const ReplayStore* replays = rooms.replay_store();
    const bool recording_kept = replays != nullptr && replays->find(history_recording_file(id)) != nullptr;      // (a match whose recording is still there would be counted again: a marker stays)
    std::string why;
    switch (history->remove(id, recording_kept, why)) {
        case HistoryStore::Removal::Missing: return history->available() ? error_reply(404, "no such match") : unavailable(*history, true);
        case HistoryStore::Removal::Failed: return error_reply(history->available() ? 500 : 503, why.empty() ? std::string("the record could not be deleted") : why);
        case HistoryStore::Removal::Done: break;
    }
    JsonValue o = JsonValue::make_object();
    o.set("deleted", JsonValue::make_string(id));
    return reply(200, o);
}

}  // namespace ants::server
