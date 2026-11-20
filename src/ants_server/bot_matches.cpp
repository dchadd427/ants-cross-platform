#include "ants_server/bot_matches.hpp"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <set>

#include "ants_net/clock.hpp"
#include "ants_server/upload_common.hpp"

namespace ants::server {

namespace {

using ctl::HttpRequest;
using ctl::HttpResponse;
using ctl::JsonValue;

constexpr size_t kNoticesMax = 100;
constexpr size_t kDetailMax = 200;

JsonValue int_value(int64_t v) { return JsonValue::make_int(v); }
JsonValue text_value(const std::string& v) { return JsonValue::make_string(v); }

JsonValue untrusted_fields(std::initializer_list<const char*> names) {
    JsonValue fields = JsonValue::make_array();
    for (const char* n : names) fields.push_back(JsonValue::make_string(n));
    return fields;
}

// The map's name as the page shows it: the file name without ".lvl"
std::string display_name(const std::string& file) {
    if (file.size() > 4) {
        const std::string tail = file.substr(file.size() - 4);
        if (tail[0] == '.' && (tail[1] == 'l' || tail[1] == 'L') && (tail[2] == 'v' || tail[2] == 'V') && (tail[3] == 'l' || tail[3] == 'L')) return file.substr(0, file.size() - 4);
    }
    return file;
}

HttpResponse failure(int status, const char* code, const char* message, JsonValue extra = JsonValue()) {
    Failure f = make_failure(status, code, message);
    f.extra = std::move(extra);
    return failure_reply(f);
}

// The sentence of a refusal of a batch or of a schedule that the page shows as it stands (the page has its own words for the ones that it can name)
HttpResponse bad_what(const std::string& code) {
    const char* message = "That did not work.";
    if (code == "bad_count") message = "Choose 1 to 8 matches.";
    else if (code == "bad_map") message = "That map is not one of the choices.";
    else if (code == "bad_rules") message = "Choose the rules: Highest score, 187 or random.";
    else if (code == "bad_bots") message = "Choose 2, 3, 4 or a random number of bots.";
    else if (code == "bad_levels") message = "A bot's level is Easy, Medium, Hard, or a draw.";
    else if (code == "bad_teams") message = "Choose free for all, a split or random teams.";
    else if (code == "bad_split") message = "That split needs three or four bots.";
    else if (code == "bad_spacing") message = "Start one every: 0 to 120 minutes, in steps of half a minute.";
    else if (code == "bad_maps") message = "Send the list as an array of map file names.";
    else if (code == "bad_name") message = "Give the schedule a name of 1 to 40 characters.";
    else if (code == "bad_every") message = "Every N minutes: a whole number from 5 to 1440.";
    else if (code == "no_times") message = "Add at least one time of the day.";
    else if (code == "bad_times") message = "A time looks like 18:30, and at most 12 are allowed.";
    else if (code == "bad_when") message = "Choose when the schedule runs.";
    else if (code == "bad_on") message = "Say whether the schedule is on.";
    return failure(400, code.c_str(), message);
}

std::string when_words(const BotSchedule& s) {
    if (s.kind == BotSchedule::Kind::Every) return "every " + std::to_string(s.minutes) + " minutes";
    std::string text = "at ";
    for (size_t i = 0; i < s.times.size(); ++i) text += (i ? ", " : "") + clock_time_text(s.times[i]);
    return text + " (Pacific)";
}

// Why a batch stopped short, in the word that the page has a sentence for
const char* stopped_word(BotRefusal refusal) {
    switch (refusal) {
        case BotRefusal::Places: return "places";
        case BotRefusal::PeopleBusy: return "busy";
        case BotRefusal::Cap: return "cap";
        case BotRefusal::Budget: return "budget";
        default: break;
    }
    return "other";
}

std::string cut(std::string text, size_t n) {
    if (text.size() > n) {
        text.resize(n);
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();       // (not in the middle of a character)
        if (!text.empty() && (static_cast<unsigned char>(text.back()) & 0x80) != 0) text.pop_back();
    }
    return text;
}

}  // namespace

BotMatches::BotMatches(RoomManager& rooms, BotScheduleStore& schedules, BotRotationStore& rotation, AdminAudit* audit) : rooms_(rooms), schedules_(schedules), rotation_(rotation), audit_(audit) {
    for (const bool kills_187 : {false, true}) rooms_.set_bot_rotation(kills_187, rotation_.list(kills_187));          // (what an admin saved before the last start, or the defaults)
}

void BotMatches::note(std::string line) {
    if (notices_.size() < kNoticesMax) notices_.push_back(std::move(line));
}

std::vector<std::string> BotMatches::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    for (std::string& line : schedules_.take_notices()) out.push_back(std::move(line));
    for (std::string& line : rotation_.take_notices()) out.push_back(std::move(line));
    return out;
}

