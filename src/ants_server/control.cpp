#include "ants_server/control.hpp"

#include <algorithm>

namespace ants::server {

using ctl::JsonValue;

namespace {

ctl::HttpResponse json_response(int status, const JsonValue& value) {
    ctl::HttpResponse r;
    r.status = status;
    r.content_type = "application/json";
    r.body = ctl::to_json(value);
    return r;
}

ctl::HttpResponse error_response(int status, const std::string& message) {
    JsonValue o = JsonValue::make_object();
    o.set("error", JsonValue::make_string(message));
    return json_response(status, o);
}

}  // namespace

JsonValue status_to_json(const RoomStatus& s) {
    JsonValue o = JsonValue::make_object();
    o.set("code", JsonValue::make_string(s.code));
    o.set("state", JsonValue::make_string(room_state_name(s.state)));
    o.set("map", JsonValue::make_string(s.map));
    o.set("fog", JsonValue::make_bool(s.fog));
    o.set("expected", JsonValue::make_int(s.expected));
    o.set("joined", JsonValue::make_int(s.joined));
    o.set("early_start", JsonValue::make_bool(s.early_start));
    o.set("leader", s.leader < 4 ? JsonValue::make_int(s.leader) : JsonValue::make_null());
    o.set("ignored_start_requests", JsonValue::make_int(s.ignored_start_requests));
    JsonValue players = JsonValue::make_array();
    for (size_t seat = 0; seat < s.names.size(); ++seat) {
        if (s.names[seat].empty()) continue;
        JsonValue p = JsonValue::make_object();
        p.set("seat", JsonValue::make_int(static_cast<int64_t>(seat)));
        p.set("name", JsonValue::make_string(s.names[seat]));
        bool is_bot = false;
        for (const RoomStatus::Bot& b : s.bots) is_bot = is_bot || b.seat == seat;
        p.set("bot", JsonValue::make_bool(is_bot));                  // a bot is a player of the room, and says so
        players.push_back(std::move(p));
    }
    o.set("players", std::move(players));
    // The computer players of the room (the specification's and the leader's fill); `joined` and `players` above count them: a bot is a player
    JsonValue bots = JsonValue::make_array();
    for (const RoomStatus::Bot& b : s.bots) {
        JsonValue row = JsonValue::make_object();
        row.set("seat", JsonValue::make_int(b.seat));
        const std::string pinned = b.style == "random" || b.style.empty() ? std::string() : ":" + b.style;       // (a pinned style is part of the text that --bot takes; a bot that draws its own is not named)
        row.set("bot", JsonValue::make_string(b.kind == "standard" ? b.level + pinned : b.kind + ":" + b.level));
        row.set("kind", JsonValue::make_string(b.kind));
        row.set("level", JsonValue::make_string(b.level));
        row.set("style", JsonValue::make_string(b.style.empty() ? "random" : b.style));
        row.set("name", JsonValue::make_string(b.name));
        row.set("fill", JsonValue::make_bool(b.fill));
        bots.push_back(std::move(row));
    }
    o.set("bots", std::move(bots));
    o.set("ticks", JsonValue::make_int(s.ticks));
    if (s.state == RoomState::Finished) {                           // the referee's state at the end of the match, as 16 hex digits (what every player's game stands at, at `ticks`)
        static const char kHex[] = "0123456789abcdef";
        std::string hex(16, '0');
        for (int i = 0; i < 16; ++i) hex[static_cast<size_t>(15 - i)] = kHex[(s.referee_hash >> (4 * i)) & 0xFu];
        o.set("state_hash", JsonValue::make_string(hex));
    }
    o.set("turns", JsonValue::make_int(s.turns));
    o.set("age_seconds", JsonValue::make_int(s.age_ms / 1000));
    o.set("reason", JsonValue::make_string(s.reason));
    // Reconnect (protocol 10): whether the room holds the seats of players whose connections are lost, its rules, who is missing now, the vote, and what happened (never a key)
    o.set("reconnect", JsonValue::make_bool(s.reconnect));
    o.set("hold_vote_seconds", JsonValue::make_int(s.vote_after_ms / 1000));
    o.set("max_pause_seconds", JsonValue::make_int(s.max_pause_ms / 1000));
    o.set("max_catch_up_seconds", JsonValue::make_int(s.max_catch_up_ms / 1000));
    o.set("resume_countdown_seconds", JsonValue::make_int(s.resume_countdown_ms / 1000));
    o.set("paused", JsonValue::make_bool(s.paused));
    o.set("resume_seconds", JsonValue::make_int(s.resume_s));
    JsonValue absent = JsonValue::make_array();
    for (const RoomStatus::Absent& a : s.absent) {
        JsonValue row = JsonValue::make_object();
        row.set("seat", JsonValue::make_int(a.seat));
        row.set("name", JsonValue::make_string(a.name));
        row.set("state", JsonValue::make_string(a.catching_up ? "catching_up" : "absent"));
        row.set("away_seconds", JsonValue::make_int(a.away_s));
        row.set("progress", JsonValue::make_int(a.progress));
        absent.push_back(std::move(row));
    }
    o.set("absent", std::move(absent));
    if (s.vote_seat < 4 && s.voters > 0) {                           // (a vote that nobody can cast, the persons being the ones who are gone and the rest bots, is no vote: null)
        JsonValue vote = JsonValue::make_object();
        vote.set("seat", JsonValue::make_int(s.vote_seat));
        vote.set("continue", JsonValue::make_int(s.votes_continue));
        vote.set("voters", JsonValue::make_int(s.voters));
        o.set("vote", std::move(vote));
    } else {
        o.set("vote", JsonValue::make_null());
    }
    o.set("paused_seconds", JsonValue::make_int(s.paused_s));
    o.set("rejoins", JsonValue::make_int(s.rejoins));
    o.set("drops_by_vote", JsonValue::make_int(s.drops_by_vote));
    o.set("drops_by_cap", JsonValue::make_int(s.drops_by_cap));
    o.set("rejoins_refused", JsonValue::make_int(s.rejoins_refused));
    o.set("catch_up_expired", JsonValue::make_int(s.catch_up_expired));
    o.set("streamed_bytes", JsonValue::make_int(static_cast<int64_t>(s.streamed_bytes)));
    JsonValue log = JsonValue::make_object();
    log.set("turns", JsonValue::make_int(s.log_turns));
    log.set("bytes", JsonValue::make_int(s.log_bytes));
    log.set("usable", JsonValue::make_bool(s.log_usable));
    o.set("log", std::move(log));
    if (s.state == RoomState::Finished) {
        JsonValue result = JsonValue::make_object();
        result.set("quitter", s.quitter < 4 ? JsonValue::make_int(s.quitter) : JsonValue::make_null());
        JsonValue rows = JsonValue::make_array();
        for (const RoomRow& r : s.rows) {
            JsonValue row = JsonValue::make_object();
            JsonValue names = JsonValue::make_array();
            names.push_back(JsonValue::make_string(r.name));
            if (r.second != 255) names.push_back(JsonValue::make_string(r.second_name));
            row.set("names", std::move(names));
            JsonValue seats = JsonValue::make_array();
            seats.push_back(JsonValue::make_int(r.first));
            if (r.second != 255) seats.push_back(JsonValue::make_int(r.second));
            row.set("seats", std::move(seats));
            row.set("score", JsonValue::make_int(r.score));
            row.set("lost", JsonValue::make_int(r.friendly_lost));
            row.set("killed", JsonValue::make_int(r.enemy_killed));
            row.set("hatched", JsonValue::make_int(r.new_hatched));
            row.set("winner", JsonValue::make_bool(r.winner));
            rows.push_back(std::move(row));
        }
        result.set("rows", std::move(rows));
        o.set("result", std::move(result));
    }
    return o;
}

bool spec_from_json(const JsonValue& body, RoomSpec& out, std::string& error) {
    if (!body.is_object()) {
        error = "the body must be a JSON object";
        return false;
    }
    RoomSpec spec = out;                                    // the caller's defaults (the server's own: RoomManager::default_spec); only what the body says is changed
    const JsonValue* map = body.find("map");
    if (map == nullptr || !map->is_string() || map->str().empty()) {
        error = "\"map\" (the map file name) is required";
        return false;
    }
    spec.map = map->str();
    auto number = [&](const char* key, int64_t lo, int64_t hi, int64_t& value) {
        const JsonValue* v = body.find(key);
        if (v == nullptr) return true;
        if (!v->is_int() || v->as_int_or(0) < lo || v->as_int_or(0) > hi) {
            error = std::string("\"") + key + "\" must be an integer from " + std::to_string(lo) + " to " + std::to_string(hi);
            return false;
        }
        value = v->as_int_or(0);
        return true;
    };
    int64_t players = 2;
    if (!number("players", 2, 4, players)) return false;
    spec.players = static_cast<uint8_t>(players);
    int64_t wait = spec.wait_ms / 1000;
    int64_t load = spec.load_ms / 1000;
    int64_t keep = spec.keep_ms / 1000;
    int64_t run = spec.run_ms / 1000;
    int64_t seed = 0;
    if (!number("wait_seconds", 1, 86400, wait) || !number("load_seconds", 1, 600, load) || !number("keep_seconds", 0, 86400, keep) || !number("max_run_seconds", 60, 86400, run)) return false;
    spec.wait_ms = static_cast<uint32_t>(wait * 1000);
    spec.load_ms = static_cast<uint32_t>(load * 1000);
    spec.keep_ms = static_cast<uint32_t>(keep * 1000);
    spec.run_ms = static_cast<uint32_t>(run * 1000);
    if (body.find("seed") != nullptr) {
        if (!number("seed", 0, 4294967295LL, seed)) return false;
        spec.has_seed = true;
        spec.seed = static_cast<uint32_t>(seed);
    }
    if (const JsonValue* v = body.find("fog")) {
        if (!v->is_bool()) {
            error = "\"fog\" must be true or false";
            return false;
        }
        spec.fog = v->as_bool_or(false);
    }
    if (const JsonValue* v = body.find("early_start")) {
        if (!v->is_bool()) {
            error = "\"early_start\" must be true or false";
            return false;
        }
        spec.early_start = v->as_bool_or(true);
    }
    // Reconnect (protocol 10): whether the room holds the seats of players whose connections are lost (the server's default when the body says nothing), how long a seat must have been away
    // before the others may vote on going on without it, and how long the match's pauses may last in all
    if (const JsonValue* v = body.find("reconnect")) {
        if (!v->is_bool()) {
            error = "\"reconnect\" must be true or false";
            return false;
        }
        spec.reconnect = v->as_bool_or(false);
    }
    int64_t vote_s = spec.vote_after_ms / 1000;
    int64_t pause_s = spec.max_pause_ms / 1000;
    if (!number("hold_vote_seconds", kMinVoteAfterMs / 1000, kMaxVoteAfterMs / 1000, vote_s) || !number("max_pause_seconds", kMinMaxPauseMs / 1000, kMaxMaxPauseMs / 1000, pause_s)) return false;
    spec.vote_after_ms = static_cast<uint32_t>(vote_s * 1000);
    spec.max_pause_ms = static_cast<uint32_t>(pause_s * 1000);
    int64_t catch_s = spec.max_catch_up_ms / 1000;
    int64_t resume_s = spec.resume_countdown_ms / 1000;
    if (!number("max_catch_up_seconds", kMinCatchUpMs / 1000, kMaxCatchUpLimitMs / 1000, catch_s) || !number("resume_countdown_seconds", 0, kMaxResumeCountdownMs / 1000, resume_s)) return false;
    spec.max_catch_up_ms = static_cast<uint32_t>(catch_s * 1000);
    spec.resume_countdown_ms = static_cast<uint32_t>(resume_s * 1000);
    // The computer players that sit in the room from the start (docs/BOTS.md B6): [{"seat": 2, "bot": "medium"}]; "bot" is what --bot takes after the seat ("easy", "medium", "hard",
    // "idle", "worker", "worker:easy", "hard:raider" (a pinned style), ...; "standard" is the default kind)
    if (const JsonValue* v = body.find("bots")) {
        if (!v->is_array()) {
            error = "\"bots\" must be an array of {\"seat\": 0 to 3, \"bot\": \"easy\" | \"medium\" | \"hard\" | ...}";
            return false;
        }
        spec.bots.clear();
        for (const JsonValue& item : v->items()) {
            const JsonValue* seat = item.is_object() ? item.find("seat") : nullptr;
            const JsonValue* bot = item.is_object() ? item.find("bot") : nullptr;
            if (seat == nullptr || !seat->is_int() || seat->as_int_or(-1) < 0 || seat->as_int_or(-1) > 3 || bot == nullptr || !bot->is_string()) {
                error = "every entry of \"bots\" must be {\"seat\": 0 to 3, \"bot\": \"easy\" | \"medium\" | \"hard\" | ...}";
                return false;
            }
            ai::BotSpec parsed;
            std::string why;
            if (!ai::parse_bot_spec(std::to_string(seat->as_int_or(0)) + ":" + bot->str(), parsed, why)) {
                error = "\"bots\": " + why;
                return false;
            }
            spec.bots.push_back(parsed);
        }
    }
    if (const JsonValue* v = body.find("code")) {
        if (!v->is_string()) {
            error = "\"code\" must be a string";
            return false;
        }
        spec.code = v->str();
    }
    out = std::move(spec);
    return true;
}

ctl::HttpResponse handle_control(RoomManager& rooms, const ctl::HttpRequest& request, uint32_t now_ms) {
    const std::string& path = request.path;
    if (path == "/rooms") {
        if (request.method == "GET") {
            JsonValue list = JsonValue::make_array();
            for (const RoomStatus& s : rooms.list(now_ms)) list.push_back(status_to_json(s));
            JsonValue o = JsonValue::make_object();
            o.set("rooms", std::move(list));
            return json_response(200, o);
        }
        if (request.method == "POST") {
            JsonValue body;
            std::string why;
            if (request.body.empty() || !ctl::parse_json(request.body, body, &why)) return error_response(400, request.body.empty() ? "a JSON body is required" : "bad JSON: " + why);
            RoomSpec spec = rooms.default_spec();           // what the server was started with (--reconnect, --hold-vote-seconds, --max-pause-seconds, --log-mb); the body overrides it
            if (!spec_from_json(body, spec, why)) return error_response(400, why);
            const CreateResult made = rooms.create_room(std::move(spec), now_ms);
            if (!made.ok) return error_response(made.http_status, made.error);
            RoomStatus s;
            rooms.status(made.code, s, now_ms);
            return json_response(201, status_to_json(s));
        }
        return error_response(405, "method not allowed");
    }
    if (path == "/stats") {
        if (request.method != "GET") return error_response(405, "method not allowed");
        JsonValue o = JsonValue::make_object();
        o.set("rooms", JsonValue::make_int(static_cast<int64_t>(rooms.room_count())));
        o.set("pending", JsonValue::make_int(static_cast<int64_t>(rooms.pending_count())));
        o.set("created", JsonValue::make_int(static_cast<int64_t>(rooms.rooms_created())));
        o.set("refused", JsonValue::make_int(static_cast<int64_t>(rooms.connections_refused())));
        o.set("log_bytes", JsonValue::make_int(static_cast<int64_t>(rooms.log_bytes())));                 // the turn logs of all the rooms (reconnect), and what they may take together
        o.set("log_budget_bytes", JsonValue::make_int(static_cast<int64_t>(rooms.log_budget_bytes())));
        return json_response(200, o);
    }
    static const std::string kPrefix = "/rooms/";
    if (path.compare(0, kPrefix.size(), kPrefix) == 0) {
        const std::string code = path.substr(kPrefix.size());
        if (!net::valid_room_code(code) || code.empty()) return error_response(404, "no such room");     // (a code that cannot exist, a trailing slash, an encoded path)
        RoomStatus s;
        if (request.method == "GET") {
            if (!rooms.status(code, s, now_ms)) return error_response(404, "no such room");
            return json_response(200, status_to_json(s));
        }
        if (request.method == "DELETE") {
            if (!rooms.close_room(code, now_ms)) return error_response(404, "no such room");
            rooms.status(code, s, now_ms);
            return json_response(200, status_to_json(s));
        }
        return error_response(405, "method not allowed");
    }
    return error_response(404, "not found");
}

}  // namespace ants::server
