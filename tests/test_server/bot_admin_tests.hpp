// The tests of what the admin page asks of the server's own matches of computer players, and of the schedules that start them (RoomManager::start_bot_match with its choices and its limits, BotMatches, BotScheduleStore, the
// routes of SubmissionApi that hand the page's calls over; docs/SERVER_REPLAYS.md "Matches of computer players" and "Schedules", docs/MAP_SUBMISSIONS.md "The admin page"): the limit of eight matches, of sixteen places and
// of what people use; the choices (levels, rules, bots, teams and splits, the maps); stop and stop all; every route behind the door's pass, token and origin; what Start now answers, and what the log keeps; the schedule
// routes; and the schedules that run rooms (a run that a limit holds back is skipped, not queued; a restart does not run at once; the days of the clock change). Included by test_server_files.cpp after bot_match_tests.hpp,
// whose fixtures it uses; test_server.cpp calls run_bot_admin_tests(). The Pacific clock and the store alone are test_bot_schedules.cpp.
#pragma once

#include "ants_server/admin_pass.hpp"
#include "ants_server/bot_matches.hpp"
#include "ants_server/bot_rotation.hpp"
#include "ants_server/bot_schedules.hpp"
#include "ants_db/database.hpp"
#include "ants_db/migrations.hpp"
#include "ants_server/crypto.hpp"
#include "ants_server/db_schema.hpp"
#include "ants_server/pacific_time.hpp"
#include "ants_server/submissions_http.hpp"

namespace {

const char* const kBotGateKey = "testing-gate-key-0b8e2d71a4c6f395-z";
const char* const kBotRefDave = "0123456789abcdef";
const char* const kBotRefMia = "fedcba9876543210";
const int64_t kBotWall0 = 1800000000;                        // 2027-01-15 08:00 UTC

// A pass as the hub's gate makes it (docs/HUB.md): signed with the RAW key, good for a minute, a new nonce each time
std::string bot_pass(int64_t now, const std::string& ref, const std::string& name, uint64_t& counter) {
    char nonce[32];
    std::snprintf(nonce, sizeof nonce, "%016llx", static_cast<unsigned long long>(++counter));
    const std::string payload = "{\"exp\":" + std::to_string(now + 60) + ",\"name\":\"" + name + "\",\"nonce\":\"" + nonce + "\",\"ref\":\"" + ref + "\",\"role\":\"admin\"}";
    const std::string body = "v1." + base64url_encode(payload);
    return body + "." + hex_encode(hmac_sha256(hmac_sha256(kBotGateKey, "ants-admin-pass-v1"), body));
}

// The admin page's door over a world of rooms, a database and the real routes: a request is made as nginx makes it (the pass in its header), and the answers are the door's
struct BotDoor {
    ReplayClock clock;
    ListedWorld w;
    db::Database database;
    SubmissionConfig config;
    std::unique_ptr<SubmissionStore> store;
    std::unique_ptr<AdminAudit> audit;
    std::unique_ptr<SubmissionApi> api;
    std::unique_ptr<BotScheduleStore> schedules;
    std::unique_ptr<BotRotationStore> rotation;
    std::unique_ptr<BotMatches> bots;
    uint64_t counter{0x3000};
    int64_t wall{kBotWall0};

    explicit BotDoor(const char* tag, ServerLimits limits = ServerLimits(), bool replays = true, bool hook = true, uint32_t bot_saves_per_hour = 30) : w((std::string(tag) + "-maps").c_str(), limits) {          // (the folder of the map of the players is not the folder of the replays: temp_dir_for makes its folder new)
        std::string why;
        const auto need = [](bool ok, const char* what) {                     // (a constructor cannot return from a failed assertion: it is told, and the test fails where it uses the door)
            ++g_assert_count;
            if (!ok) ANTS_TEST_REPORT(what);
        };
        if (replays) {
            ReplayConfig rc = replay_config(tag, &clock);
            rc.max_bot_saves_per_hour = bot_saves_per_hour;
            need(w.mgr.enable_replays(rc, false, why), "the door's replays");
            (void)w.mgr.take_notices();
        }
        w.mgr.seed_bot_matches(20261118u);
        need(db::open_and_migrate(database, ":memory:", db_migrations(), db::OpenOptions()).ok(), "the door's database");
        config.dir = (fs::path(temp_dir_for((std::string(tag) + "-submissions").c_str())) / "files").string();
        config.min_free_bytes = 1;
        store = std::make_unique<SubmissionStore>(database, config, [](const std::string&) { return false; });
        store->open(wall, why);
        audit = std::make_unique<AdminAudit>(database);
        audit->open(why);
        SubmissionApiConfig sc;
        sc.gate_secret = kBotGateKey;
        api = std::make_unique<SubmissionApi>(*store, *audit, sc);
        schedules = std::make_unique<BotScheduleStore>(&database);
        schedules->open(wall, why);
        rotation = std::make_unique<BotRotationStore>(&database);
        rotation->open(why);
        bots = std::make_unique<BotMatches>(w.mgr, *schedules, *rotation, audit.get());
        if (hook) {
            api->set_bot_routes([this](const ctl::HttpRequest& request, const AdminIdentity& who, const std::string& tail, const ctl::JsonValue& body, int64_t now_s, uint32_t now_ms) {
                return bots->handle(request, who, tail, body, now_s, now_ms);
            });
        }
    }

    ctl::HttpRequest req(const std::string& method, const std::string& tail, const std::map<std::string, std::string>& headers = {}, const std::string& body = "") {
        ctl::HttpRequest r;
        r.method = method;
        const size_t question = tail.find('?');
        r.path = "/admin/api/" + tail.substr(0, question);
        if (question != std::string::npos) r.query = tail.substr(question + 1);
        r.headers.insert(headers.begin(), headers.end());
        r.body = body;
        r.peer = "172.18.0.4";
        return r;
    }
    ctl::HttpResponse call(const ctl::HttpRequest& r) { return api->handle(r, wall, w.now); }
    std::string pass(const std::string& ref = kBotRefDave, const std::string& name = "dave") { return bot_pass(wall, ref, name, counter); }
    ctl::HttpResponse get(const std::string& tail, const std::string& with_pass = "<new>") {
        std::map<std::string, std::string> h;
        if (!with_pass.empty()) h["x-ants-admin-pass"] = with_pass == "<new>" ? pass() : with_pass;
        return call(req("GET", tail, h));
    }
    std::string csrf(const std::string& ref = kBotRefDave, const std::string& name = "dave") {
        return json(call(req("GET", "me", {{"x-ants-admin-pass", pass(ref, name)}}))).get("csrf").str();
    }
    // a change as the page makes it: the pass, the token of the account, JSON, the same origin; `extra` changes a header ("<erase>" takes it out)
    ctl::HttpResponse post(const std::string& tail, const std::string& body = "{}", const std::string& ref = kBotRefDave, const std::string& name = "dave", std::map<std::string, std::string> extra = {}) {
        std::map<std::string, std::string> h = {{"x-ants-admin-pass", pass(ref, name)}, {"x-ants-csrf", csrf(ref, name)}, {"content-type", "application/json"}, {"sec-fetch-site", "same-origin"}};
        for (const auto& e : extra) {
            if (e.second == "<erase>") h.erase(e.first);
            else h[e.first] = e.second;
        }
        return call(req("POST", tail, h, body));
    }
    static ctl::JsonValue json(const ctl::HttpResponse& r) {
        ctl::JsonValue v;
        ctl::JsonLimits limits;
        limits.max_bytes = 4u * 1024u * 1024u;                      // (the catalog of the server's maps is about 90 kB: the default 64 kB is for what a visitor sends)
        ctl::parse_json(r.body, v, nullptr, limits);
        return v;
    }
    std::vector<AuditEntry> log() { return audit->list(200); }
};

// The body of a batch, as the page sends it
std::string batch_body(int count, const std::string& map = "random:rotation", const std::string& rules = "random", const std::string& bots = "random", const std::string& teams = "random", const std::string& split = "",
                       const std::string& levels = "[\"rmh\",\"rmh\",\"rmh\",\"rmh\"]") {
    return "{\"count\":" + std::to_string(count) + ",\"map\":\"" + map + "\",\"rules\":\"" + rules + "\",\"bots\":\"" + bots + "\",\"levels\":" + levels + ",\"teams\":\"" + teams + "\",\"split\":\"" + split + "\"}";
}

const char* const kBotClosed = "{\"error\":\"closed\",\"message\":\"The admin page is closed.\"}";

BotMatchSpec fight_island_spec() {
    BotMatchSpec s;
    s.map = "Fight Island.lvl";
    s.rules = BotRules::Kills187;
    s.bots = 2;
    return s;
}

// How many maps of the list of the rotation for those rules a room can play now (the draw skips the others)
size_t drawable(const RoomManager& m, bool kills_187) {
    size_t n = 0;
    for (const BotRotationEntry& e : m.bot_rotation(kills_187)) {
        uint32_t minutes = 0;
        if (m.bot_map_refusal(e.file, e.approved, minutes).empty()) ++n;
    }
    return n;
}

// Starts matches of `spec` one after another until one is refused, or `most`; the codes of those that were made
std::vector<std::string> start_many(World& w, const BotMatchSpec& spec, size_t most, BotRefusal* refusal = nullptr, std::string* why = nullptr) {
    std::vector<std::string> codes;
    for (size_t i = 0; i < most; ++i) {
        std::string code, reason;
        BotRefusal r = BotRefusal::None;
        if (!w.mgr.start_bot_match(w.now, spec, code, reason, &r)) {
            if (refusal != nullptr) *refusal = r;
            if (why != nullptr) *why = reason;
            break;
        }
        codes.push_back(code);
    }
    return codes;
}

}  // namespace