void BotMatches::record(const AdminIdentity& who, int64_t now_s, const char* action, const std::string& target, const std::string& detail) {
    if (audit_ != nullptr) audit_->record(Actor{who.ref, who.name}, now_s, action, target, cut(detail, kDetailMax));
}

// The matches that were started here and no longer go are forgotten
void BotMatches::forget_ended(const std::vector<BotMatchInfo>& going) {
    started_.erase(std::remove_if(started_.begin(), started_.end(), [&](const Started& s) {
        return std::none_of(going.begin(), going.end(), [&](const BotMatchInfo& m) { return m.code == s.code; });
    }), started_.end());
}

HttpResponse BotMatches::handle(const HttpRequest& request, const AdminIdentity& who, const std::string& tail, const JsonValue& body, int64_t now_s, uint32_t now_ms) {
    const bool get = request.method == "GET";
    if (get) {
        if (tail == "bot-matches") return list_matches(now_s, now_ms);
        if (tail == "bot-options") return list_options();
        if (tail == "bot-schedules") return list_schedules();
        if (tail == "bot-rotation") return list_rotation();
        return not_found();
    }
    if (tail == "bot-rotation") return rotation_save(who, body, now_s);
    if (tail == "bot-rotation/reset") return rotation_reset(who, body, now_s);
    if (tail == "bot-matches") return start_now(who, body, now_s, now_ms);
    if (tail == "bot-matches/stop-all") return stop_all(who, now_s, now_ms);
    if (tail.compare(0, 12, "bot-matches/") == 0 && tail.size() > 17 && tail.compare(tail.size() - 5, 5, "/stop") == 0) return stop_one(who, tail.substr(12, tail.size() - 17), now_s, now_ms);
    if (tail == "bot-schedules") return schedule_create(who, body, now_s);
    if (tail.compare(0, 14, "bot-schedules/") == 0) {
        std::string rest = tail.substr(14);
        const bool deleting = rest.size() > 7 && rest.compare(rest.size() - 7, 7, "/delete") == 0;
        if (deleting) rest.resize(rest.size() - 7);
        if (rest.empty() || rest.size() > 12 || rest.find_first_not_of("0123456789") != std::string::npos) return not_found();
        const int64_t id = std::stoll(rest);
        return deleting ? schedule_delete(who, id, now_s) : schedule_update(who, id, body, now_s);
    }
    return not_found();
}

// ------------------------------------------------------------------------------------------------ what goes now

namespace {

// The choice of the map in words that a row can carry: the file's name without ".lvl", or the word of a random choice as it is
std::string map_target(const std::string& map) { return map == kBotMapRotation ? "the rotation" : map == kBotMapOriginal ? "the original maps" : map == kBotMapPlayers ? "the players' maps" : display_name(map); }

}  // namespace

HttpResponse BotMatches::list_matches(int64_t now_s, uint32_t now_ms) {
    const std::vector<BotMatchInfo> going = rooms_.bot_matches(now_ms);
    forget_ended(going);
    JsonValue list = JsonValue::make_array();
    for (const BotMatchInfo& m : going) {
        Started* origin = nullptr;
        for (Started& s : started_) {
            if (s.code == m.code) origin = &s;
        }
        if (origin == nullptr) {                                                    // (a match that something else started: it is listed, with no maker)
            started_.push_back(Started{"m" + std::to_string(next_id_++), m.code, 0, "", ""});
            origin = &started_.back();
        }
        JsonValue row = JsonValue::make_object();
        row.set("id", text_value(origin->id));
        row.set("name", text_value(display_name(m.map)));
        row.set("rules", text_value(m.mode == static_cast<uint8_t>(sim::GameMode::Kills187) ? "187" : "highest-score"));
        JsonValue seats = JsonValue::make_array();
        for (const auto& seat : m.seats) {
            JsonValue one = JsonValue::make_object();
            one.set("seat", int_value(seat.first));
            one.set("level", text_value(seat.second));
            seats.push_back(std::move(one));
        }
        row.set("seats", std::move(seats));
        row.set("teams", text_value(m.sides));
        row.set("started", int_value(origin->started_s));
        row.set("live", m.live_id.empty() ? JsonValue::make_null() : text_value(m.live_id));
        if (!origin->schedule.empty()) row.set("schedule", text_value(origin->schedule));
        else row.set("by", text_value(origin->by.empty() ? "the server" : origin->by));
        list.push_back(std::move(row));
    }
    JsonValue waiting = JsonValue::make_array();                                    // (the starts that wait for their time: the oldest batch first)
    for (const Waiting& w : waiting_) {
        const int32_t left = static_cast<int32_t>(w.due_ms - now_ms);
        JsonValue row = JsonValue::make_object();
        row.set("id", text_value(w.id));
        row.set("name", text_value(map_target(w.what.spec.map)));
        row.set("what", bot_what_json(w.what));
        row.set("n", int_value(w.number));
        row.set("of", int_value(w.of));
        row.set("at", int_value(now_s + (left > 0 ? (static_cast<int64_t>(left) + 999) / 1000 : 0)));
        row.set("held", JsonValue::make_bool(w.held && left <= 0));
        if (!w.schedule.empty()) row.set("schedule", text_value(w.schedule));
        else row.set("by", text_value(w.by));
        waiting.push_back(std::move(row));
    }
    JsonValue places = JsonValue::make_object();
    places.set("people", int_value(static_cast<int64_t>(rooms_.people_places())));
    places.set("total", int_value(static_cast<int64_t>(rooms_.place_total())));
    places.set("bots", int_value(rooms_.bot_places()));
    places.set("bots_max", int_value(kBotPlacesMax));
    JsonValue o = JsonValue::make_object();
    o.set("matches", std::move(list));
    o.set("waiting", std::move(waiting));
    o.set("waiting_max", int_value(static_cast<int64_t>(kBotWaitingMax)));
    o.set("cap", int_value(kBotMatchesMax));
    o.set("places", std::move(places));
    o.set("replays", JsonValue::make_bool(rooms_.replay_store() != nullptr));
    o.set("untrusted", JsonValue::make_bool(true));
    o.set("untrusted_fields", untrusted_fields({"name", "by", "schedule"}));
    return json_reply(200, o);
}

