#pragma once

// The admin side of the server's own matches of computer players (docs/SERVER_REPLAYS.md, "Matches of computer players", and docs/MAP_SUBMISSIONS.md, "The admin page"): what the admin page's section "Bot matches" asks
// for, and the schedules that start matches by themselves. It sits between the door of the admin API (SubmissionApi, which checks the pass, the token, the origin and the JSON before it hands a call over: nothing here is
// reached by anybody else), the schedules (bot_schedules.hpp) and the rooms (RoomManager, which makes the rooms within its limits). One thread, like everything else of the server: the main loop calls update() every pass and
// the door calls handle() from the same loop, so nothing here runs beside a match.
//
// The calls (all under /admin/api; the tail is what follows it):
//   GET  bot-matches                      the matches that go now, the queues of starts that wait, the limit, the places, how busy the loop is
//   GET  bot-matches/summary              the counters only (what the chat's /bots status asks): small whatever the number of matches
//   GET  bot-options                      every map of the server (the catalog) with what a room can do with it
//   GET  bot-schedules                    the schedules (and whether the server runs them at all: `scheduling`)
//   GET  bot-rotation                     the two lists of the rotation, the defaults, the most maps of a list
//   POST bot-matches                      start now: the choices of one batch (bot_match_spec.hpp), 1 - 10,000 matches, as many as the limits allow now and the rest in a queue (all at once, or one every X minutes)
//   POST bot-matches/<id>/stop            end one match, or cancel the rest of one queue (the id is this object's own, never the room's code)
//   POST bot-matches/stop-all             end every match of computer players and cancel every queue
//   POST bot-rotation                     replace the list of one rules (the file names of the maps, each of the catalog and, if it is new, playable now)
//   POST bot-rotation/reset               put the list of one rules back to the defaults
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
#include "ants_server/bot_rotation.hpp"
#include "ants_server/bot_schedules.hpp"
#include "ants_server/room_manager.hpp"

namespace ants::server {

class BotMatches {
public:
    /// `audit` may be null (a server without the admin door has none: only the schedules run then). The lists of `rotation` are given to the rooms at once.
    BotMatches(RoomManager& rooms, BotScheduleStore& schedules, BotRotationStore& rotation, AdminAudit* audit);

    /// One call of the admin API whose tail starts with "bot-". `body` is the parsed JSON of a change (the null value for a read); `now_s` is the wall clock (seconds since 1970), `now_ms` the steady millisecond clock of the loop.
    ctl::HttpResponse handle(const ctl::HttpRequest& request, const AdminIdentity& who, const std::string& tail, const ctl::JsonValue& body, int64_t now_s, uint32_t now_ms);

    /// The main loop's pass: the starts that wait for their time start when it comes (a start that a limit holds back waits for a free place), the schedules that are due start their matches (within the limits: a run that a limit
    /// holds back is skipped, never queued), and the maps of the catalog that have no verdict yet are judged a few at a time. Nothing that waits is kept anywhere: a restart of the server drops it.
    void update(int64_t now_s, uint32_t now_ms);

    /// How many starts wait now (the starts that the queues have still to make) and how many queues there are
    size_t waiting_count() const noexcept;
    size_t queue_count() const noexcept { return queues_.size(); }

    /// Whether the schedules run at all. The operator's off switch is --bot-match-every-min 0 (ANTS_BOT_MATCH_EVERY_MIN=0): the schedules can still be seen and edited on the admin page, and none of them starts a match;
    /// the admin page's Start now is not a schedule and still works. On by default.
    void set_scheduling(bool on) { scheduling_ = on; }
    bool scheduling() const noexcept { return scheduling_; }

