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
        players.push_back(std::move(p));
    }
    o.set("players", std::move(players));
    o.set("ticks", JsonValue::make_int(s.ticks));
    o.set("turns", JsonValue::make_int(s.turns));
    o.set("age_seconds", JsonValue::make_int(s.age_ms / 1000));
    o.set("reason", JsonValue::make_string(s.reason));
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
    RoomSpec spec;
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
            RoomSpec spec;
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