namespace {

JsonValue option_json(const BotMapOption& m) {
    JsonValue row = JsonValue::make_object();
    row.set("file", text_value(m.file));
    row.set("name", text_value(display_name(m.file)));
    row.set("group", text_value(m.group));
    row.set("width", int_value(m.width));
    row.set("height", int_value(m.height));
    row.set("players", int_value(m.players));
    row.set("places", int_value(m.places));
    row.set("minutes", int_value(m.minutes));
    row.set("random", JsonValue::make_bool(m.random));
    row.set("playable", JsonValue::make_bool(m.playable));
    if (!m.reason.empty()) row.set("reason", text_value(m.reason));
    if (!m.checked) row.set("checked", JsonValue::make_bool(false));               // (its file was not judged yet: the page asks again in a moment)
    return row;
}

}  // namespace

HttpResponse BotMatches::list_options() {
    JsonValue list = JsonValue::make_array();
    size_t pending = 0;
    const std::vector<BotMapOption> options = rooms_.bot_map_options(kBotCatalogLoads, &pending);
    for (const BotMapOption& m : options) list.push_back(option_json(m));
    JsonValue o = JsonValue::make_object();
    o.set("maps", std::move(list));
    o.set("checking", int_value(static_cast<int64_t>(pending)));
    o.set("random_max_minutes", int_value(kBotRandomMaxMinutes));
    o.set("untrusted", JsonValue::make_bool(true));
    o.set("untrusted_fields", untrusted_fields({"name"}));
    return json_reply(200, o);
}

// The matches of one batch, one after another, until one is refused: how many were made, and why the next was not. A batch with a spacing starts the first now and keeps the others as waiting starts (as many as fit the
// ones that wait: kBotWaitingMax), each at its own time; what a match is dealt is drawn when it starts, not now.
BotMatches::Batch BotMatches::run_batch(const BotWhat& what, const std::string& by, const std::string& schedule, int64_t now_s, uint32_t now_ms) {
    Batch batch;
    const bool spaced = what.every_s != 0 && what.count > 1;
    const uint32_t now_wanted = spaced ? 1 : what.count;
    for (uint32_t i = 0; i < now_wanted; ++i) {
        std::string code, why;
        BotRefusal refusal = BotRefusal::None;
        if (!rooms_.start_bot_match(now_ms, what.spec, code, why, &refusal)) {
            batch.refusal = refusal;
            batch.why = why;
            break;
        }
        started_.push_back(Started{"m" + std::to_string(next_id_++), code, now_s, by, schedule});
        ++batch.started;
    }
    if (!spaced || batch.started == 0) return batch;
    const uint64_t number = next_batch_++;
    for (uint32_t i = 1; i < what.count; ++i) {
        if (waiting_.size() >= kBotWaitingMax) {
            batch.queue_full = true;
            break;
        }
        Waiting w;
        w.id = "w" + std::to_string(next_id_++);
        w.what = what;
        w.what.count = 1;
        w.what.every_s = 0;
        w.by = by;
        w.schedule = schedule;
        w.every_ms = what.every_s * 1000u;
        w.due_ms = now_ms + i * w.every_ms;
        w.batch = number;
        w.number = i + 1;
        w.of = what.count;
        waiting_.push_back(std::move(w));
        ++batch.queued;
    }
    std::stable_sort(waiting_.begin(), waiting_.end(), [&](const Waiting& a, const Waiting& b) { return static_cast<int32_t>(a.due_ms - now_ms) < static_cast<int32_t>(b.due_ms - now_ms); });
    return batch;
}