    /// Lines for the server's log: what a schedule did when it ran, by the schedule's number (never its name)
    std::vector<std::string> take_notices();

private:
    struct Started {
        std::string id;                         // this object's own name for the match: "m1", "m2" ... (the room's code is never given out)
        std::string code;                       // the room's code
        int64_t started_s{0};
        std::string by;                         // the admin who pressed Start now ("" for a match of a schedule)
        std::string schedule;                   // the schedule's name when it started the match
        uint64_t batch{0};                      // the batch (queue) that it belongs to
    };
    struct Queue {                              // the starts of one batch that are still to be made: ONE object however many (10,000 are the most that wait in all the queues)
        std::string id;                         // this object's own name for it: "q1", "q2" ...
        BotWhat what;                           // the choices of the batch as they were asked (count: how many in all); each match is dealt its draws when it starts
        std::string by;
        std::string schedule;
        uint64_t batch{0};
        uint32_t remaining{0};                  // the starts that are still to be made
        uint32_t started{0}, finished{0}, kept{0};      // made, ended (done), and made with a slot of the hour's budget of recordings: the match is kept as a replay when it ends
        uint32_t every_ms{0};                   // the batch's spacing (0: a start whenever there is room)
        uint32_t due_ms{0};                     // with a spacing: the loop's clock at which the next start is due
        bool held{false};                       // a limit holds the next start back
        BotRefusal why{BotRefusal::None};       // which one
    };
    struct Batch {
        size_t started{0};
        size_t queued{0};                       // the starts of the batch that were put in a queue
        BotRefusal refusal{BotRefusal::None};   // why the loop of the first starts stopped, if it did
        std::string why;
        bool queue_full{false};                 // the rest of the batch did not fit the queues
    };

    ctl::HttpResponse list_matches(int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse list_options();
    ctl::HttpResponse list_schedules();
    ctl::HttpResponse list_rotation();
    ctl::HttpResponse rotation_save(const AdminIdentity& who, const ctl::JsonValue& body, int64_t now_s);
    ctl::HttpResponse rotation_reset(const AdminIdentity& who, const ctl::JsonValue& body, int64_t now_s);
    ctl::HttpResponse start_now(const AdminIdentity& who, const ctl::JsonValue& body, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse stop_one(const AdminIdentity& who, const std::string& id, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse stop_all(const AdminIdentity& who, int64_t now_s, uint32_t now_ms);
    ctl::HttpResponse schedule_create(const AdminIdentity& who, const ctl::JsonValue& body, int64_t now_s);
    ctl::HttpResponse schedule_update(const AdminIdentity& who, int64_t id, const ctl::JsonValue& body, int64_t now_s);
    ctl::HttpResponse schedule_delete(const AdminIdentity& who, int64_t id, int64_t now_s);
    ctl::HttpResponse summary(uint32_t now_ms);
    Batch run_batch(const BotWhat& what, const std::string& by, const std::string& schedule, int64_t now_s, uint32_t now_ms, bool queue_rest);
    size_t release_queues(int64_t now_s, uint32_t now_ms);
    void sync_started(uint32_t now_ms);
    void credit_ended(const Started& s);
    size_t running_of(uint64_t batch) const;
    void drop_idle_queues();
    ctl::JsonValue queue_json(const Queue& q, int64_t now_s, uint32_t now_ms, bool full);
    void warm_maps(uint32_t now_ms);
    void record(const AdminIdentity& who, int64_t now_s, const char* action, const std::string& target, const std::string& detail);
    void forget_ended(const std::vector<BotMatchInfo>& going);
    void note(std::string line);

    RoomManager& rooms_;
    BotScheduleStore& schedules_;
    BotRotationStore& rotation_;
    AdminAudit* audit_;
    std::vector<Started> started_;
    std::vector<Queue> queues_;                 // in the order that the batches were made
    uint64_t next_id_{1};
    uint64_t next_batch_{1};
    uint32_t next_release_ms_{0};               // while a limit holds a start back, the loop asks again once a second, not at every pass
    bool release_held_{false};
    uint32_t next_sync_ms_{0};                  // the matches that ended are looked for once a second
    int64_t last_prune_s_{0};                   // the schedules' clocks are mended once a minute
    uint32_t next_warm_ms_{0};                  // the next pass that looks for maps without a verdict
    bool warm_waits_{false};
    bool scheduling_{true};
    std::vector<std::string> notices_;
};

}  // namespace ants::server
