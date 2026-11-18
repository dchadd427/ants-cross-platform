#pragma once

// The admin side of the server's own matches of computer players (docs/SERVER_REPLAYS.md, "Matches of computer players", and docs/MAP_SUBMISSIONS.md, "The admin page"): what the admin page's section "Bot matches" asks
// for, and the schedules that start matches by themselves. It sits between the door of the admin API (SubmissionApi, which checks the pass, the token, the origin and the JSON before it hands a call over: nothing here is
// reached by anybody else), the schedules (bot_schedules.hpp) and the rooms (RoomManager, which makes the rooms within its limits). One thread, like everything else of the server: the main loop calls update() every pass and
// the door calls handle() from the same loop, so nothing here runs beside a match.
//
// The calls (all under /admin/api; the tail is what follows it):
//   GET  bot-matches                      the matches that go now, the limit and the places
//   GET  bot-options                      the maps that a batch may name
//   GET  bot-schedules                    the schedules
//   POST bot-matches                      start now: the choices of one batch (bot_match_spec.hpp), 1 - 8 matches
//   POST bot-matches/<id>/stop            end one match (the id is this object's own, never the room's code)
//   POST bot-matches/stop-all             end every match of computer players
//   POST bot-schedules                    a new schedule
//   POST bot-schedules/<id>               change a schedule (any of its name, on, when, what)
//   POST bot-schedules/<id>/delete        delete a schedule
// What was typed (a schedule's name, the map's name of a player, the name of the admin who started a match) is data, and the answers say so (untrusted, untrusted_fields).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ants_ctl/http.hpp"
#include "ants_ctl/json.hpp"
#include "ants_server/admin_audit.hpp"
#include "ants_server/admin_pass.hpp"
#include "ants_server/bot_schedules.hpp"
#include "ants_server/room_manager.hpp"

namespace ants::server {

class BotMatches {
public:
    /// `audit` may be null (a server without the admin door has none: only the schedules run then)
    BotMatches(RoomManager& rooms, BotScheduleStore& schedules, AdminAudit* audit);

    /// One call of the admin API whose tail starts with "bot-". `body` is the parsed JSON of a change (the null value for a read); `now_s` is the wall clock (seconds since 1970), `now_ms` the steady millisecond clock of the loop.
    ctl::HttpResponse handle(const ctl::HttpRequest& request, const AdminIdentity& who, const std::string& tail, const ctl::JsonValue& body, int64_t now_s, uint32_t now_ms);

    /// The main loop's pass: the schedules that are due start their matches (within the limits: a run that a limit holds back is skipped, never queued)
    void update(int64_t now_s, uint32_t now_ms);

    /// Lines for the server's log: what a schedule did when it ran, by the schedule's number (never its name)
    std::vector<std::string> take_notices();

private:
    struct Started {
        std::string id;                         // this object's own name for the match: "m1", "m2" ... (the room's code is never given out)
        std::string code;                       // the room's code
        int64_t started_s{0};
        std::string by;                         // the admin who pressed Start now ("" for a match of a schedule)
        std::string schedule;                   // the schedule's name when it started the match
    };
    struct Batch {
        size_t started{0};
        BotRefusal refusal{BotRefusal::None};
        std::string why;
    };

    ctl::HttpResponse list_matches(int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse list_options();
    ctl::HttpResponse list_schedules();
    ctl::HttpResponse start_now(const AdminIdentity& who, const ctl::JsonValue& body, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse stop_one(const AdminIdentity& who, const std::string& id, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse stop_all(const AdminIdentity& who, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse schedule_create(const AdminIdentity& who, const ctl::JsonValue& body, int64_t now_s);
    ctl::HttpResponse schedule_update(const AdminIdentity& who, int64_t id, const ctl::JsonValue& body, int64_t now_s);
    ctl::HttpResponse schedule_delete(const AdminIdentity& who, int64_t id, int64_t now_s);
    Batch run_batch(const BotWhat& what, const std::string& by, const std::string& schedule, int64_t now_s, uint32_t now_ms);
    void record(const AdminIdentity& who, int64_t now_s, const char* action, const std::string& target, const std::string& detail);
    void forget_ended(const std::vector<BotMatchInfo>& going);
    void note(std::string line);

    RoomManager& rooms_;
    BotScheduleStore& schedules_;
    AdminAudit* audit_;
    std::vector<Started> started_;
    uint64_t next_id_{1};
    std::vector<std::string> notices_;
};

}  // namespace ants::server