HttpResponse BotMatches::start_now(const AdminIdentity& who, const JsonValue& body, int64_t now_s, uint32_t now_ms) {
    BotWhat what;
    std::string error;
    if (!parse_bot_what(body, what, error)) return bad_what(error);
    if (rooms_.replay_store() == nullptr) return failure(503, "no_replays", "The server keeps no replays, and a match that nobody could look at again is not played.");
    const bool spaced = what.every_s != 0 && what.count > 1;
    const uint32_t at_once = spaced ? 1 : what.count;                               // (a batch with a spacing needs room for its first match only: the others wait for their time)
    const size_t running = rooms_.bot_match_count();
    if (at_once > kBotMatchesMax - std::min(running, kBotMatchesMax)) {
        JsonValue extra = JsonValue::make_object();
        extra.set("running", int_value(static_cast<int64_t>(running)));
        extra.set("asked", int_value(at_once));
        extra.set("free", int_value(static_cast<int64_t>(kBotMatchesMax - std::min(running, kBotMatchesMax))));
        return failure(409, "cap", "That would be over the limit of bot matches.", std::move(extra));
    }
    if (spaced && waiting_.size() + (what.count - 1) > kBotWaitingMax) {
        JsonValue extra = JsonValue::make_object();
        extra.set("waiting", int_value(static_cast<int64_t>(waiting_.size())));
        extra.set("asked", int_value(what.count - 1));
        extra.set("max", int_value(static_cast<int64_t>(kBotWaitingMax)));
        return failure(409, "queue_full", "That would be more starts waiting for their time than the server holds.", std::move(extra));
    }
    if (rooms_.people_busy()) {
        JsonValue extra = JsonValue::make_object();
        extra.set("people", int_value(static_cast<int64_t>(rooms_.people_places())));
        extra.set("total", int_value(static_cast<int64_t>(rooms_.place_total())));
        return failure(409, "busy", "People's matches use most of the server's places.", std::move(extra));
    }
    const Batch batch = run_batch(what, who.name, std::string(), now_s, now_ms);
    if (batch.started == 0) {
        switch (batch.refusal) {
            case BotRefusal::Islands187: return failure(409, "map_187", "ISLANDS is not played in 187.");
            case BotRefusal::Places: {
                JsonValue extra = JsonValue::make_object();
                extra.set("used", int_value(rooms_.bot_places()));
                extra.set("max", int_value(kBotPlacesMax));
                return failure(409, "places", "These matches would take more places than bot matches may have.", std::move(extra));
            }
            case BotRefusal::NoMap: return failure(409, "no_map", "That map cannot be played now.");
            case BotRefusal::NoReplays: return failure(503, "no_replays", "The server keeps no replays, and a match that nobody could look at again is not played.");
            case BotRefusal::Cap: return failure(409, "cap", "That would be over the limit of bot matches.");
            case BotRefusal::PeopleBusy: return failure(409, "busy", "People's matches use most of the server's places.");
            case BotRefusal::Budget: {
                JsonValue extra = JsonValue::make_object();
                extra.set("per_hour", int_value(rooms_.replay_store() != nullptr ? rooms_.replay_store()->config().max_bot_saves_per_hour : 0));
                return failure(409, "budget", "The store keeps only so many recordings of bot matches an hour, and that many are kept or on their way.", std::move(extra));
            }
            default: return failure(503, "failed", "The server could not make the matches.");
        }
    }
    const size_t total = batch.started + batch.queued;
    record(who, now_s, "bot_started", std::to_string(total) + (total == 1 ? " match" : " matches"), bot_what_words(what));
    JsonValue o = JsonValue::make_object();
    o.set("started", int_value(static_cast<int64_t>(batch.started)));
    o.set("asked", int_value(what.count));
    if (batch.queued != 0) {
        o.set("queued", int_value(static_cast<int64_t>(batch.queued)));
        o.set("every_s", int_value(what.every_s));
    }
    if (batch.started + batch.queued < what.count) o.set("stopped_for", text_value(batch.queue_full ? "queue" : stopped_word(batch.refusal)));
    return json_reply(200, o);
}