void run_bot_admin_tests() {
    TEST_CASE("S3.270 At Most Eight Matches Of Computer Players Go At Once And Sixteen Places Are Theirs; A Ninth Match Or A Seventeenth Place Is Refused With The Reason, A Stopped Match Frees Its Place, And Matches Start A Second Apart") {
        ReplayClock clock;
        std::string why;
        {   // eight: the ninth is refused, and no room is made for it
            World w;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botadmin-cap", &clock), false, why));
            (void)w.mgr.take_notices();
            w.mgr.seed_bot_matches(31u);
            BotRefusal refusal = BotRefusal::None;
            std::vector<std::string> codes = start_many(w, BotMatchSpec(), 20, &refusal, &why);
            ASSERT_EQ(codes.size(), kBotMatchesMax);
            ASSERT_TRUE(refusal == BotRefusal::Cap && why.find("8 matches of computer players go already") != std::string::npos);
            ASSERT_EQ(w.mgr.bot_match_count(), kBotMatchesMax);
            ASSERT_EQ(w.mgr.list(w.now).size(), kBotMatchesMax);
            ASSERT_TRUE(w.mgr.stop_bot_match(codes[3], w.now));                          // (a stopped match is no longer one that goes: its place is free at once)
            ASSERT_EQ(w.mgr.bot_match_count(), kBotMatchesMax - 1);
            std::string code, reason;
            ASSERT_TRUE(w.mgr.start_bot_match(w.now, BotMatchSpec(), code, reason));
            ASSERT_EQ(w.mgr.bot_match_count(), kBotMatchesMax);
            ASSERT_FALSE(w.mgr.start_bot_match(w.now, BotMatchSpec(), code, reason));
        }
        {   // sixteen places: a match on a big map takes more than one
            ListedWorld w("botadmin-places-maps");
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botadmin-places", &clock), false, why));
            (void)w.mgr.take_notices();
            w.mgr.seed_bot_matches(32u);
            BotRefusal refusal = BotRefusal::None;
            const std::vector<std::string> big = start_many(w, fight_island_spec(), 10, &refusal, &why);
            ASSERT_EQ(big.size(), size_t{5});                                           // (150 x 150 is three places: five take fifteen, the sixth would take eighteen)
            ASSERT_TRUE(refusal == BotRefusal::Places && why.find("18 of the 16 places") != std::string::npos);
            ASSERT_EQ(w.mgr.bot_places(), 15u);
            BotMatchSpec tiny;
            tiny.map = "TINY.LVL";
            ASSERT_EQ(start_many(w, tiny, 1).size(), size_t{1});                         // (one more place: sixteen, and six matches)
            ASSERT_EQ(w.mgr.bot_places(), 16u);
            ASSERT_EQ(w.mgr.bot_match_count(), size_t{6});
            refusal = BotRefusal::None;
            ASSERT_TRUE(start_many(w, tiny, 1, &refusal).empty() && refusal == BotRefusal::Places);         // (seven matches are under eight, but seventeen places are over sixteen)
            ASSERT_TRUE(w.mgr.stop_bot_match(big[0], w.now));
            ASSERT_EQ(w.mgr.bot_places(), 13u);
            ASSERT_EQ(start_many(w, tiny, 3).size(), size_t{3});                         // (the places that it held are free)
        }
        {   // people first: while their matches use more than three of every four places, none starts
            ServerLimits limits;
            limits.demo_rooms = 8;
            ListedWorld w("botadmin-people-maps", limits);
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botadmin-people", &clock), false, why));
            (void)w.mgr.take_notices();
            w.mgr.seed_bot_matches(33u);
            for (int i = 0; i < 6; ++i) {
                RoomSpec p = spec_of("pub-" + std::to_string(i), 2);
                p.public_room = true;
                ASSERT_TRUE(w.mgr.create_room(p, w.now).ok);
            }
            ASSERT_TRUE(w.mgr.people_places() == 6 && w.mgr.place_total() == 8 && !w.mgr.people_busy());      // (six of eight is exactly three of every four: not more)
            std::string code, reason;
            ASSERT_TRUE(w.mgr.start_bot_match(w.now, BotMatchSpec(), code, reason));
            RoomSpec seventh = spec_of("pub-6", 2);
            seventh.public_room = true;
            ASSERT_TRUE(w.mgr.create_room(seventh, w.now).ok);
            ASSERT_TRUE(w.mgr.people_busy());
            BotRefusal refusal = BotRefusal::None;
            ASSERT_FALSE(w.mgr.start_bot_match(w.now, BotMatchSpec(), code, reason, &refusal));
            ASSERT_TRUE(refusal == BotRefusal::PeopleBusy && reason == "people's matches use 7 of the server's 8 places" && code.empty());
            ASSERT_EQ(w.mgr.bot_match_count(), size_t{1});                               // (the match that was there goes on: nothing is stopped)
        }
        {   // the matches of one batch are started a second apart, so that setting up their engines is not done in one pass
            World w;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botadmin-gap", &clock), false, why));
            (void)w.mgr.take_notices();
            w.mgr.seed_bot_matches(34u);
            const std::vector<std::string> codes = start_many(w, BotMatchSpec(), 3);
            ASSERT_EQ(codes.size(), size_t{3});
            uint32_t at[3] = {0, 0, 0};
            for (uint32_t t = 0; t < 30000; t += 10) {
                for (size_t i = 0; i < 3; ++i) {
                    if (at[i] == 0 && w.status(codes[i]).state == RoomState::Running) at[i] = t + 1;
                }
                if (at[0] != 0 && at[1] != 0 && at[2] != 0) break;
                w.run(10);
            }
            ASSERT_TRUE(at[0] != 0 && at[1] != 0 && at[2] != 0);
            ASSERT_TRUE(at[1] - at[0] >= kBotStartGapMs - 20 && at[2] - at[1] >= kBotStartGapMs - 20);
            ASSERT_TRUE(at[1] - at[0] <= kBotStartGapMs + 200 && at[2] - at[1] <= kBotStartGapMs + 200);          // (a second apart, and no longer than that)
            ASSERT_EQ(kBotMatchesMax, size_t{8});
            ASSERT_EQ(kBotPlacesMax, 16u);
        }
    } TEST_END();

    TEST_CASE("S3.271 The Choices Of A Match Are What The Page Says: The Bots' Levels, The Rules, The Number Of Bots, Free For All Or The Split That Names The Bots, And The Map; A Choice That Is Fixed Is Never Drawn, A Choice That Is Open Is Drawn Over All Of It") {
        ReplayClock clock;
        std::string why;
        ListedWorld w("botadmin-choice-maps");
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("botadmin-choice", &clock), false, why));
        (void)w.mgr.take_notices();
        w.mgr.seed_bot_matches(41u);
        const auto made = [&](const BotMatchSpec& spec, RoomStatus& out) {
            std::string code, reason;
            BotRefusal refusal = BotRefusal::None;
            const bool ok = w.mgr.start_bot_match(w.now, spec, code, reason, &refusal);
            if (ok) {
                out = w.status(code);
                w.mgr.close_room(code, w.now);
            }
            return ok;
        };
        const auto sweep = [&]() { w.run(31000); };                                      // (closed rooms are forgotten after their keep time)
        {   // the bots: each seat's level, bot by bot in the order of the seats
            const BotLevelChoice levels[] = {BotLevelChoice::Easy, BotLevelChoice::Hard, BotLevelChoice::Medium, BotLevelChoice::Easy};
            const char* const words[] = {"easy", "hard", "medium", "easy"};
            for (uint8_t bots = 2; bots <= 4; ++bots) {
                for (int round = 0; round < 12; ++round) {
                    BotMatchSpec spec;
                    spec.bots = bots;
                    for (size_t i = 0; i < 4; ++i) spec.levels[i] = levels[i];
                    RoomStatus s;
                    ASSERT_TRUE(made(spec, s));
                    ASSERT_EQ(s.bots.size(), static_cast<size_t>(bots));
                    for (size_t i = 0; i < s.bots.size(); ++i) ASSERT_TRUE(s.bots[i].level == words[i] && (i == 0 || s.bots[i - 1].seat < s.bots[i].seat));
                }
                sweep();
            }
        }
        {   // a level that is drawn: any of the three (Easy included), or Medium and Hard alone
            std::set<std::string> any, rmh;
            for (int round = 0; round < 40; ++round) {
                BotMatchSpec spec;
                spec.bots = 4;
                spec.levels = {BotLevelChoice::RandomAny, BotLevelChoice::RandomAny, BotLevelChoice::RandomAny, BotLevelChoice::RandomAny};
                BotMatchSpec def;
                def.bots = 4;
                RoomStatus a, b;
                ASSERT_TRUE(made(spec, a) && made(def, b));
                for (const RoomStatus::Bot& bot : a.bots) any.insert(bot.level);
                for (const RoomStatus::Bot& bot : b.bots) rmh.insert(bot.level);
                if (round % 8 == 7) sweep();
            }
            ASSERT_EQ(any, (std::set<std::string>{"easy", "hard", "medium"}));
            ASSERT_EQ(rmh, (std::set<std::string>{"hard", "medium"}));                   // (the default level never draws Easy)
            sweep();
        }
        {   // the rules and the number of bots: fixed, or drawn
            std::set<size_t> counts;
            for (const BotRules rules : {BotRules::HighestScore, BotRules::Kills187}) {
                for (int round = 0; round < 30; ++round) {
                    BotMatchSpec spec;
                    spec.rules = rules;
                    RoomStatus s;
                    ASSERT_TRUE(made(spec, s));
                    ASSERT_EQ(s.mode, rules == BotRules::Kills187 ? uint8_t{1} : uint8_t{0});
                    counts.insert(s.bots.size());
                }
                sweep();
            }
            ASSERT_EQ(counts, (std::set<size_t>{2, 3, 4}));                              // (the number of bots was drawn: two, three and four all came up)
        }
        {   // the teams: free for all is never teams; a split is the one that names the bots (on three and on four), whichever seats they sit on
            for (int round = 0; round < 30; ++round) {
                BotMatchSpec ffa;
                ffa.bots = 4;
                ffa.teams = BotTeamsChoice::FreeForAll;
                RoomStatus s;
                ASSERT_TRUE(made(ffa, s));
                ASSERT_TRUE(s.room_teams.empty());
            }
            sweep();
            for (const uint8_t bots : {uint8_t{3}, uint8_t{4}}) {
                const std::vector<BotSplit>& splits = bot_splits(bots);
                ASSERT_EQ(splits.size(), size_t{3});
                for (size_t k = 0; k < splits.size(); ++k) {
                    for (int round = 0; round < 6; ++round) {
                        BotMatchSpec spec;
                        spec.bots = bots;
                        spec.teams = BotTeamsChoice::Split;
                        spec.split = static_cast<int>(k);
                        RoomStatus s;
                        ASSERT_TRUE(made(spec, s));
                        ASSERT_EQ(s.bots.size(), static_cast<size_t>(bots));
                        const std::string wanted = std::to_string(s.bots[splits[k].pair[0]].seat) + "+" + std::to_string(s.bots[splits[k].pair[1]].seat);
                        ASSERT_EQ(s.room_teams, wanted);
                    }
                }
                sweep();
            }
        }
        {   // the maps: the original's six (ISLANDS in the original's game only), the players' maps, one named map
            std::set<std::string> original_187, original_score, players;
            for (int round = 0; round < 80; ++round) {
                BotMatchSpec spec;
                spec.map = kBotMapOriginal;
                spec.rules = BotRules::Kills187;
                BotMatchSpec score = spec;
                score.rules = BotRules::HighestScore;
                BotMatchSpec mine;
                mine.map = kBotMapPlayers;
                RoomStatus a, b, c;
                ASSERT_TRUE(made(spec, a) && made(score, b) && made(mine, c));
                original_187.insert(a.map);
                original_score.insert(b.map);
                players.insert(c.map);
                if (round % 20 == 19) sweep();
            }
            ASSERT_EQ(original_187, (std::set<std::string>{"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL"}));
            ASSERT_EQ(original_score, (std::set<std::string>{"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"}));
            ASSERT_EQ(players, (std::set<std::string>{"Fight Island.lvl"}));
            sweep();
            for (const char* name : {"POPcOrN.lvl", "TINY.LVL", "Fight Island.lvl"}) {
                BotMatchSpec spec;
                spec.map = name;
                RoomStatus s;
                for (int round = 0; round < 6; ++round) {
                    ASSERT_TRUE(made(spec, s));
                    ASSERT_EQ(s.map, std::string(name));
                }
            }
            sweep();
            BotMatchSpec islands;                                                        // (ISLANDS with the rules left to the coin is dealt the original's game; with 187 asked it is refused before anything is drawn)
            islands.map = "ISLANDS.LVL";
            for (int round = 0; round < 25; ++round) {
                RoomStatus s;
                ASSERT_TRUE(made(islands, s));
                ASSERT_TRUE(s.map == "ISLANDS.LVL" && s.mode == 0);
            }
            sweep();
            islands.rules = BotRules::Kills187;
            std::string code, reason;
            BotRefusal refusal = BotRefusal::None;
            ASSERT_FALSE(w.mgr.start_bot_match(w.now, islands, code, reason, &refusal));
            ASSERT_TRUE(refusal == BotRefusal::Islands187 && w.mgr.bot_match_count() == 0);
            BotMatchSpec nothing;                                                        // (a map that is not one of the choices, or that this server cannot play now)
            nothing.map = "Nowhere.lvl";
            ASSERT_FALSE(w.mgr.start_bot_match(w.now, nothing, code, reason, &refusal));
            ASSERT_TRUE(refusal == BotRefusal::NoMap && reason.find("not one of the maps of this server") != std::string::npos);
            nothing.map = "tiny.lvl";                                                    // (the name is exact: the choices are the files of the catalog)
            ASSERT_FALSE(w.mgr.start_bot_match(w.now, nothing, code, reason, &refusal) || refusal != BotRefusal::NoMap);
            World bare;
            ASSERT_TRUE(bare.mgr.enable_replays(replay_config("botadmin-choice-bare", &clock), false, why));
            (void)bare.mgr.take_notices();
            BotMatchSpec library;
            library.map = "POPcOrN.lvl";                                                 // (a server without the library does not have the map: it is not one of its maps)
            ASSERT_FALSE(bare.mgr.start_bot_match(bare.now, library, code, reason, &refusal));
            ASSERT_TRUE(refusal == BotRefusal::NoMap && reason.find("not one of the maps of this server") != std::string::npos);
            BotMatchSpec mine;
            mine.map = kBotMapPlayers;
            ASSERT_FALSE(bare.mgr.start_bot_match(bare.now, mine, code, reason, &refusal));
            ASSERT_TRUE(refusal == BotRefusal::NoMap && reason.find("none of the maps") != std::string::npos);
        }
    } TEST_END();

    TEST_CASE("S3.272 The Admin Page Lists What Can Be Chosen (The Original's Six, Three Of The Library, Every Approved Map Of The Players, With Their Sizes And Places) And The Matches That Go (The Seats With Their Levels, The Sides By Seat, The Live Id Once The Public List Shows The Match)") {
        ReplayClock clock;
        std::string why;
        {
            ListedWorld w("botadmin-list-maps");                                         // (the original's six, the library of the repository and the made-up approved map of the players)
            const std::vector<BotMapOption> options = w.mgr.bot_map_options(kBotLoadAll);
            ASSERT_EQ(options.size(), size_t{541});                                      // (every map that the server has: 6 + 534 + 1)
            std::vector<std::string> first;
            for (size_t i = 0; i < 6; ++i) first.push_back(options[i].group + ":" + options[i].file + ":" + std::to_string(options[i].places));
            ASSERT_EQ(first, (std::vector<std::string>{"original:TINY.LVL:1", "original:SMALL.LVL:1", "original:MEDIUM.LVL:1", "original:GAUNTLET.LVL:1", "original:TREASURE.LVL:1", "original:ISLANDS.LVL:1"}));
            std::map<std::string, BotMapOption> by_file;
            size_t library = 0;
            for (const BotMapOption& o : options) {
                by_file[o.file] = o;
                library += o.group == "library" ? 1 : 0;
                ASSERT_TRUE(o.checked && o.playable && o.reason.empty() && o.width > 0 && o.height > 0 && o.players > 0 && o.minutes > 0);
            }
            ASSERT_EQ(library, size_t{534});
            ASSERT_TRUE(by_file["POPcOrN.lvl"].group == "library" && by_file["POPcOrN.lvl"].places == 1);
            ASSERT_TRUE(by_file["Fight Island.lvl"].group == "players" && by_file["Fight Island.lvl"].width == 150 && by_file["Fight Island.lvl"].height == 150 && by_file["Fight Island.lvl"].places == 3 && by_file["Fight Island.lvl"].players == 4);
            ASSERT_TRUE(w.mgr.bot_rotation(false).size() == 9 && w.mgr.bot_rotation(true).size() == 6);
            ASSERT_TRUE(drawable(w.mgr, false) == 9 && drawable(w.mgr, true) == 6);
            w.mgr.mutable_store().set_approved({});                                      // (the maintainer takes the map down: it is not in the catalog, and the list of 187 is one shorter now)
            ASSERT_EQ(w.mgr.bot_map_options(kBotLoadAll).size(), size_t{540});
            ASSERT_EQ(drawable(w.mgr, true), size_t{5});
            World bare;
            ASSERT_TRUE(bare.mgr.bot_map_options(kBotLoadAll).size() == 6 && drawable(bare.mgr, false) == 6 && drawable(bare.mgr, true) == 5);
        }
        {
            ListedWorld w("botadmin-info-maps");
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("botadmin-info", &clock), false, why));
            (void)w.mgr.take_notices();
            w.mgr.seed_bot_matches(51u);
            BotMatchSpec spec;
            spec.map = "TREASURE.LVL";
            spec.rules = BotRules::Kills187;
            spec.bots = 4;
            spec.teams = BotTeamsChoice::Split;
            spec.split = 1;                                                              // (bots 1 and 3 against 2 and 4)
            BotMatchSpec three;
            three.map = "TINY.LVL";
            three.bots = 3;
            three.teams = BotTeamsChoice::Split;
            three.split = 0;                                                             // (bot 1 against 2 and 3)
            BotMatchSpec ffa;
            ffa.map = "SMALL.LVL";
            ffa.bots = 2;
            std::string code, reason;
            ASSERT_TRUE(w.mgr.start_bot_match(w.now, spec, code, reason));
            ASSERT_TRUE(w.mgr.start_bot_match(w.now, three, code, reason));
            ASSERT_TRUE(w.mgr.start_bot_match(w.now, ffa, code, reason));
            std::vector<BotMatchInfo> info = w.mgr.bot_matches(w.now);
            ASSERT_EQ(info.size(), size_t{3});
            std::map<std::string, BotMatchInfo> by_map;
            for (const BotMatchInfo& m : info) by_map[m.map] = m;
            const BotMatchInfo& four = by_map["TREASURE.LVL"];
            ASSERT_TRUE(four.mode == 1 && four.seats.size() == 4 && four.places == 1 && four.live_id.empty());
            ASSERT_TRUE(four.sides == std::string(1, '0') + "2" + "-" + "13");           // (four bots sit on seats 0 to 3: bots 1 and 3 are seats 0 and 2)
            const BotMatchInfo& three_info = by_map["TINY.LVL"];
            ASSERT_EQ(three_info.seats.size(), size_t{3});
            {
                const std::string a(1, static_cast<char>('0' + three_info.seats[0].first));
                const std::string b(1, static_cast<char>('0' + three_info.seats[1].first));
                const std::string c(1, static_cast<char>('0' + three_info.seats[2].first));
                ASSERT_EQ(three_info.sides, a + "-" + b + c);                            // (the first bot plays alone, the others are the team)
            }
            ASSERT_EQ(by_map["SMALL.LVL"].sides, std::string("ffa"));
            ASSERT_TRUE(by_map["SMALL.LVL"].seats.size() == 2 && four.seats[0].first == 0 && four.seats[3].first == 3);
            for (const BotMatchInfo& m : info) {
                for (const auto& seat : m.seats) ASSERT_TRUE(seat.second == "medium" || seat.second == "hard");
                ASSERT_TRUE(m.code.size() >= 4 && m.state != RoomState::Finished);
            }
            // the live id comes with the public list, which shows a match from its start: the matches are listed once they run (20 seconds on, well before a replay is kept), under the same ids as later
            w.run(20000);
            std::set<std::string> board;
            for (const LiveMatch& live : w.mgr.live_board().list()) board.insert(live.id);
            ASSERT_EQ(board.size(), size_t{3});
            info = w.mgr.bot_matches(w.now);
            ASSERT_EQ(info.size(), size_t{3});
            std::map<std::string, std::string> id_of;
            for (const BotMatchInfo& m : info) {
                ASSERT_TRUE(!m.live_id.empty() && board.count(m.live_id) == 1);
                id_of[m.code] = m.live_id;
            }
            w.run(30000);
            info = w.mgr.bot_matches(w.now);
            ASSERT_EQ(info.size(), size_t{3});
            for (const BotMatchInfo& m : info) ASSERT_TRUE(id_of.count(m.code) == 1 && id_of[m.code] == m.live_id);
            ASSERT_TRUE(info[0].code != info[1].code && info[0].live_id != info[1].live_id);
        }
    } TEST_END();

    TEST_CASE("S3.273 Stop Ends One Match Of Computer Players Or All Of Them And Nothing Else: A Match That Ran Thirty Seconds Is Kept As A Replay Up To Where It Stopped, A Room Of People Is Never Touched, A Code That Is No Such Match Is Refused") {
        ReplayClock clock;
        std::string why;
        ListedWorld w("botadmin-stop-maps");
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("botadmin-stop", &clock), false, why));
        (void)w.mgr.take_notices();
        w.mgr.seed_bot_matches(61u);
        ASSERT_TRUE(w.mgr.create_room(spec_of("PEOPLE-1", 2), w.now).ok);                // (a room of the control interface that waits for its people)
        const std::vector<std::string> codes = start_many(w, BotMatchSpec(), 4);
        ASSERT_EQ(codes.size(), size_t{4});
        w.run(50000);                                                                    // (more than thirty seconds of play)
        ASSERT_TRUE(w.mgr.stop_bot_match(codes[0], w.now));
        ASSERT_EQ(w.status(codes[0]).state, RoomState::Failed);
        ASSERT_EQ(w.mgr.bot_match_count(), size_t{3});
        ASSERT_FALSE(w.mgr.stop_bot_match(codes[0], w.now));                             // (already over)
        ASSERT_FALSE(w.mgr.stop_bot_match("PEOPLE-1", w.now));                           // (a room of people is no match of computer players: it is not stopped, however it is asked)
        ASSERT_FALSE(w.mgr.stop_bot_match("NO-SUCH", w.now));
        ASSERT_EQ(w.status("PEOPLE-1").state, RoomState::Waiting);
        ASSERT_EQ(w.mgr.stop_bot_matches(w.now), size_t{3});
        ASSERT_EQ(w.mgr.bot_match_count(), size_t{0});
        ASSERT_EQ(w.mgr.stop_bot_matches(w.now), size_t{0});
        ASSERT_EQ(w.status("PEOPLE-1").state, RoomState::Waiting);
        size_t kept = 0;
        for (const RoomStatus& ended : w.mgr.take_ended(w.now)) {
            if (ended.bots_only && ended.replay_kept) ++kept;
        }
        ASSERT_EQ(kept, size_t{4});                                                      // (every match had run thirty seconds: all four are replays)
        ASSERT_EQ(w.mgr.replay_store()->count(), size_t{4});
    } TEST_END();


    TEST_CASE("S3.274 Every New Route Is Behind The Door: No Pass, A Bad Pass, An Old Pass Or A Used Pass Is The One Answer 'closed'; A Change Needs The Account's Token, The Same Origin, JSON And A JSON Object, And Does Nothing Without Them; A Server With No Routes Says So") {
        BotDoor d("botadmin-door");
        struct Route {
            const char* method;
            const char* tail;
        };
        const Route routes[] = {{"GET", "bot-matches"},          {"GET", "bot-options"},         {"GET", "bot-schedules"},    {"POST", "bot-matches"},          {"POST", "bot-matches/stop-all"},
                                {"POST", "bot-matches/m1/stop"}, {"POST", "bot-schedules"},      {"POST", "bot-schedules/1"}, {"POST", "bot-schedules/1/delete"}, {"GET", "bot-rotation"},
                                {"POST", "bot-rotation"},        {"POST", "bot-rotation/reset"}};
        const std::string start = batch_body(2, "TINY.LVL", "highest-score", "2");
        const std::string schedule = "{\"name\":\"x\",\"when\":{\"kind\":\"every\",\"minutes\":30},\"what\":" + batch_body(1) + "}";
        const std::string rotation_body = "{\"rules\":\"highest-score\",\"maps\":[\"TINY.LVL\"]}";
        const auto body_of = [&](const Route& r) { return std::string(r.tail) == "bot-schedules" ? schedule : std::string(r.tail) == "bot-rotation" || std::string(r.tail) == "bot-rotation/reset" ? rotation_body : start; };
        const auto nothing_done = [&]() { return d.w.mgr.bot_match_count() == 0 && d.schedules->list().empty() && d.log().empty() && !d.rotation->edited(false) && !d.rotation->edited(true); };
        const auto closed = [](const ctl::HttpResponse& r) { return r.status == 403 && r.body == kBotClosed; };
        const std::string token = d.csrf();
        for (const Route& r : routes) {
            const std::map<std::string, std::string> page = {{"x-ants-csrf", token}, {"content-type", "application/json"}, {"sec-fetch-site", "same-origin"}};
            std::map<std::string, std::string> h = page;
            ASSERT_TRUE(closed(d.call(d.req(r.method, r.tail, h, body_of(r)))));                                           // (no pass: even with the token and the rest, which are right)
            h["x-ants-admin-pass"] = "v1.AAAA.bbbb";
            ASSERT_TRUE(closed(d.call(d.req(r.method, r.tail, h, body_of(r)))));                                           // (a pass that is not one)
            h["x-ants-admin-pass"] = bot_pass(d.wall - 600, kBotRefDave, "dave", d.counter);
            ASSERT_TRUE(closed(d.call(d.req(r.method, r.tail, h, body_of(r)))));                                           // (a pass that ended ten minutes ago)
            ASSERT_TRUE(nothing_done());
            ASSERT_TRUE(closed(d.call(d.req("PUT", r.tail, {{"x-ants-admin-pass", "nope"}}, body_of(r)))));              // (nor does another method tell anything)
        }
        for (const Route& r : routes) {
            if (std::string(r.method) != "GET") continue;
            const std::string used = d.pass();
            ASSERT_EQ(d.get(r.tail, used).status, 200);
            ASSERT_TRUE(closed(d.get(r.tail, used)));                                                                       // (a pass is good once: the second use is the one answer)
        }
        // a change, with a good pass each time: the token, the origin, the type and the body come first
        const auto change = [&](const std::string& tail, const std::string& payload, std::map<std::string, std::string> extra = {}) { return d.post(tail, payload, kBotRefDave, "dave", std::move(extra)); };
        for (const Route& r : routes) {
            if (std::string(r.method) != "POST") continue;
            const std::string payload = body_of(r);
            ctl::HttpResponse a = change(r.tail, payload, {{"x-ants-csrf", "<erase>"}});
            ASSERT_TRUE(a.status == 403 && a.body.find("\"error\":\"csrf\"") != std::string::npos);                          // (no token)
            a = change(r.tail, payload, {{"x-ants-csrf", d.csrf(kBotRefMia, "mia")}});
            ASSERT_TRUE(a.status == 403 && a.body.find("\"error\":\"csrf\"") != std::string::npos);                          // (the token of another account)
            a = change(r.tail, payload, {{"x-ants-csrf", token + "0"}});
            ASSERT_TRUE(a.status == 403 && a.body.find("\"error\":\"csrf\"") != std::string::npos);
            a = change(r.tail, payload, {{"sec-fetch-site", "cross-site"}});
            ASSERT_EQ(a.status, 404);                                                                                        // (another site's request is turned away before it is read)
            a = change(r.tail, payload, {{"sec-fetch-site", "same-site"}});
            ASSERT_EQ(a.status, 404);
            a = change(r.tail, payload, {{"content-type", "text/plain"}});
            ASSERT_TRUE(a.status == 415 && a.body.find("bad_content_type") != std::string::npos);
            a = change(r.tail, payload, {{"content-type", "<erase>"}});
            ASSERT_EQ(a.status, 415);
            a = change(r.tail, "[1]");
            ASSERT_TRUE(a.status == 400 && a.body.find("bad_json") != std::string::npos);
            a = change(r.tail, "not json");
            ASSERT_TRUE(a.status == 400 && a.body.find("bad_json") != std::string::npos);
            ASSERT_TRUE(nothing_done());                                                                                     // (none of them did anything: no match, no schedule, no line in the log)
        }
        ASSERT_EQ(d.get("bot-nothing").status, 404);                                                                         // (a name that is no route)
        ASSERT_EQ(d.post("bot-nothing").status, 404);
        ASSERT_EQ(d.post("bot-schedules/abc").status, 404);
        ASSERT_EQ(d.post("bot-schedules/1/other").status, 404);
        ASSERT_EQ(d.post("bot-matches/m1/go").status, 404);
        ASSERT_EQ(d.get("bot-matches/m1/stop").status, 404);                                                                 // (a read of a change)
        ASSERT_TRUE(nothing_done());
        BotDoor bare("botadmin-door-bare", ServerLimits(), true, false);                                                     // (a server that was given no routes)
        ASSERT_TRUE(closed(bare.call(bare.req("GET", "bot-matches"))));
        const ctl::HttpResponse off = bare.get("bot-matches");
        ASSERT_TRUE(off.status == 503 && off.body.find("\"error\":\"bots_off\"") != std::string::npos);
        const ctl::HttpResponse off_post = bare.post("bot-matches", start);
        ASSERT_TRUE(off_post.status == 503 && off_post.body.find("\"error\":\"bots_off\"") != std::string::npos);
        ASSERT_EQ(bare.post("bot-matches", start, kBotRefDave, "dave", {{"x-ants-csrf", "<erase>"}}).status, 403);
    } TEST_END();

    TEST_CASE("S3.275 Start Now Through The Door: The Answer Is How Many Started; Over The Limit Of Eight Is Refused Whole With What Runs And What Is Free; A Batch That Does Not Fit The Places Starts As Many As Fit; People's Matches Hold It Back; The Log Keeps What Was Done And Never A Refusal") {
        {
            BotDoor d("botadmin-start");
            ctl::HttpResponse r = d.post("bot-matches", batch_body(2, "TINY.LVL", "highest-score", "2", "ffa"));
            ASSERT_EQ(r.status, 200);
            ASSERT_TRUE(BotDoor::json(r).get("started").as_int_or(0) == 2 && BotDoor::json(r).get("asked").as_int_or(0) == 2);
            std::vector<AuditEntry> log = d.log();
            ASSERT_EQ(log.size(), size_t{1});
            ASSERT_TRUE(log[0].actor == "dave" && log[0].ref == kBotRefDave && log[0].action == "bot_started" && log[0].target == "2 matches" && log[0].at == d.wall);
            ASSERT_TRUE(log[0].detail.find("TINY.LVL") != std::string::npos && log[0].detail.find("rules highest score") != std::string::npos && log[0].detail.size() <= 200);
            // what goes now
            const ctl::JsonValue list = BotDoor::json(d.get("bot-matches"));
            ASSERT_TRUE(list.get("matches").size() == 2 && list.get("cap").as_int_or(0) == 8 && list.get("untrusted").as_bool_or(false) && list.get("replays").as_bool_or(false));
            ASSERT_TRUE(list.get("places").get("people").as_int_or(-1) == 0 && list.get("places").get("bots").as_int_or(-1) == 2 && list.get("places").get("bots_max").as_int_or(-1) == 16);
            std::set<std::string> fields;
            for (size_t i = 0; i < list.get("untrusted_fields").size(); ++i) fields.insert(list.get("untrusted_fields").at(i).str());
            ASSERT_EQ(fields, (std::set<std::string>{"name", "by", "schedule"}));
            std::set<std::string> codes;
            for (const RoomStatus& s : d.w.mgr.list(d.w.now)) codes.insert(s.code);
            std::set<std::string> ids;
            for (size_t i = 0; i < list.get("matches").size(); ++i) {
                const ctl::JsonValue& m = list.get("matches").at(i);
                ids.insert(m.get("id").str());
                ASSERT_TRUE(m.get("name").str() == "TINY" && m.get("rules").str() == "highest-score" && m.get("teams").str() == "ffa" && m.get("by").str() == "dave" && !m.has("schedule"));
                ASSERT_TRUE(m.get("seats").size() == 2 && m.get("live").is_null() && m.get("started").as_int_or(0) == d.wall);
                ASSERT_TRUE(codes.count(m.get("id").str()) == 0 && m.get("id").str().size() < 6);                         // (the page's name for a match is not the room's code)
            }
            ASSERT_EQ(ids.size(), size_t{2});
            // the limit: eight at once; a batch that is over it is refused whole
            r = d.post("bot-matches", batch_body(7, "TINY.LVL", "highest-score", "2"));
            ASSERT_EQ(r.status, 409);
            const ctl::JsonValue over = BotDoor::json(r);
            ASSERT_TRUE(over.get("error").str() == "cap" && over.get("running").as_int_or(-1) == 2 && over.get("asked").as_int_or(-1) == 7 && over.get("free").as_int_or(-1) == 6);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.log().size() == 1);                                           // (nothing was started, and a refusal is no line of the log)
            ASSERT_EQ(d.post("bot-matches", batch_body(6, "TINY.LVL", "highest-score", "2")).status, 200);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{8});
            r = d.post("bot-matches", batch_body(1, "TINY.LVL", "highest-score", "2"));
            ASSERT_TRUE(r.status == 409 && BotDoor::json(r).get("free").as_int_or(-1) == 0 && BotDoor::json(r).get("running").as_int_or(-1) == 8);
            // a batch of the wrong shape: its own code, nothing started
            for (const auto& bad : std::vector<std::pair<std::string, std::string>>{{batch_body(0), "bad_count"}, {batch_body(9), "bad_count"}, {batch_body(1, "random:nothing"), "bad_map"}, {batch_body(1, "random:rotation", "both"), "bad_rules"},
                                                                                   {batch_body(1, "random:rotation", "random", "5"), "bad_bots"}, {batch_body(1, "random:rotation", "random", "2", "random", "", "[\"loud\"]"), "bad_levels"},
                                                                                   {batch_body(1, "random:rotation", "random", "2", "pairs"), "bad_teams"}, {batch_body(1, "random:rotation", "random", "2", "split", "12-34"), "bad_split"}}) {
                r = d.post("bot-matches", bad.first);
                ASSERT_TRUE(r.status == 400 && BotDoor::json(r).get("error").str() == bad.second);
            }
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 8 && d.log().size() == 2);
            // stop one, and all
            const std::string first = list.get("matches").at(0).get("id").str();
            r = d.post("bot-matches/" + first + "/stop");
            ASSERT_TRUE(r.status == 200 && BotDoor::json(r).get("stopped").as_int_or(0) == 1);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{7});
            log = d.log();
            ASSERT_TRUE(log[0].action == "bot_stopped" && log[0].target == "TINY" && log[0].actor == "dave");
            r = d.post("bot-matches/" + first + "/stop");
            ASSERT_TRUE(r.status == 404 && BotDoor::json(r).get("error").str() == "gone");                                // (it is not running any more)
            ASSERT_EQ(d.post("bot-matches/m999/stop").status, 404);
            ASSERT_EQ(d.log().size(), size_t{3});
            r = d.post("bot-matches/stop-all", "{}", kBotRefMia, "mia");
            ASSERT_TRUE(r.status == 200 && BotDoor::json(r).get("stopped").as_int_or(0) == 7 && d.w.mgr.bot_match_count() == 0);
            log = d.log();
            ASSERT_TRUE(log[0].action == "bot_stopped_all" && log[0].target == "7 matches" && log[0].actor == "mia" && log[0].ref == kBotRefMia);
            r = d.post("bot-matches/stop-all");
            ASSERT_TRUE(r.status == 200 && BotDoor::json(r).get("stopped").as_int_or(-1) == 0);
            ASSERT_EQ(BotDoor::json(d.get("bot-matches")).get("matches").size(), size_t{0});
        }
        {   // the places: five matches on the big map take fifteen; a batch of four starts as many as fit (one, then the next would be over)
            BotDoor d("botadmin-places");
            const ctl::HttpResponse four = d.post("bot-matches", batch_body(4, "Fight Island.lvl", "187", "2"));
            ASSERT_MSG(four.status == 200, four.body);
            ASSERT_EQ(d.w.mgr.bot_places(), 12u);
            ctl::HttpResponse r = d.post("bot-matches", batch_body(2, "Fight Island.lvl", "187", "2"));
            ASSERT_EQ(r.status, 200);
            ASSERT_TRUE(BotDoor::json(r).get("started").as_int_or(0) == 1 && BotDoor::json(r).get("asked").as_int_or(0) == 2 && BotDoor::json(r).get("stopped_for").str() == "places");
            ASSERT_EQ(d.w.mgr.bot_places(), 15u);
            r = d.post("bot-matches", batch_body(1, "Fight Island.lvl", "187", "2"));
            const ctl::JsonValue full = BotDoor::json(r);
            ASSERT_TRUE(r.status == 409 && full.get("error").str() == "places" && full.get("used").as_int_or(0) == 15 && full.get("max").as_int_or(0) == 16);
            ASSERT_EQ(d.log().size(), size_t{2});                                                                            // (the two starts: the refusal is not in the log)
            r = d.post("bot-matches", batch_body(1, "ISLANDS.LVL", "187", "2"));
            ASSERT_TRUE(r.status == 409 && BotDoor::json(r).get("error").str() == "map_187");
            r = d.post("bot-matches", batch_body(1, "Nowhere.lvl", "highest-score", "2"));
            ASSERT_TRUE(r.status == 409 && BotDoor::json(r).get("error").str() == "no_map");
        }
        {   // people first
            ServerLimits limits;
            limits.demo_rooms = 8;
            BotDoor d("botadmin-people", limits);
            for (int i = 0; i < 7; ++i) {
                RoomSpec p = spec_of("pub-" + std::to_string(i), 2);
                p.public_room = true;
                ASSERT_TRUE(d.w.mgr.create_room(p, d.w.now).ok);
            }
            const ctl::HttpResponse r = d.post("bot-matches", batch_body(1));
            const ctl::JsonValue busy = BotDoor::json(r);
            ASSERT_TRUE(r.status == 409 && busy.get("error").str() == "busy" && busy.get("people").as_int_or(0) == 7 && busy.get("total").as_int_or(0) == 8);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 0 && d.log().empty());
            const ctl::JsonValue list = BotDoor::json(d.get("bot-matches"));
            ASSERT_TRUE(list.get("places").get("people").as_int_or(0) == 7 && list.get("places").get("total").as_int_or(0) == 8);
        }
        {   // no replays: a match that nobody could look at again is not played
            BotDoor d("botadmin-noreplay", ServerLimits(), false);
            const ctl::HttpResponse r = d.post("bot-matches", batch_body(1));
            ASSERT_TRUE(r.status == 503 && BotDoor::json(r).get("error").str() == "no_replays");
            ASSERT_FALSE(BotDoor::json(d.get("bot-matches")).get("replays").as_bool_or(true));
            ASSERT_TRUE(d.w.mgr.list(d.w.now).empty() && d.log().empty());
        }
        {   // the options
            BotDoor d("botadmin-options");
            const ctl::JsonValue o = BotDoor::json(d.get("bot-options"));
            ASSERT_EQ(o.get("maps").size(), size_t{541});                                // (every map: the first call judges some of them and lists the rest as not checked: bot_rotation_tests.hpp)
            const ctl::JsonValue& last = o.get("maps").at(540);
            ASSERT_TRUE(last.get("file").str() == "Fight Island.lvl" && last.get("name").str() == "Fight Island" && last.get("group").str() == "players" && last.get("width").as_int_or(0) == 150 && last.get("players").as_int_or(0) == 4);
            ASSERT_TRUE(o.get("untrusted").as_bool_or(false) && o.get("untrusted_fields").size() == 1 && o.get("untrusted_fields").at(0).str() == "name");
        }
    } TEST_END();

    TEST_CASE("S3.276 The Schedule Routes: A Schedule Is Added, Listed, Changed, Turned Off And On And Deleted, Each With Its Line In The Log; Every Refusal Has Its Code And Changes Nothing; Twenty At The Most; A Restart Of The Server Brings Them Back") {
        BotDoor d("botadmin-sched");
        const std::string what = batch_body(4, "random:rotation", "highest-score", "4", "random", "", "[\"hard\",\"hard\",\"medium\",\"medium\"]");
        ASSERT_EQ(BotDoor::json(d.get("bot-schedules")).get("schedules").size(), size_t{0});
        ASSERT_EQ(BotDoor::json(d.get("bot-schedules")).get("max").as_int_or(0), 20);
        ctl::HttpResponse r = d.post("bot-schedules", "{\"name\":\"  Evening   swarm\",\"on\":true,\"when\":{\"kind\":\"times\",\"times\":[\"22:00\",\"18:00\"]},\"what\":" + what + "}");
        ASSERT_EQ(r.status, 200);
        ctl::JsonValue s = BotDoor::json(r).get("schedule");
        ASSERT_TRUE(s.get("id").as_int_or(0) == 1 && s.get("name").str() == "Evening swarm" && !s.get("default").as_bool_or(true) && s.get("on").as_bool_or(false));
        ASSERT_TRUE(s.get("when").get("kind").str() == "times" && ctl::to_json(s.get("when").get("times")) == "[\"18:00\",\"22:00\"]" && s.get("last").as_int_or(-1) == 0);
        ASSERT_EQ(s.get("next").as_int_or(0), next_pacific_run(d.wall, {18 * 60, 22 * 60}));
        ASSERT_TRUE(s.get("what").get("count").as_int_or(0) == 4 && s.get("what").get("rules").str() == "highest-score" && s.get("what").get("bots").str() == "4");
        std::vector<AuditEntry> log = d.log();
        ASSERT_TRUE(log.size() == 1 && log[0].action == "bot_schedule_added" && log[0].target == "Evening swarm" && log[0].actor == "dave" && log[0].detail.find("at 18:00, 22:00 (Pacific)") != std::string::npos);
        // a list, a change of the name, off and on, a change of when
        ASSERT_EQ(BotDoor::json(d.get("bot-schedules")).get("schedules").size(), size_t{1});
        r = d.post("bot-schedules/1", "{\"on\":false}");
        s = BotDoor::json(r).get("schedule");
        ASSERT_TRUE(r.status == 200 && !s.get("on").as_bool_or(true) && s.get("next").is_null());
        ASSERT_EQ(d.log()[0].action, std::string("bot_schedule_off"));
        ASSERT_EQ(d.post("bot-schedules/1", "{\"on\":true}").status, 200);
        ASSERT_EQ(d.log()[0].action, std::string("bot_schedule_on"));
        r = d.post("bot-schedules/1", "{\"name\":\"Late swarm\"}");
        ASSERT_TRUE(r.status == 200 && BotDoor::json(r).get("schedule").get("name").str() == "Late swarm");
        ASSERT_TRUE(d.log()[0].action == "bot_schedule_changed" && d.log()[0].target == "Late swarm");
        r = d.post("bot-schedules/1", "{\"when\":{\"kind\":\"every\",\"minutes\":\"90\"}}", kBotRefMia, "mia");
        s = BotDoor::json(r).get("schedule");
        ASSERT_TRUE(r.status == 200 && s.get("when").get("kind").str() == "every" && s.get("when").get("minutes").as_int_or(0) == 90 && s.get("next").as_int_or(0) == d.wall + 90 * 60);
        ASSERT_TRUE(d.log()[0].detail.find("every 90 minutes") != std::string::npos && d.log()[0].actor == "mia");
        ASSERT_EQ(d.log().size(), size_t{5});
        // the refusals
        const size_t lines = d.log().size();
        for (const auto& bad : std::vector<std::pair<std::string, std::string>>{
                 {"{\"name\":\"\",\"when\":{\"kind\":\"every\",\"minutes\":30},\"what\":" + what + "}", "bad_name"},
                 {"{\"name\":\"" + std::string(41, 'x') + "\",\"when\":{\"kind\":\"every\",\"minutes\":30},\"what\":" + what + "}", "bad_name"},
                 {"{\"name\":\"x\",\"when\":{\"kind\":\"every\",\"minutes\":4},\"what\":" + what + "}", "bad_every"},
                 {"{\"name\":\"x\",\"when\":{\"kind\":\"every\",\"minutes\":1441},\"what\":" + what + "}", "bad_every"},
                 {"{\"name\":\"x\",\"when\":{\"kind\":\"times\",\"times\":[]},\"what\":" + what + "}", "no_times"},
                 {"{\"name\":\"x\",\"when\":{\"kind\":\"times\",\"times\":[\"25:00\"]},\"what\":" + what + "}", "bad_times"},
                 {"{\"name\":\"x\",\"when\":{\"kind\":\"weekly\"},\"what\":" + what + "}", "bad_when"},
                 {"{\"name\":\"x\",\"when\":{\"kind\":\"every\",\"minutes\":30},\"what\":" + batch_body(0) + "}", "bad_count"},
                 {"{\"name\":\"x\",\"when\":{\"kind\":\"every\",\"minutes\":30}}", "bad_count"}}) {
            r = d.post("bot-schedules", bad.first);
            ASSERT_TRUE(r.status == 400 && BotDoor::json(r).get("error").str() == bad.second);
        }
        ASSERT_TRUE(d.schedules->list().size() == 1 && d.log().size() == lines);                                             // (nothing was added, and no line was written)
        r = d.post("bot-schedules/1", "{\"when\":{\"kind\":\"every\",\"minutes\":2}}");
        ASSERT_TRUE(r.status == 400 && d.schedules->find(1)->minutes == 90 && d.log().size() == lines);
        r = d.post("bot-schedules/99", "{\"on\":false}");
        ASSERT_TRUE(r.status == 404 && BotDoor::json(r).get("error").str() == "gone");
        r = d.post("bot-schedules/99/delete");
        ASSERT_TRUE(r.status == 404 && BotDoor::json(r).get("error").str() == "gone");
        // twenty at the most
        for (size_t i = 1; i < kMaxBotSchedules; ++i) ASSERT_EQ(d.post("bot-schedules", "{\"name\":\"s" + std::to_string(i) + "\",\"when\":{\"kind\":\"every\",\"minutes\":60},\"what\":" + batch_body(1) + "}").status, 200);
        r = d.post("bot-schedules", "{\"name\":\"one too many\",\"when\":{\"kind\":\"every\",\"minutes\":60},\"what\":" + batch_body(1) + "}");
        ASSERT_TRUE(r.status == 409 && BotDoor::json(r).get("error").str() == "too_many" && d.schedules->list().size() == kMaxBotSchedules);
        // a restart: the schedules come back from the database, every change that was made is there
        {
            BotScheduleStore again(&d.database);
            std::string why;
            ASSERT_TRUE(again.open(d.wall + 7200, why));
            ASSERT_EQ(again.list().size(), kMaxBotSchedules);
            const BotSchedule* first = again.find(1);
            ASSERT_TRUE(first != nullptr && first->name == "Late swarm" && first->kind == BotSchedule::Kind::Every && first->minutes == 90 && first->on && first->what.count == 4);
            ASSERT_EQ(first->next_run, d.wall + 7200 + 60);                                                                    // (it was due while the server was down: a minute after it is up)
        }
        // a delete, with its line
        r = d.post("bot-schedules/1/delete");
        ASSERT_TRUE(r.status == 200 && d.schedules->find(1) == nullptr && d.schedules->list().size() == kMaxBotSchedules - 1);
        ASSERT_TRUE(d.log()[0].action == "bot_schedule_deleted" && d.log()[0].target == "Late swarm");
        ASSERT_EQ(d.post("bot-schedules/1/delete").status, 404);
        ASSERT_EQ(BotDoor::json(d.get("bot-schedules")).get("schedules").size(), kMaxBotSchedules - 1);
        ASSERT_TRUE(BotDoor::json(d.get("bot-schedules")).get("untrusted").as_bool_or(false));
    } TEST_END();

    TEST_CASE("S3.277 The Schedules Run Rooms: A Run Starts Its Matches On Time And Counts From The Start; A Run That A Limit Holds Back Is Skipped, Never Queued; A Run Starts As Many As Fit; A Restart Does Not Run At Once; Times Of The Day Follow The Pacific Clock On The Days Of The Change; Nothing Is Made Up") {
        const auto every = [](const std::string& name, int minutes, int count) {
            BotSchedulePatch p;
            p.has_name = true;
            p.name = name;
            p.has_when = true;
            p.kind = BotSchedule::Kind::Every;
            p.minutes = minutes;
            p.has_what = true;
            p.what.count = static_cast<uint32_t>(count);
            p.what.spec.map = "TINY.LVL";
            p.what.spec.bots = 2;
            return p;
        };
        const auto times = [](const std::string& name, int minute) {
            BotSchedulePatch p;
            p.has_name = true;
            p.name = name;
            p.has_when = true;
            p.kind = BotSchedule::Kind::Times;
            p.times = {minute};
            p.has_what = true;
            p.what.count = 1;
            p.what.spec.map = "TINY.LVL";
            p.what.spec.bots = 2;
            return p;
        };
        {   // every five minutes, three matches each time; a limit holds a run back and it is skipped
            BotDoor d("botadmin-run");
            std::string why;
            BotSchedule s;
            ASSERT_TRUE(d.schedules->create(every("Quick swarm", 5, 3), kBotWall0, s, why));
            ASSERT_EQ(s.next_run, kBotWall0 + 300);
            d.bots->update(kBotWall0 + 299, d.w.now);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 0 && d.bots->take_notices().empty());                                    // (not before its time)
            d.bots->update(kBotWall0 + 300, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{3});
            const BotSchedule* now = d.schedules->find(s.id);
            ASSERT_TRUE(now->last_run == kBotWall0 + 300 && now->next_run == kBotWall0 + 600);                                // (counted from the start)
            std::vector<std::string> notes = d.bots->take_notices();
            ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot schedule " + std::to_string(s.id) + ": started 3 of 3");        // (the line names the schedule by its number, never by its name)
            d.bots->update(kBotWall0 + 301, d.w.now);                                                                          // (once: a second look at the same time starts nothing more)
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{3});
            const ctl::JsonValue list = BotDoor::json(d.get("bot-matches"));
            ASSERT_TRUE(list.get("matches").size() == 3 && list.get("matches").at(0).get("schedule").str() == "Quick swarm" && !list.get("matches").at(0).has("by") && list.get("matches").at(0).get("started").as_int_or(0) == kBotWall0 + 300);
            // the limit is shared with Start now: five more fill the eight
            ASSERT_EQ(d.post("bot-matches", batch_body(5, "TINY.LVL", "highest-score", "2")).status, 200);
            ASSERT_EQ(d.w.mgr.bot_match_count(), kBotMatchesMax);
            d.bots->update(kBotWall0 + 600, d.w.now);                                                                          // (due, and held back: skipped)
            ASSERT_EQ(d.w.mgr.bot_match_count(), kBotMatchesMax);
            notes = d.bots->take_notices();
            ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot schedule " + std::to_string(s.id) + ": started 0 of 3 (8 matches of computer players go already)");
            ASSERT_TRUE(d.schedules->find(s.id)->last_run == kBotWall0 + 300 && d.schedules->find(s.id)->next_run == kBotWall0 + 900);       // (the next run is five minutes on: it is not waiting for a place)
            ASSERT_EQ(d.post("bot-matches/stop-all").status, 200);                                                             // (places are free again)
            d.bots->update(kBotWall0 + 601, d.w.now);
            d.bots->update(kBotWall0 + 899, d.w.now);
            ASSERT_TRUE(d.w.mgr.bot_match_count() == 0 && d.bots->take_notices().empty());                                    // (and the skipped run was not queued: nothing starts until the next time)
            d.bots->update(kBotWall0 + 900, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{3});
            // as many as fit: six go, a run of five starts two
            ASSERT_TRUE(d.schedules->remove(s.id, why));
            ASSERT_EQ(d.post("bot-matches", batch_body(3, "TINY.LVL", "highest-score", "2")).status, 200);
            BotSchedule five;
            ASSERT_TRUE(d.schedules->create(every("Five", 5, 5), kBotWall0 + 900, five, why));
            (void)d.bots->take_notices();
            d.bots->update(kBotWall0 + 900 + 300, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), kBotMatchesMax);
            notes = d.bots->take_notices();
            ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot schedule " + std::to_string(five.id) + ": started 2 of 5 (8 matches of computer players go already)");
        }
        {   // the matches of people come first: a run that they hold back is skipped too
            ServerLimits limits;
            limits.demo_rooms = 8;
            BotDoor d("botadmin-run-people", limits);
            std::string why;
            BotSchedule s;
            ASSERT_TRUE(d.schedules->create(every("Hourly", 60, 1), kBotWall0, s, why));
            for (int i = 0; i < 7; ++i) {
                RoomSpec p = spec_of("pub-" + std::to_string(i), 2);
                p.public_room = true;
                ASSERT_TRUE(d.w.mgr.create_room(p, d.w.now).ok);
            }
            d.bots->update(kBotWall0 + 3600, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{0});
            const std::vector<std::string> notes = d.bots->take_notices();
            ASSERT_TRUE(notes.size() == 1 && notes[0] == "bot schedule " + std::to_string(s.id) + ": started 0 of 1 (people's matches use 7 of the server's 8 places)");
            ASSERT_EQ(d.schedules->find(s.id)->next_run, kBotWall0 + 7200);
        }
        {   // a schedule that is off starts nothing; a server that was down does not make up what it missed
            BotDoor d("botadmin-run-off");
            std::string why;
            BotSchedule off, hourly;
            BotSchedulePatch p = every("Off", 5, 1);
            p.has_on = true;
            p.on = false;
            ASSERT_TRUE(d.schedules->create(p, kBotWall0, off, why));
            ASSERT_TRUE(d.schedules->create(every("Hourly", 60, 1), kBotWall0, hourly, why));
            d.bots->update(kBotWall0 + 3 * 86400, d.w.now);                                                                     // (three days later: the off schedule does nothing, the hourly one runs once, not 72 times)
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
            ASSERT_EQ(d.schedules->find(hourly.id)->next_run, kBotWall0 + 3 * 86400 + 3600);
            ASSERT_TRUE(d.schedules->find(off.id)->last_run == 0 && d.schedules->find(off.id)->next_run == 0);
            ASSERT_EQ(d.post("bot-schedules/" + std::to_string(hourly.id) + "/delete").status, 200);
            d.bots->update(kBotWall0 + 9 * 86400, d.w.now);                                                                     // (a schedule that was deleted runs no more)
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
        }
        {   // a restart does not run at once: the server comes up three hours after the run was due, and its first match is a minute later
            BotDoor d("botadmin-run-restart");
            std::string why;
            BotSchedule s;
            ASSERT_TRUE(d.schedules->create(every("Often", 15, 1), kBotWall0, s, why));
            const int64_t up = kBotWall0 + 3 * 3600;
            BotScheduleStore store(&d.database);
            ASSERT_TRUE(store.open(up, why));
            BotRotationStore bots_rotation(nullptr);
            BotMatches bots(d.w.mgr, store, bots_rotation, nullptr);
            bots.update(up, d.w.now);
            bots.update(up + 59, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{0});
            bots.update(up + 60, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
            ASSERT_EQ(store.find(s.id)->next_run, up + 60 + 900);
            BotScheduleStore ahead(&d.database);                                                                               // (and a run that is still ahead stays where it was when the server comes back soon)
            ASSERT_TRUE(ahead.open(up + 100, why));
            ASSERT_EQ(ahead.find(s.id)->next_run, up + 60 + 900);
        }
        {   // times of the day by the Pacific clock: 18:00 is 02:00 UTC in winter, and the same minute of the next day is not run twice
            BotDoor d("botadmin-run-times");
            std::string why;
            BotSchedule s;
            ASSERT_TRUE(d.schedules->create(times("Evening", 18 * 60), kBotWall0, s, why));
            const int64_t evening = 1800000000 + 18 * 3600;                                                                     // 2027-01-16 02:00 UTC
            ASSERT_EQ(s.next_run, evening);
            d.bots->update(evening - 1, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{0});
            d.bots->update(evening, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
            for (int64_t t = evening + 1; t < evening + 600; t += 30) d.bots->update(t, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
            ASSERT_EQ(d.schedules->find(s.id)->next_run, evening + 86400);
        }
        {   // the day that has no 02:30: spring forward at 02:00 on 8 March 2026, the schedule runs at 03:00 (10:00 UTC), once
            BotDoor d("botadmin-run-spring");
            std::string why;
            const int64_t before = 1772964000 - 30;                                                                             // 01:59:30 Pacific standard time
            BotSchedule s;
            ASSERT_TRUE(d.schedules->create(times("Half past two", 2 * 60 + 30), before, s, why));
            ASSERT_EQ(s.next_run, int64_t{1772964000});
            d.bots->update(1772964000 - 1, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{0});
            d.bots->update(1772964000, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
            for (int64_t t = 1772964000 + 1; t < 1772964000 + 7200; t += 60) d.bots->update(t, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
            ASSERT_EQ(d.schedules->find(s.id)->next_run, int64_t{1772964000} + 23 * 3600 + 30 * 60);                          // (the next day's 02:30, which exists: 09:30 UTC of the 9th)
        }
        {   // the hour that happens twice: fall back at 02:00 on 1 November 2026, 01:30 runs at the first, and the repeat at 09:30 UTC does not run
            BotDoor d("botadmin-run-fall");
            std::string why;
            const int64_t before = 1793523600 - 3600 - 60;                                                              // 07:59 UTC: 00:59 Pacific daylight time
            BotSchedule s;
            ASSERT_TRUE(d.schedules->create(times("Half past one", 1 * 60 + 30), before, s, why));
            const int64_t first = 1793523600 - 30 * 60;                                                                         // 08:30 UTC: 01:30 daylight time
            ASSERT_EQ(s.next_run, first);
            d.bots->update(first - 1, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{0});
            d.bots->update(first, d.w.now);
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
            ASSERT_EQ(d.post("bot-matches/stop-all").status, 200);
            for (int64_t t = first + 1; t < first + 3 * 3600; t += 30) d.bots->update(t, d.w.now);                               // (through 09:30 UTC, when the clock reads 01:30 again: nothing starts)
            ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{0});
            ASSERT_EQ(d.schedules->find(s.id)->next_run, first + 25 * 3600);                                                    // (the next day's 01:30, in standard time: 09:30 UTC of the 2nd)
        }
    } TEST_END();

    TEST_CASE("S3.278 A Schedule Has Four Levels Whatever The Number Of Bots: The List Gives Four For Two Bots (Also For A Row That Was Kept With Two), So That Raising The Bots And Saving Is Not Refused; A Level That Is Missing Is Still Refused") {
        BotDoor d("botadmin-levels");
        const std::string two = "{\"name\":\"Two bots\",\"on\":true,\"when\":{\"kind\":\"every\",\"minutes\":30},\"what\":" + batch_body(1, "TINY.LVL", "highest-score", "2", "ffa", "", "[\"hard\",\"easy\"]") + "}";
        const ctl::HttpResponse made = d.post("bot-schedules", two);
        ASSERT_EQ(made.status, 200);
        const ctl::JsonValue row = BotDoor::json(made).get("schedule");
        ASSERT_EQ(row.get("what").get("levels").size(), size_t{4});
        std::vector<std::string> levels;
        for (size_t i = 0; i < 4; ++i) levels.push_back(row.get("what").get("levels").at(i).str());
        ASSERT_TRUE(levels == (std::vector<std::string>{"hard", "easy", "rmh", "rmh"}));
        // the page takes what the list gave, raises the bots to four and saves: not refused
        const std::string id = std::to_string(row.get("id").as_int_or(0));
        const std::string raised = "{\"what\":" + batch_body(1, "TINY.LVL", "highest-score", "4", "ffa", "", "[\"hard\",\"easy\",\"rmh\",\"rmh\"]") + "}";
        ASSERT_EQ(d.post("bot-schedules/" + id, raised).status, 200);
        ASSERT_EQ(BotDoor::json(d.get("bot-schedules")).get("schedules").at(0).get("what").get("bots").str(), std::string("4"));
        // a list that holds a hole (what a page that did not pad would send) is refused, and changes nothing
        const std::string hole = "{\"what\":" + batch_body(1, "TINY.LVL", "highest-score", "4", "ffa", "", "[\"hard\",\"easy\",null,null]") + "}";
        const ctl::HttpResponse refused = d.post("bot-schedules/" + id, hole);
        ASSERT_TRUE(refused.status == 400 && BotDoor::json(refused).get("error").str() == "bad_levels");
        // a row of the table that was kept with two levels (before the list gave four) is read, and listed with four
        BotDoor old("botadmin-levels-old");
        db::Statement st;
        ASSERT_TRUE(old.database.prepare(st, "INSERT INTO bot_schedules (name, enabled, is_default, kind, minutes, times, what, last_run, next_run, created_at) VALUES ('Old two', 1, 0, 'every', 30, '[]', ?, 0, 0, 1)").ok());
        ASSERT_TRUE(st.bind_text(1, "{\"count\":1,\"map\":\"TINY.LVL\",\"rules\":\"highest-score\",\"bots\":\"2\",\"levels\":[\"hard\",\"easy\"],\"teams\":\"ffa\",\"split\":\"\"}").ok() && st.step() == db::Statement::Step::Done);
        std::string why;
        BotScheduleStore reopened(&old.database);
        ASSERT_TRUE(reopened.open(kBotWall0, why) && reopened.list().size() == 1);
        const ctl::JsonValue listed = bot_what_json(reopened.list()[0].what);
        ASSERT_TRUE(listed.get("levels").size() == 4 && listed.get("levels").at(0).str() == "hard" && listed.get("levels").at(1).str() == "easy" && listed.get("levels").at(2).str() == "rmh");
    } TEST_END();

    TEST_CASE("S3.279 The Operator's Off Switch (--bot-match-every-min 0): With Scheduling Off No Schedule Starts A Match, Whatever Is Due Or Stored; The List Says So And Promises No Next Start; Start Now Still Works; Switched On Again The Due Schedule Runs") {
        BotDoor d("botadmin-off");
        std::string why;
        BotSchedule s;
        BotSchedulePatch p;
        p.has_name = true;
        p.name = "Quick";
        p.has_when = true;
        p.kind = BotSchedule::Kind::Every;
        p.minutes = 5;
        p.has_what = true;
        p.what.count = 2;
        p.what.spec.map = "TINY.LVL";
        p.what.spec.bots = 2;
        ASSERT_TRUE(d.schedules->create(p, kBotWall0, s, why));
        ASSERT_TRUE(d.bots->scheduling());
        d.bots->set_scheduling(false);
        d.bots->update(kBotWall0 + 300, d.w.now);
        d.bots->update(kBotWall0 + 4000, d.w.now);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 0 && d.bots->take_notices().empty());                                         // (due for an hour, and nothing started)
        const ctl::JsonValue list = BotDoor::json(d.get("bot-schedules"));
        ASSERT_TRUE(!list.get("scheduling").as_bool_or(true) && list.get("schedules").size() == 1 && list.get("schedules").at(0).get("on").as_bool_or(false) && list.get("schedules").at(0).get("next").is_null());
        ASSERT_EQ(d.post("bot-matches", batch_body(1, "TINY.LVL", "highest-score", "2")).status, 200);                           // (Start now is not a schedule)
        ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{1});
        ASSERT_EQ(d.post("bot-schedules", "{\"name\":\"Added while off\",\"when\":{\"kind\":\"every\",\"minutes\":60},\"what\":" + batch_body(1) + "}").status, 200);          // (they can still be edited)
        d.bots->set_scheduling(true);
        d.bots->update(kBotWall0 + 4001, d.w.now);
        ASSERT_EQ(d.w.mgr.bot_match_count(), size_t{4});                                                                       // (one from Start now, the two of "Quick" and the one of the schedule that was added while it was off: both are due)
        const ctl::JsonValue again = BotDoor::json(d.get("bot-schedules"));
        ASSERT_TRUE(again.get("scheduling").as_bool_or(false) && !again.get("schedules").at(0).get("next").is_null());
    } TEST_END();

    TEST_CASE("S3.280 A Change That Changes Nothing Is Not Logged: Stopping Nothing, Saving The Same Schedule Again, Turning On What Is On, Turning Off What Is Off; The Same Save Twice In A Moment (A Double Click) Is One Schedule With One Line; A Different Schedule Is Another") {
        BotDoor d("botadmin-noop");
        ASSERT_EQ(d.post("bot-matches/stop-all").status, 200);
        ASSERT_TRUE(d.log().empty());                                                                                          // (nothing ran: nothing to say)
        ASSERT_EQ(d.post("bot-matches", batch_body(2, "TINY.LVL", "highest-score", "2")).status, 200);
        ASSERT_EQ(d.post("bot-matches/stop-all").status, 200);
        ASSERT_EQ(d.post("bot-matches/stop-all").status, 200);
        size_t stopped = 0;
        for (const AuditEntry& e : d.log()) { if (e.action == "bot_stopped_all") ++stopped; }
        ASSERT_EQ(stopped, size_t{1});                                                                                         // (the second call stopped nothing)
        const std::string body = "{\"name\":\"Evening\",\"on\":true,\"when\":{\"kind\":\"times\",\"times\":[\"18:00\"]},\"what\":" + batch_body(1) + "}";
        const ctl::HttpResponse first = d.post("bot-schedules", body);
        const ctl::HttpResponse second = d.post("bot-schedules", body);                                                         // (the double click)
        ASSERT_TRUE(first.status == 200 && second.status == 200);
        ASSERT_EQ(BotDoor::json(first).get("schedule").get("id").as_int_or(-1), BotDoor::json(second).get("schedule").get("id").as_int_or(-2));
        ASSERT_EQ(BotDoor::json(d.get("bot-schedules")).get("schedules").size(), size_t{1});
        size_t added = 0;
        for (const AuditEntry& e : d.log()) { if (e.action == "bot_schedule_added") ++added; }
        ASSERT_EQ(added, size_t{1});
        const std::string id = std::to_string(BotDoor::json(first).get("schedule").get("id").as_int_or(0));
        const size_t before = d.log().size();
        ASSERT_EQ(d.post("bot-schedules/" + id, "{\"on\":true}").status, 200);                                                  // (on, and it is on)
        ASSERT_EQ(d.post("bot-schedules/" + id, body).status, 200);                                                             // (the same name, time and choices)
        ASSERT_EQ(d.log().size(), before);
        ASSERT_EQ(d.post("bot-schedules/" + id, "{\"on\":false}").status, 200);
        ASSERT_EQ(d.post("bot-schedules/" + id, "{\"on\":false}").status, 200);
        ASSERT_EQ(d.log().size(), before + 1);
        ASSERT_EQ(d.log()[0].action, std::string("bot_schedule_off"));
        const std::string other = "{\"name\":\"Evening\",\"on\":true,\"when\":{\"kind\":\"times\",\"times\":[\"19:00\"]},\"what\":" + batch_body(1) + "}";
        ASSERT_EQ(d.post("bot-schedules", other).status, 200);                                                                  // (another time: another schedule)
        ASSERT_EQ(BotDoor::json(d.get("bot-schedules")).get("schedules").size(), size_t{2});
        // the same save a good while later is a second schedule too: the window is for the double click
        d.wall += 60;
        ASSERT_EQ(d.post("bot-schedules", other).status, 200);
        ASSERT_EQ(BotDoor::json(d.get("bot-schedules")).get("schedules").size(), size_t{3});
        // a change of the time keeps the next run when only the name changes, and moves it when the time changes
        const int64_t next = BotDoor::json(d.get("bot-schedules")).get("schedules").at(1).get("next").as_int_or(0);
        const std::string second_id = std::to_string(BotDoor::json(d.get("bot-schedules")).get("schedules").at(1).get("id").as_int_or(0));
        ASSERT_EQ(d.post("bot-schedules/" + second_id, "{\"name\":\"Evening 2\"}").status, 200);
        ASSERT_EQ(BotDoor::json(d.get("bot-schedules")).get("schedules").at(1).get("next").as_int_or(-1), next);
    } TEST_END();

    TEST_CASE("S3.281 The Log Is Asked For By Kind In The Query: A Hundred And Fifty Lines About The Matches Never Push The Decisions On Maps Out Of The Newest Lines, Nor The Other Way; A Kind That Is None Is Refused; The Door Is The Same") {
        BotDoor d("botadmin-kinds");
        const Actor who{kBotRefDave, "dave"};
        d.audit->record(who, kBotWall0 + 1, "map_approved", "Mossy Ridge", "");
        d.audit->record(who, kBotWall0 + 2, "map_declined", "Test 1", "Same as an existing map.");
        d.audit->record(who, kBotWall0 + 3, "map_taken_down", "Old Bridge", "");
        for (int i = 0; i < 250; ++i) d.audit->record(who, kBotWall0 + 10 + i, i % 2 == 0 ? "bot_started" : "bot_stopped", "TINY", "");
        const auto actions = [&](const std::string& tail) {
            std::vector<std::string> out;
            const ctl::HttpResponse r = d.get(tail);
            const ctl::JsonValue entries = BotDoor::json(r).get("entries");
            for (size_t i = 0; i < entries.size(); ++i) out.push_back(entries.at(i).get("action").str());
            return out;
        };
        const std::vector<std::string> maps = actions("audit?kind=maps");
        ASSERT_TRUE(maps == (std::vector<std::string>{"map_taken_down", "map_declined", "map_approved"}));                      // (newest first, and all three, though 250 lines of the other kind are newer)
        const std::vector<std::string> bots = actions("audit?kind=bots");
        ASSERT_EQ(bots.size(), size_t{200});
        for (const std::string& a : bots) ASSERT_TRUE(a.compare(0, 4, "bot_") == 0);
        const std::vector<std::string> all = actions("audit");
        ASSERT_TRUE(all.size() == 200 && all.back().compare(0, 4, "bot_") == 0);                                                  // (without a kind: the newest 200 of everything, as before)
        ASSERT_EQ(d.get("audit?kind=chat").status, 400);
        ASSERT_EQ(BotDoor::json(d.get("audit?kind=chat")).get("error").str(), std::string("bad_kind"));
        const ctl::HttpResponse closed = d.get("audit?kind=bots", "");
        ASSERT_TRUE(closed.status == 403 && closed.body == kBotClosed);
    } TEST_END();

    TEST_CASE("S3.282 A Match Of Computer Players Is Not Played When The Store Has No Room To Keep It This Hour: The Start Is Refused With The Reason (409 budget, The Number An Hour), A Batch Starts As Many As The Room Allows, And The Room Is Used By A Match That Was Kept, Not By One That Was Stopped At Once") {
        BotDoor d("botadmin-budget", ServerLimits(), true, true, 2);
        ASSERT_EQ(d.w.mgr.replay_store()->bot_saves_left(), size_t{2});
        const ctl::HttpResponse batch = d.post("bot-matches", batch_body(3, "TINY.LVL", "highest-score", "2"));
        ASSERT_EQ(batch.status, 200);
        const ctl::JsonValue answer = BotDoor::json(batch);
        ASSERT_TRUE(answer.get("started").as_int_or(0) == 2 && answer.get("asked").as_int_or(0) == 3 && answer.get("stopped_for").str() == "budget");
        const ctl::HttpResponse over = d.post("bot-matches", batch_body(1, "TINY.LVL", "highest-score", "2"));
        ASSERT_TRUE(over.status == 409 && BotDoor::json(over).get("error").str() == "budget" && BotDoor::json(over).get("per_hour").as_int_or(0) == 2);
        std::string code, reason;
        BotRefusal refusal = BotRefusal::None;
        ASSERT_FALSE(d.w.mgr.start_bot_match(d.w.now, BotMatchSpec(), code, reason, &refusal));
        ASSERT_TRUE(refusal == BotRefusal::Budget && reason.find("2 recordings") != std::string::npos);
        ASSERT_EQ(d.post("bot-matches/stop-all").status, 200);                                                                  // (stopped at once: nothing was kept, nothing was used)
        ASSERT_EQ(d.w.mgr.replay_store()->bot_saves_left(), size_t{2});
        ASSERT_EQ(d.post("bot-matches", batch_body(1, "TINY.LVL", "highest-score", "2")).status, 200);
        d.w.run(40000);                                                                                                         // (it plays 40 s of its 6 minutes)
        d.clock.now += 40;
        ASSERT_EQ(d.post("bot-matches/stop-all").status, 200);                                                                  // (stopped after 30 s: kept as a replay up to there)
        ASSERT_EQ(d.w.mgr.replay_store()->count(), size_t{1});
        ASSERT_EQ(d.w.mgr.replay_store()->bot_saves_left(), size_t{1});                                                         // (this hour's room is used by what was kept)
        ASSERT_EQ(d.w.mgr.replay_store()->bot_count(), size_t{1});
        ASSERT_EQ(BotDoor::json(d.post("bot-matches", batch_body(2, "TINY.LVL", "highest-score", "2"))).get("started").as_int_or(0), 1);
        d.clock.now += 3700;                                                                                                    // (the hour has passed)
        ASSERT_EQ(d.w.mgr.replay_store()->bot_saves_left(), size_t{2});
    } TEST_END();

    TEST_CASE("S3.283 What A Room Can Do With A Map Is Judged Once: The Lists Load Each Map File One Time (Not At Every Call, Every Start And Every Page), A File That Was Replaced Is Judged Again, A Map That Was Taken Down Is Never Answered From What Was Kept, And A File That Went Bad Is Not Played On Its Old Verdict") {
        BotDoor d("botadmin-verdicts");
        const std::vector<BotMapOption> first = d.w.mgr.bot_map_options(kBotLoadAll);
        const size_t loads = d.w.mgr.bot_map_loads();
        ASSERT_TRUE(loads >= first.size() && loads > 0);
        (void)d.w.mgr.bot_map_options(kBotLoadAll);
        (void)drawable(d.w.mgr, false);
        (void)drawable(d.w.mgr, true);
        ASSERT_EQ(d.w.mgr.bot_map_loads(), loads);                                                                              // (asked again: nothing is loaded)
        std::string code, reason;
        for (int i = 0; i < 10; ++i) {
            ASSERT_TRUE(d.w.mgr.start_bot_match(d.w.now, BotMatchSpec(), code, reason));
            d.w.mgr.stop_bot_matches(d.w.now);
            d.w.run(2000);
        }
        ASSERT_EQ(d.w.mgr.bot_map_loads(), loads);                                                                              // (ten starts from the lists: still none)
        BotMatchSpec players;
        players.map = kBotMapPlayers;
        ASSERT_TRUE(d.w.mgr.start_bot_match(d.w.now, players, code, reason) && d.w.mgr.bot_map_loads() == loads);
        d.w.mgr.stop_bot_matches(d.w.now);
        bool found = false;
        for (const BotMapOption& o : first) found = found || o.file == "Fight Island.lvl";
        ASSERT_TRUE(found);
        // the file is replaced by another one (a bigger map): judged again, once, and the new size is what the list says
        ApprovedMap bigger = made_up_fight_island("botadmin-verdicts-b", 170, 170);
        d.w.mgr.mutable_store().set_approved({bigger});
        const std::vector<BotMapOption> second = d.w.mgr.bot_map_options(kBotLoadAll);
        ASSERT_EQ(d.w.mgr.bot_map_loads(), loads + 1);
        uint32_t width = 0;
        for (const BotMapOption& o : second) width = o.file == "Fight Island.lvl" ? o.width : width;
        ASSERT_EQ(width, 170u);
        (void)d.w.mgr.bot_map_options(kBotLoadAll);
        ASSERT_EQ(d.w.mgr.bot_map_loads(), loads + 1);
        // the same name and path, the file rewritten as bytes that are no map: the old "can be played" is not trusted (the catalog still lists it, and says why a room cannot play it)
        { std::ofstream out(bigger.path, std::ios::binary | std::ios::trunc); out << "this is not a map of the game"; }
        bigger.size = 29;
        d.w.mgr.mutable_store().set_approved({bigger});
        bool still = false, playable = true;
        for (const BotMapOption& o : d.w.mgr.bot_map_options(kBotLoadAll)) {
            still = still || o.file == "Fight Island.lvl";
            if (o.file == "Fight Island.lvl") playable = o.playable || o.reason.find("cannot play") == std::string::npos;
        }
        ASSERT_TRUE(still && !playable);
        BotRefusal refusal = BotRefusal::None;
        BotMatchSpec named;
        named.map = "Fight Island.lvl";
        named.rules = BotRules::Kills187;
        ASSERT_FALSE(d.w.mgr.start_bot_match(d.w.now, named, code, reason, &refusal));
        ASSERT_TRUE(refusal == BotRefusal::NoMap);
        // the owner takes the map down: it is gone from the list and a named choice is refused, though the file is still on the disk and was judged to be good before
        ApprovedMap good = made_up_fight_island("botadmin-verdicts-c");
        d.w.mgr.mutable_store().set_approved({good});
        bool back = false;
        for (const BotMapOption& o : d.w.mgr.bot_map_options(kBotLoadAll)) back = back || (o.file == "Fight Island.lvl" && o.playable);
        ASSERT_TRUE(back);
        d.w.mgr.mutable_store().set_approved({});
        bool gone = true;
        for (const BotMapOption& o : d.w.mgr.bot_map_options(kBotLoadAll)) gone = gone && o.file != "Fight Island.lvl";
        ASSERT_TRUE(gone);
        ASSERT_FALSE(d.w.mgr.start_bot_match(d.w.now, named, code, reason, &refusal));
        ASSERT_TRUE(refusal == BotRefusal::NoMap);
        d.w.mgr.mutable_store().set_approved({good});                                                                            // (approved again: judged again, and playable)
        ASSERT_TRUE(d.w.mgr.start_bot_match(d.w.now, named, code, reason));
    } TEST_END();

    TEST_CASE("S3.284 A Random Pick Keeps To Maps Of 30 Minutes Or Less: A Map Of The Players That Plays Longer Is Never Drawn (From The Players' Maps, The Rotation Or The Original's) But A Choice That Names It Plays It; The List Gives Each Map's Minutes And Whether A Random Pick May Take It") {
        BotDoor d("botadmin-minutes");
        ApprovedMap longer = made_up_fight_island("botadmin-minutes-long", 60, 60, "Long Haul.lvl", 45);
        ApprovedMap shorter = made_up_fight_island("botadmin-minutes-short", 60, 60, "Short Hop.lvl", 12);
        d.w.mgr.mutable_store().set_approved({longer, shorter, d.w.mgr.store().approved_maps().empty() ? shorter : d.w.mgr.store().approved_maps()[0]});
        d.w.mgr.mutable_store().set_approved({longer, shorter});
        std::map<std::string, BotMapOption> by_file;
        for (const BotMapOption& o : d.w.mgr.bot_map_options(kBotLoadAll)) by_file[o.file] = o;
        ASSERT_TRUE(by_file.count("Long Haul.lvl") == 1 && by_file["Long Haul.lvl"].minutes == 45 && !by_file["Long Haul.lvl"].random);
        ASSERT_TRUE(by_file.count("Short Hop.lvl") == 1 && by_file["Short Hop.lvl"].minutes == 12 && by_file["Short Hop.lvl"].random);
        for (const char* file : {"TINY.LVL", "ISLANDS.LVL", "POPcOrN.lvl"}) ASSERT_TRUE(by_file[file].minutes > 0 && by_file[file].minutes <= kBotRandomMaxMinutes && by_file[file].random);
        const ctl::JsonValue options = BotDoor::json(d.get("bot-options"));
        ASSERT_EQ(options.get("random_max_minutes").as_int_or(0), 30);
        bool long_in_page = false;
        for (size_t i = 0; i < options.get("maps").size(); ++i) {
            const ctl::JsonValue& m = options.get("maps").at(i);
            if (m.get("file").str() == "Long Haul.lvl") long_in_page = m.get("minutes").as_int_or(0) == 45 && !m.get("random").as_bool_or(true);
        }
        ASSERT_TRUE(long_in_page);
        BotMatchSpec players;
        players.map = kBotMapPlayers;
        players.bots = 2;
        std::set<std::string> drawn;
        std::string code, reason;
        for (int i = 0; i < 40; ++i) {
            ASSERT_TRUE(d.w.mgr.start_bot_match(d.w.now, players, code, reason));
            for (const BotMatchInfo& m : d.w.mgr.bot_matches(d.w.now)) drawn.insert(m.map);
            d.w.mgr.stop_bot_matches(d.w.now);
            d.w.run(1200);
        }
        ASSERT_TRUE(drawn.count("Short Hop.lvl") == 1 && drawn.count("Long Haul.lvl") == 0 && drawn.size() == 1);
        BotMatchSpec named;
        named.map = "Long Haul.lvl";
        named.rules = BotRules::HighestScore;
        named.bots = 2;
        ASSERT_TRUE(d.w.mgr.start_bot_match(d.w.now, named, code, reason));                                                      // (named: played, and the room's own guard is longer than its minutes)
        ASSERT_EQ(d.w.mgr.bot_matches(d.w.now).size(), size_t{1});
        d.w.mgr.stop_bot_matches(d.w.now);
        d.w.run(2000);
        // only a long map of the players: a random pick of theirs has nothing to play
        d.w.mgr.mutable_store().set_approved({longer});
        BotRefusal refusal = BotRefusal::None;
        ASSERT_FALSE(d.w.mgr.start_bot_match(d.w.now, players, code, reason, &refusal));
        ASSERT_TRUE(refusal == BotRefusal::NoMap);
        // a list of the rotation may hold a long map on purpose: the list of 187 draws the players' map at 45 minutes as it does at 20 (the random picks of the original's and of the players' maps are the ones that skip it)
        ApprovedMap fight_long = made_up_fight_island("botadmin-minutes-fi", 60, 60, "Fight Island.lvl", 45);
        d.w.mgr.mutable_store().set_approved({fight_long});
        ASSERT_EQ(drawable(d.w.mgr, true), size_t{6});
        BotMatchSpec rotation;
        rotation.rules = BotRules::Kills187;
        rotation.bots = 2;
        std::set<std::string> from_list;
        for (int i = 0; i < 40; ++i) {
            ASSERT_TRUE(d.w.mgr.start_bot_match(d.w.now, rotation, code, reason));
            for (const BotMatchInfo& m : d.w.mgr.bot_matches(d.w.now)) from_list.insert(m.map);
            d.w.mgr.stop_bot_matches(d.w.now);
            d.w.run(1200);
        }
        ASSERT_TRUE(from_list.count("Fight Island.lvl") == 1 && from_list.size() == 6);
    } TEST_END();

}
