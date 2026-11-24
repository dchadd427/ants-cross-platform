#pragma once

// The door of the plays and the likes of replays (ants_server --counts-port, docs/REPLAY_COUNTS.md): the routes that change a number of the match history. It is a door of its own (ctl::HttpServer::listen_door): the
// public door of the history is read only and stays so. No route here lists anybody: an answer holds counts, and the one list that a person is given is their own.
//
//   POST /counts/play/<id>             anyone: one play of the replay (a page counted 10 seconds of it playing). 204; 404 when the match is not in the history or its recording cannot be played here; 429 when the
//                                      door's budget of plays is used up; 503 when the database does not answer. Nothing is kept about who played.
//   GET  /counts/mine?ids=<id>,...     a person (the pass of the hub's like gate): which of up to 50 matches they like, and the token of their changes. {"signed_in":true,"liked":[ids],"csrf":"<32 hex>"}.
//   POST /counts/like/<id>             a person, with the token in X-Ants-Csrf: likes the match (once; a second call changes nothing). {"liked":true,"likes":N}. 404 as above; 409 {"error":"limit"} at 1,000 likes of
//   POST /counts/unlike/<id>           the person; 429 {"error":"rate"}. unlike takes the like back, for any match that is in the history. {"liked":false,"likes":N}.
//   POST /counts/forget                the hub only (nginx does not pass it): takes back every like of the pass's person. {"removed":N}
//
// Who the person is comes from the pass alone (like_pass.hpp); no cookie, address or name comes here. A request that says it is cross-site is refused whatever it holds. Every answer is JSON with no store.
// One thread, like the rest of the server: the store's writer connection is the game server's own.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ants_ctl/http.hpp"
#include "ants_server/history_store.hpp"
#include "ants_server/like_pass.hpp"
#include "ants_server/replay_store.hpp"

namespace ants::server {

struct ReplayCountsConfig {
    std::string gate_secret;                         // ANTS_ADMIN_GATE_SECRET ("" or not good: likes are off, plays still count)
    std::function<int64_t()> clock_s;                // seconds since 1970 UTC (empty: the system clock)
};

class ReplayCountsApi {
public:
    static constexpr size_t kMaxIds = 50;            // the matches that one GET /counts/mine asks about
    static constexpr double kPlayBurst = 100;        // the door's budget of plays: a token bucket over all visitors, 10 a second with a burst of 100 (nginx limits each address first)
    static constexpr double kPlaysPerSecond = 10;
    static constexpr double kPersonBurst = 10;       // the changes of one person: a burst of 10, then one every 2 seconds
    static constexpr double kPersonPerSecond = 0.5;
    static constexpr size_t kPersonsKept = 4096;     // the people whose budget is remembered (one that is full again is forgotten)

    /// `history` and `replays` (null: this server keeps no recordings, so nothing can be played or liked) must outlive the api.
    ReplayCountsApi(HistoryStore& history, const ReplayStore* replays, ReplayCountsConfig config);

    /// Whether likes can be checked at all (a good key was given)
    bool likes_on() const { return gate_.open(); }
    /// The body of one request is never read: a request with a body is refused. The limit for the door's head check.
    static size_t body_limit(const ctl::HttpRequest&) { return 0; }

    ctl::HttpResponse handle(const ctl::HttpRequest& request, uint32_t now_ms);

    /// Lines for the server's log (a pass that is refused: one a minute at the most for each kind), once each
    std::vector<std::string> take_notices();
    /// The state of a recording for a play or a like (the list's own rule: kept and played by this build, or a file whose head is not read yet)
    bool playable(const std::string& id) const;

private:
    struct Bucket {
        double tokens{0};
        uint32_t at_ms{0};
    };
    ctl::HttpResponse play(const std::string& id, uint32_t now_ms);
    ctl::HttpResponse mine(const ctl::HttpRequest& request, int64_t now_s);
    ctl::HttpResponse change(const ctl::HttpRequest& request, const std::string& kind, const std::string& id, int64_t now_s, uint32_t now_ms);
    bool take_play(uint32_t now_ms);
    bool take_person(const std::string& ref, uint32_t now_ms);
    /// The person of the request's pass, or the answer that refuses it
    bool person_of(const ctl::HttpRequest& request, int64_t now_s, PersonIdentity& who, ctl::HttpResponse& refusal);
    void notice(const std::string& kind, int64_t now_s, const std::string& text);

    HistoryStore& history_;
    const ReplayStore* replays_;
    std::function<int64_t()> clock_s_;
    LikeGate gate_;
    Bucket plays_;
    bool plays_armed_{false};
    std::unordered_map<std::string, Bucket> persons_;
    std::unordered_map<std::string, int64_t> notice_at_;
    std::vector<std::string> notices_;
};

}  // namespace ants::server