HttpResponse BotMatches::stop_one(const AdminIdentity& who, const std::string& id, int64_t now_s, uint32_t now_ms) {
    for (size_t i = 0; i < waiting_.size(); ++i) {                                   // (a start that waits for its time: it is cancelled, and the others of its batch go on)
        if (waiting_[i].id != id) continue;
        const std::string target = map_target(waiting_[i].what.spec.map);
        waiting_.erase(waiting_.begin() + static_cast<std::ptrdiff_t>(i));
        record(who, now_s, "bot_waiting_stopped", target, std::string());
        JsonValue o = JsonValue::make_object();
        o.set("stopped", int_value(1));
        return json_reply(200, o);
    }
    for (const Started& s : started_) {
        if (s.id != id) continue;
        std::string name;
        for (const BotMatchInfo& m : rooms_.bot_matches(now_ms)) {
            if (m.code == s.code) name = display_name(m.map);
        }
        if (!rooms_.stop_bot_match(s.code, now_ms)) break;
        record(who, now_s, "bot_stopped", name, std::string());
        JsonValue o = JsonValue::make_object();
        o.set("stopped", int_value(1));
        return json_reply(200, o);
    }
    return failure(404, "gone", "That match is not running any more.");
}

HttpResponse BotMatches::stop_all(const AdminIdentity& who, int64_t now_s, uint32_t now_ms) {
    const size_t n = rooms_.stop_bot_matches(now_ms);
    const size_t dropped = waiting_.size();                                           // (the batch ends: every start that waits for its time is cancelled with the matches that go)
    waiting_.clear();
    if (n > 0 || dropped > 0) record(who, now_s, "bot_stopped_all", std::to_string(n) + (n == 1 ? " match" : " matches"), dropped > 0 ? "and " + std::to_string(dropped) + (dropped == 1 ? " waiting start" : " waiting starts") : std::string());         // (stopping nothing is not logged)
    JsonValue o = JsonValue::make_object();
    o.set("stopped", int_value(static_cast<int64_t>(n)));
    o.set("dropped", int_value(static_cast<int64_t>(dropped)));
    return json_reply(200, o);
}

// ------------------------------------------------------------------------------------------------ the rotation

namespace {

const char* rules_name(bool kills_187) { return kills_187 ? "187" : "Highest score"; }

// What the page shows of a map of a list: the catalog's row of the file, and whether the list can draw it now (`state`: ok, skipped with `reason`, or checking while its file has not been judged)
JsonValue rotation_entry_json(const BotRotationEntry& e, const BotMapOption* row) {
    JsonValue o = row != nullptr ? option_json(*row) : JsonValue::make_object();
    if (row == nullptr) {
        o.set("file", text_value(e.file));
        o.set("name", text_value(display_name(e.file)));
        o.set("group", text_value(e.approved ? "players" : "library"));
    }
    std::string reason;
    const char* state = "ok";
    if (row == nullptr) {
        state = "skipped";
        reason = e.approved ? "taken down" : "not on this server";
    } else if (!row->checked) {
        state = "checking";
    } else if (!row->playable) {
        state = "skipped";
        reason = row->reason;
    }
    o.set("state", text_value(state));
    if (!reason.empty()) o.set("reason", text_value(reason));
    return o;
}

}  // namespace

HttpResponse BotMatches::list_rotation() {
    size_t pending = 0;
    const std::vector<BotMapOption> catalog = rooms_.bot_map_options(kBotCatalogLoads, &pending);
    std::map<std::string, const BotMapOption*> by_file;
    for (const BotMapOption& m : catalog) by_file[m.file] = &m;
    JsonValue lists = JsonValue::make_object(), defaults = JsonValue::make_object();
    for (const bool kills_187 : {false, true}) {
        JsonValue one = JsonValue::make_array();
        for (const BotRotationEntry& e : rooms_.bot_rotation(kills_187)) {
            const auto found = by_file.find(e.file);
            one.push_back(rotation_entry_json(e, found == by_file.end() ? nullptr : found->second));
        }
        lists.set(bot_rotation_word(kills_187), std::move(one));
        JsonValue base = JsonValue::make_array();
        for (const BotRotationEntry& e : bot_rotation_defaults(kills_187)) {
            JsonValue row = JsonValue::make_object();
            row.set("file", text_value(e.file));
            row.set("name", text_value(display_name(e.file)));
            base.push_back(std::move(row));
        }
        defaults.set(bot_rotation_word(kills_187), std::move(base));
    }
    JsonValue o = JsonValue::make_object();
    o.set("lists", std::move(lists));
    o.set("defaults", std::move(defaults));
    o.set("max", int_value(static_cast<int64_t>(kBotRotationMax)));
    o.set("checking", int_value(static_cast<int64_t>(pending)));
    o.set("untrusted", JsonValue::make_bool(true));
    o.set("untrusted_fields", untrusted_fields({"name"}));
    return json_reply(200, o);
}

