#include "ants_server/bot_matches.hpp"

#include <algorithm>
#include <initializer_list>

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

BotMatches::BotMatches(RoomManager& rooms, BotScheduleStore& schedules, AdminAudit* audit) : rooms_(rooms), schedules_(schedules), audit_(audit) {}

void BotMatches::note(std::string line) {
    if (notices_.size() < kNoticesMax) notices_.push_back(std::move(line));
}

std::vector<std::string> BotMatches::take_notices() {
    std::vector<std::string> out;
    out.swap(notices_);
    for (std::string& line : schedules_.take_notices()) out.push_back(std::move(line));
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
        return not_found();
    }
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

HttpResponse BotMatches::list_matches(int64_t now_s, uint32_t now_ms) {
    (void)now_s;
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
    JsonValue places = JsonValue::make_object();
    places.set("people", int_value(static_cast<int64_t>(rooms_.people_places())));
    places.set("total", int_value(static_cast<int64_t>(rooms_.place_total())));
    places.set("bots", int_value(rooms_.bot_places()));
    places.set("bots_max", int_value(kBotPlacesMax));
    JsonValue o = JsonValue::make_object();
    o.set("matches", std::move(list));
    o.set("cap", int_value(kBotMatchesMax));
    o.set("places", std::move(places));
    o.set("replays", JsonValue::make_bool(rooms_.replay_store() != nullptr));
    o.set("untrusted", JsonValue::make_bool(true));
    o.set("untrusted_fields", untrusted_fields({"name", "by", "schedule"}));
    return json_reply(200, o);
}

HttpResponse BotMatches::list_options() {
    JsonValue list = JsonValue::make_array();
    const std::vector<BotMapOption> options = rooms_.bot_map_options();
    for (const BotMapOption& m : options) {
        JsonValue row = JsonValue::make_object();
        row.set("file", text_value(m.file));
        row.set("name", text_value(display_name(m.file)));
        row.set("group", text_value(m.group));
        row.set("width", int_value(m.width));
        row.set("height", int_value(m.height));
        row.set("places", int_value(m.places));
        row.set("minutes", int_value(m.minutes));
        row.set("random", JsonValue::make_bool(m.random));
        list.push_back(std::move(row));
    }
    JsonValue rotation = JsonValue::make_object();
    rotation.set("highest-score", int_value(static_cast<int64_t>(rooms_.bot_rotation_maps(false))));
    rotation.set("187", int_value(static_cast<int64_t>(rooms_.bot_rotation_maps(true))));
    JsonValue o = JsonValue::make_object();
    o.set("maps", std::move(list));
    o.set("rotation", std::move(rotation));
    o.set("random_max_minutes", int_value(kBotRandomMaxMinutes));
    o.set("untrusted", JsonValue::make_bool(true));
    o.set("untrusted_fields", untrusted_fields({"name"}));
    return json_reply(200, o);
}

// The matches of one batch, one after another, until one is refused: how many were made, and why the next was not
BotMatches::Batch BotMatches::run_batch(const BotWhat& what, const std::string& by, const std::string& schedule, int64_t now_s, uint32_t now_ms) {
    Batch batch;
    for (uint32_t i = 0; i < what.count; ++i) {
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
    return batch;
}

HttpResponse BotMatches::start_now(const AdminIdentity& who, const JsonValue& body, int64_t now_s, uint32_t now_ms) {
    BotWhat what;
    std::string error;
    if (!parse_bot_what(body, what, error)) return bad_what(error);
    if (rooms_.replay_store() == nullptr) return failure(503, "no_replays", "The server keeps no replays, and a match that nobody could look at again is not played.");
    const size_t running = rooms_.bot_match_count();
    if (what.count > kBotMatchesMax - std::min(running, kBotMatchesMax)) {
        JsonValue extra = JsonValue::make_object();
        extra.set("running", int_value(static_cast<int64_t>(running)));
        extra.set("asked", int_value(what.count));
        extra.set("free", int_value(static_cast<int64_t>(kBotMatchesMax - std::min(running, kBotMatchesMax))));
        return failure(409, "cap", "That would be over the limit of bot matches.", std::move(extra));
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
    record(who, now_s, "bot_started", std::to_string(batch.started) + (batch.started == 1 ? " match" : " matches"), bot_what_words(what));
    JsonValue o = JsonValue::make_object();
    o.set("started", int_value(static_cast<int64_t>(batch.started)));
    o.set("asked", int_value(what.count));
    if (batch.started < what.count) o.set("stopped_for", text_value(stopped_word(batch.refusal)));
    return json_reply(200, o);
}

HttpResponse BotMatches::stop_one(const AdminIdentity& who, const std::string& id, int64_t now_s, uint32_t now_ms) {
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
    if (n > 0) record(who, now_s, "bot_stopped_all", std::to_string(n) + (n == 1 ? " match" : " matches"), std::string());         // (stopping nothing is not logged)
    JsonValue o = JsonValue::make_object();
    o.set("stopped", int_value(static_cast<int64_t>(n)));
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

void BotMatches::update(int64_t now_s, uint32_t now_ms) {
    if (now_s < last_prune_s_ || now_s - last_prune_s_ >= 60) {
        last_prune_s_ = now_s;
        forget_ended(rooms_.bot_matches(now_ms));
        schedules_.repair_clock(now_s);                                              // (a time that cannot be true, from a clock that was wrong or a damaged row, is brought back once a minute)
    }
    if (!scheduling_) return;                                                        // (the operator switched the schedules off: they are kept and shown, and none of them starts a match)
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
        if (batch.started < s.what.count) line += " (" + batch.why + ")";
        note(line);
        schedules_.note_run(s.id, batch.started > 0 ? now_s : s.last_run, bot_schedule_next_after_run(s, now_s));
    }
}

}  // namespace ants::server