// A list is replaced whole: the file names, in the order of the draws. Every name must be one that the catalog has (the server's own list of its maps: a name is never taken as a path), and a map that the list does not have
// yet must be one that a room can play now; a map that the list has already stays, though it cannot be played now (it is skipped, as before). A list has at least one map and at most kBotRotationMax.
HttpResponse BotMatches::rotation_save(const AdminIdentity& who, const JsonValue& body, int64_t now_s) {
    bool kills_187 = false;
    if (!body.get("rules").is_string() || !bot_rotation_rules_of(body.get("rules").str(), kills_187)) return bad_what("bad_rules");
    const JsonValue& maps = body.get("maps");
    if (!maps.is_array()) return bad_what("bad_maps");
    if (maps.size() == 0) return failure(400, "empty", "A list needs at least one map: a list with no map would never be drawn.");
    if (maps.size() > kBotRotationMax) {
        JsonValue extra = JsonValue::make_object();
        extra.set("max", int_value(static_cast<int64_t>(kBotRotationMax)));
        return failure(400, "too_many", "A list may hold 100 maps at the most.", std::move(extra));
    }
    const std::vector<BotRotationEntry> current = rooms_.bot_rotation(kills_187);
    std::vector<BotRotationEntry> next;
    std::set<std::string> seen;
    for (size_t i = 0; i < maps.size(); ++i) {
        const JsonValue& one = maps.at(i);
        if (!one.is_string() || !bot_map_name_shape_ok(one.str())) return failure(400, "bad_map", "That map is not one of the server's maps.");
        const std::string& file = one.str();
        if (!seen.insert(file).second) return failure(400, "bad_map", "A map is in the list twice.");
        const auto have = std::find_if(current.begin(), current.end(), [&](const BotRotationEntry& e) { return e.file == file; });
        bool approved = false;
        if (!rooms_.bot_catalog_has(file, approved)) {
            if (have == current.end()) return failure(400, "bad_map", "That map is not one of the server's maps.");
            approved = have->approved;                                               // (a map of the list that the server lost: it stays in the list, skipped, until it is taken out)
        } else if (have == current.end()) {
            uint32_t minutes = 0;
            const std::string reason = rooms_.bot_map_refusal(file, approved, minutes);
            if (!reason.empty()) {
                JsonValue extra = JsonValue::make_object();
                extra.set("file", text_value(file));
                extra.set("why", text_value(reason));
                return failure(409, "no_map", "That map cannot be played now.", std::move(extra));
            }
        }
        next.push_back({file, approved});
    }
    JsonValue o = JsonValue::make_object();
    o.set("count", int_value(static_cast<int64_t>(next.size())));
    if (bot_rotation_same(next, current)) {                                          // (a save that changes nothing writes nothing and is not logged)
        o.set("changed", JsonValue::make_bool(false));
        return json_reply(200, o);
    }
    std::string error;
    if (!rotation_.set(kills_187, next, now_s, error)) return failure(503, "db", "The database did not take the list.");
    rooms_.set_bot_rotation(kills_187, next);
    size_t added = 0, removed = 0;
    for (const BotRotationEntry& e : next) added += std::none_of(current.begin(), current.end(), [&](const BotRotationEntry& c) { return c.file == e.file; }) ? size_t{1} : size_t{0};
    for (const BotRotationEntry& c : current) removed += std::none_of(next.begin(), next.end(), [&](const BotRotationEntry& e) { return c.file == e.file; }) ? size_t{1} : size_t{0};
    record(who, now_s, "bot_rotation_changed", rules_name(kills_187), "+" + std::to_string(added) + " -" + std::to_string(removed) + ", " + std::to_string(next.size()) + (next.size() == 1 ? " map" : " maps"));
    o.set("changed", JsonValue::make_bool(true));
    return json_reply(200, o);
}

HttpResponse BotMatches::rotation_reset(const AdminIdentity& who, const JsonValue& body, int64_t now_s) {
    bool kills_187 = false;
    if (!body.get("rules").is_string() || !bot_rotation_rules_of(body.get("rules").str(), kills_187)) return bad_what("bad_rules");
    const std::vector<BotRotationEntry>& base = bot_rotation_defaults(kills_187);
    JsonValue o = JsonValue::make_object();
    o.set("count", int_value(static_cast<int64_t>(base.size())));
    if (!rotation_.edited(kills_187) && bot_rotation_same(rooms_.bot_rotation(kills_187), base)) {      // (already the defaults: nothing is written or logged)
        o.set("changed", JsonValue::make_bool(false));
        return json_reply(200, o);
    }
    std::string error;
    if (!rotation_.reset(kills_187, error)) return failure(503, "db", "The database did not take the change.");
    rooms_.set_bot_rotation(kills_187, base);
    record(who, now_s, "bot_rotation_reset", rules_name(kills_187), std::to_string(base.size()) + " maps");
    o.set("changed", JsonValue::make_bool(true));
    return json_reply(200, o);
}

// ------------------------------------------------------------------------------------------------ the schedules

namespace {

JsonValue schedule_json(const BotSchedule& s, bool scheduling) {
    JsonValue o = JsonValue::make_object();
    o.set("id", int_value(s.id));
    o.set("name", text_value(s.name));
    o.set("default", JsonValue::make_bool(s.is_default));
    o.set("on", JsonValue::make_bool(s.on));
    JsonValue when = JsonValue::make_object();
    if (s.kind == BotSchedule::Kind::Every) {
        when.set("kind", text_value("every"));
        when.set("minutes", int_value(s.minutes));
    } else {
        when.set("kind", text_value("times"));
        JsonValue times = JsonValue::make_array();
        for (const int t : s.times) times.push_back(text_value(clock_time_text(t)));
        when.set("times", std::move(times));
    }
    o.set("when", std::move(when));
    o.set("what", bot_what_json(s.what));
    o.set("last", int_value(s.last_run));
    o.set("next", scheduling && s.on && s.next_run != 0 ? int_value(s.next_run) : JsonValue::make_null());       // (a server whose schedules are switched off starts nothing)
    return o;
}

}  // namespace

HttpResponse BotMatches::list_schedules() {
    JsonValue list = JsonValue::make_array();
    for (const BotSchedule& s : schedules_.list()) list.push_back(schedule_json(s, scheduling_));
    JsonValue o = JsonValue::make_object();
    o.set("schedules", std::move(list));
    o.set("max", int_value(static_cast<int64_t>(kMaxBotSchedules)));
    o.set("scheduling", JsonValue::make_bool(scheduling_));
    o.set("untrusted", JsonValue::make_bool(true));
    o.set("untrusted_fields", untrusted_fields({"name"}));
    return json_reply(200, o);
}

HttpResponse BotMatches::schedule_create(const AdminIdentity& who, const JsonValue& body, int64_t now_s) {
    BotSchedulePatch patch;
    std::string error;
    if (!parse_schedule_patch(body, true, patch, error)) return bad_what(error);
    BotSchedule made;
    bool repeated = false;
    if (!schedules_.create(patch, now_s, made, error, &repeated)) {
        if (error == "too_many") return failure(409, "too_many", "There are 20 schedules already. Delete one first.");
        return failure(503, "db", "The database did not take the schedule.");
    }
    if (!repeated) record(who, now_s, "bot_schedule_added", made.name, when_words(made) + ", " + bot_what_words(made.what));         // (the same save a second time is the first: one schedule, one line)
    JsonValue o = JsonValue::make_object();
    o.set("schedule", schedule_json(made, scheduling_));
    return json_reply(200, o);
}

HttpResponse BotMatches::schedule_update(const AdminIdentity& who, int64_t id, const JsonValue& body, int64_t now_s) {
    BotSchedulePatch patch;
    std::string error;
    if (!parse_schedule_patch(body, false, patch, error)) return bad_what(error);
    BotSchedule changed;
    bool different = false;
    if (!schedules_.update(id, patch, now_s, changed, error, &different)) {
        if (error == "gone") return failure(404, "gone", "That schedule is not there any more.");
        return failure(503, "db", "The database did not take the change.");
    }
    if (different) {                                                                 // (a save that changes nothing is not logged)
        const bool only_on = patch.has_on && !patch.has_name && !patch.has_when && !patch.has_what;
        if (only_on) record(who, now_s, patch.on ? "bot_schedule_on" : "bot_schedule_off", changed.name, std::string());
        else record(who, now_s, "bot_schedule_changed", changed.name, when_words(changed) + ", " + bot_what_words(changed.what));
    }
    JsonValue o = JsonValue::make_object();
    o.set("schedule", schedule_json(changed, scheduling_));
    return json_reply(200, o);
}

HttpResponse BotMatches::schedule_delete(const AdminIdentity& who, int64_t id, int64_t now_s) {
    const BotSchedule* row = schedules_.find(id);
    const std::string name = row != nullptr ? row->name : std::string();
    std::string error;
    if (!schedules_.remove(id, error)) {
        if (error == "gone") return failure(404, "gone", "That schedule is not there any more.");
        return failure(503, "db", "The database did not take the change.");
    }
    record(who, now_s, "bot_schedule_deleted", name, std::string());
    JsonValue o = JsonValue::make_object();
    o.set("ok", JsonValue::make_bool(true));
    return json_reply(200, o);
}

// ------------------------------------------------------------------------------------------------ the loop

// The starts that wait for their time: each one whose time has come starts a match (what it is dealt is drawn now). A limit that holds a start back (the eight matches, the places, people's share, the store's room) holds it
// until a place is free, and the loop asks again once a second; a start that cannot ever be made (the map is gone, the rules refuse it) is dropped with a line in the log. A start that was held and made late moves the starts of
// its batch that follow it, so that they keep the spacing from it.
void BotMatches::release_waiting(int64_t now_s, uint32_t now_ms) {
    if (waiting_.empty()) {
        release_held_ = false;
        return;
    }
    if (release_held_ && !net::time_reached(now_ms, next_release_ms_)) return;
    release_held_ = false;
    for (Waiting& w : waiting_) w.held = false;
    for (size_t i = 0; i < waiting_.size();) {
        Waiting& w = waiting_[i];
        if (!net::time_reached(now_ms, w.due_ms)) {
            ++i;
            continue;
        }
        std::string code, why;
        BotRefusal refusal = BotRefusal::None;
        if (rooms_.start_bot_match(now_ms, w.what.spec, code, why, &refusal)) {
            started_.push_back(Started{"m" + std::to_string(next_id_++), code, now_s, w.by, w.schedule});
            const uint64_t batch = w.batch;
            const uint32_t earliest = now_ms + w.every_ms;
            waiting_.erase(waiting_.begin() + static_cast<std::ptrdiff_t>(i));
            for (Waiting& later : waiting_) {
                if (later.batch == batch && !net::time_reached(later.due_ms, earliest)) later.due_ms = earliest;
            }
            continue;
        }
        if (refusal == BotRefusal::Cap || refusal == BotRefusal::Places || refusal == BotRefusal::PeopleBusy || refusal == BotRefusal::Budget) {          // (a limit: every other start is held by the same one)
            w.held = true;
            release_held_ = true;
            next_release_ms_ = now_ms + 1000u;
            return;
        }
        note("bot waiting start " + std::to_string(w.number) + " of " + std::to_string(w.of) + " was dropped (" + why + ")");
        waiting_.erase(waiting_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void BotMatches::update(int64_t now_s, uint32_t now_ms) {
    if (now_s < last_prune_s_ || now_s - last_prune_s_ >= 60) {
        last_prune_s_ = now_s;
        forget_ended(rooms_.bot_matches(now_ms));
        schedules_.repair_clock(now_s);                                              // (a time that cannot be true, from a clock that was wrong or a damaged row, is brought back once a minute)
    }
    const size_t before = started_.size();
    release_waiting(now_s, now_ms);                                                  // (what Start now and a schedule set in time: it goes on while the schedules are switched off)
    if (!scheduling_) {
        if (started_.size() == before) warm_maps(now_ms);
        return;                                                                      // (the operator switched the schedules off: they are kept and shown, and none of them starts a match)
    }
    std::vector<int64_t> due;
    for (const BotSchedule& s : schedules_.list()) {
        if (s.on && s.next_run != 0 && now_s >= s.next_run) due.push_back(s.id);
    }
    for (const int64_t id : due) {
        const BotSchedule* found = schedules_.find(id);
        if (found == nullptr) continue;
        const BotSchedule s = *found;
        const Batch batch = run_batch(s.what, std::string(), s.name, now_s, now_ms);
        std::string line = "bot schedule " + std::to_string(s.id) + ": started " + std::to_string(batch.started) + " of " + std::to_string(s.what.count);
        if (batch.queued > 0) line += ", " + std::to_string(batch.queued) + " waiting for their time";
        if (batch.started + batch.queued < s.what.count) line += " (" + (batch.queue_full ? std::string("more starts than the server holds wait already") : batch.why) + ")";
        note(line);
        schedules_.note_run(s.id, batch.started > 0 ? now_s : s.last_run, bot_schedule_next_after_run(s, now_s));
    }
    if (started_.size() == before) warm_maps(now_ms);
}

// The maps of the catalog that have no verdict yet are judged a few at a time while nothing else is started, so that the first call of the admin page finds them kept. Once a pass finds nothing to do, it looks again in ten
// seconds (a map that was approved since has none).
void BotMatches::warm_maps(uint32_t now_ms) {
    if (warm_waits_ && !net::time_reached(now_ms, next_warm_ms_)) return;
    warm_waits_ = rooms_.warm_bot_maps(kBotWarmLoadsPerPass) == 0;
    if (warm_waits_) next_warm_ms_ = now_ms + 10u * 1000u;
}

}  // namespace ants::server
