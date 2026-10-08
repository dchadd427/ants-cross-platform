// The tests of the replays that the server keeps (ants_server/replay_store.hpp, Room, RoomManager::enable_replays and the control calls GET /replays, GET and DELETE /replays/<file>): a whole match
// through the door is kept and plays out to the same state on a fresh engine, the people's typed names are in it (and a seat that was given none is the colour), a bot has its name, no address or
// room code is, short and unrecorded matches are not kept (and the status says why),
// a server that is stopped keeps the match that runs, a room that came back from a restart record does not record, the limits show in the status and the log, and the two doors (the secret's and the
// public one) answer as they should. The store's own rules (names, ages, sizes, a folder with other things in it) are test_replay_store.cpp. Included by test_server.cpp, which holds the harness
// (TEST_CASE, ASSERT_*, World, PWorld, Client) and calls run_replay_tests().
#pragma once

namespace {

constexpr int64_t kReplayT0 = 1791469929;          // 2026-10-08 14:32:09 UTC

// The store's clock, moved by the test
struct ReplayClock {
    int64_t now{kReplayT0};
};

ReplayConfig replay_config(const char* tag, ReplayClock* clock) {
    ReplayConfig c;
    c.dir = (fs::path(temp_dir_for(tag)) / "replays").string();
    c.game_version = "v0.0.0-test";
    c.build_id = "abc123";
    c.min_free_bytes = 0;                           // (the disk of the machine that runs the test is not the test's business)
    if (clock != nullptr) c.clock_s = [clock]() { return clock->now; };
    return c;
}

bool bytes_contain(const std::vector<uint8_t>& bytes, const std::string& text) {
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

// A stored file, read and decoded
struct StoredReplay {
    std::vector<uint8_t> bytes;
    replay::Replay rep;
    bool ok{false};
};

StoredReplay load_stored(const ReplayStore& store, const std::string& file) {
    StoredReplay s;
    std::string why;
    s.ok = store.read(file, s.bytes) && replay::decode(s.bytes.data(), s.bytes.size(), s.rep, why);
    return s;
}

// Does the file play out on a fresh engine (the map found in the maps folder) to every hash it holds, and its last one?
bool plays_out(const replay::Replay& rep, replay::Outcome& out) {
    assets::LevelData level;
    std::string why;
    if (!replay::load_map(rep.head, maps_dir(), level, why)) return false;
    out = replay::play(rep, level);
    return out.ran && out.ok;
}

// A match of two players on TINY that has run `play_ms` of play (after the start dialog), and is still running
void start_pair(World& w, const std::string& code, uint32_t play_ms, const char* a = "Ann", const char* b = "Bob") {
    w.connect(a, code);
    w.connect(b, code);
    w.run(1500 + kPre + play_ms);
}

// A small synthetic replay (what the store keeps): the same shape as the server's files, no match behind it. Seat 0 has a name, seat 1 has none (it is "Red" in the lists).
std::vector<uint8_t> synthetic_replay(uint32_t seed, uint32_t turns = 700, uint16_t rules = net::kProtocolVersion) {
    replay::Replay r;
    r.head.engine_rules = rules;
    r.head.game_version = "v0.0.0-test";
    r.head.build_id = "abc123";
    r.head.venue = "game server";
    r.head.map_name = "TINY.LVL";
    r.head.map_hash = 0x1122334455667788ull;
    r.head.seed = seed;
    r.head.roster = 0x03;
    r.head.names[0] = "Ann";
    r.complete = true;
    r.total_turns = turns;
    r.match_over = true;
    r.final_hash = 77;
    r.hashes.assign(turns / replay::kHashPeriodTurns, 0xABCDu);
    std::string error;
    return replay::encode(r, error);
}

ctl::JsonValue replay_json_of(const ctl::HttpResponse& r) {
    ctl::JsonValue v;
    std::string why;
    ctl::parse_json(r.body, v, &why);
    return v;
}

ctl::HttpResponse replay_call(RoomManager& mgr, const char* method, const std::string& path, const std::string& query = std::string(), const std::string& body = std::string()) {
    ctl::HttpRequest rq;
    rq.method = method;
    rq.path = path;
    rq.query = query;
    rq.body = body;
    return handle_control(mgr, rq, 1000);
}

ctl::HttpResponse public_call(const RoomManager& mgr, const char* method, const std::string& path, const std::string& query = std::string()) {
    ctl::HttpRequest rq;
    rq.method = method;
    rq.path = path;
    rq.query = query;
    return handle_public_replays(mgr, rq);
}

}  // namespace

void run_replay_tests() {
    TEST_CASE("S3.160 A Whole Match Is Kept On The Server When It Ends: One File Under A Name With The Map And The End Time, Listed By The Status, The Control Interface And The Store; It Plays Out On A Fresh Engine To Every Hash And To The State The Referee And The Players Ended In; The Names The People Typed Are In It, Seat By Seat, And Nothing Of The Room's Code Or Of Their Addresses") {
        ReplayClock clock;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("replay-whole", &clock), false, why));
        ASSERT_TRUE(w.mgr.create_room(spec_of("REC-1", 3), w.now).ok);
        ASSERT_TRUE(!w.status("REC-1").replay_kept && w.status("REC-1").replay_note.empty());           // (a match that may still be kept says nothing)
        Client& a = w.connect("Ann", "REC-1", 2);
        Client& b = w.connect("Bob", "REC-1");
        Client& c = w.connect("Cat", "REC-1", 3, {40, 20});
        w.run(1800);
        ASSERT_TRUE(w.status("REC-1").state == RoomState::Running);
        ASSERT_TRUE(!w.status("REC-1").replay_kept && w.status("REC-1").replay_note.empty() && w.mgr.replay_store()->count() == 0);       // (nothing is written while the match runs)
        for (int guard = 0; guard < 4000 && w.status("REC-1").state == RoomState::Running; ++guard) w.run(250);
        const RoomStatus s = w.status("REC-1");
        ASSERT_TRUE(s.state == RoomState::Finished);
        ASSERT_TRUE(s.replay_kept && s.replay_note.empty() && s.replay_bytes > 100);
        ASSERT_TRUE(ReplayStore::valid_file_name(s.replay_file) && s.replay_file.rfind("ants-TINY-20261008-143209Z", 0) == 0);
        const ReplayStore& store = *w.mgr.replay_store();
        ASSERT_TRUE(store.count() == 1 && store.total_bytes() == s.replay_bytes);
        const std::vector<ReplayEntry> list = store.list();
        ASSERT_TRUE(list.size() == 1 && list[0].file == s.replay_file && list[0].readable && list[0].finished && list[0].map == "TINY.LVL");
        // (a match that the rules ended has one turn more than the engine counts ticks: the call of tick() that finds the end does not count one)
        ASSERT_MSG(list[0].turns >= s.ticks && list[0].turns <= s.ticks + 2, "turns " + std::to_string(list[0].turns) + " ticks " + std::to_string(s.ticks));
        ASSERT_TRUE(list[0].game == "v0.0.0-test" && list[0].rules == net::kProtocolVersion);
        ASSERT_EQ(list[0].players, (std::vector<std::string>{"Green (Bob)", "Blue (Ann)", "Black (Cat)"}));      // seats 0, 2 and 3 (Ann asked for 2, Bob took the first free seat, Cat asked for 3): a colour each, with its player's name
        // the file: it is what the server says, and it is the whole match
        const StoredReplay f = load_stored(store, s.replay_file);
        ASSERT_TRUE(f.ok && f.rep.complete && f.rep.match_over);
        ASSERT_TRUE(f.rep.head.venue == "game server" && f.rep.head.map_name == "TINY.LVL" && f.rep.head.roster == 0x0D && !f.rep.head.fog && f.rep.head.seed == 4242);
        ASSERT_TRUE(f.rep.head.game_version == "v0.0.0-test" && f.rep.head.build_id == "abc123" && f.rep.head.recorder_seat == replay::kNoSeat);
        ASSERT_EQ(f.rep.total_turns, list[0].turns);
        ASSERT_TRUE(f.rep.commands.size() > 5);
        ASSERT_EQ(f.rep.head.names, (std::array<std::string, sim::MAX_PLAYERS>{{"Bob", "", "Ann", "Cat"}}));     // (by seat: seat 1 does not play and has none)
        for (const char* typed : {"Ann", "Bob", "Cat"}) ASSERT_TRUE(bytes_contain(f.bytes, typed));
        for (const char* never : {"REC-1", "127.0.0.1"}) ASSERT_FALSE(bytes_contain(f.bytes, never));         // (the room's code, and the address that its players came from)
        std::string again;
        ASSERT_TRUE(replay::encode(f.rep, again) == f.bytes);                                            // (the reader gives back what the writer wrote, the names with it)
        ASSERT_EQ(f.rep.final_hash, s.referee_hash);
        ASSERT_EQ(f.rep.final_hash, a.sim.state_hash().total);                                           // the state the players ended in
        ASSERT_TRUE(a.sim.state_hash().total == b.sim.state_hash().total && b.sim.state_hash().total == c.sim.state_hash().total);
        replay::Outcome played;
        ASSERT_TRUE(plays_out(f.rep, played));
        ASSERT_TRUE(played.complete && played.match_over && played.turns == f.rep.total_turns && played.hashes_checked > 5 && played.hash == s.referee_hash);
        // the status as the control interface tells it
        const ctl::JsonValue j = status_to_json(s);
        ASSERT_TRUE(j.get("replay").is_object() && j.get("replay").get("kept").as_bool_or(false) && j.get("replay").get("file").str() == s.replay_file);
        ASSERT_TRUE(j.get("replay").get("bytes").as_int_or(0) == static_cast<int64_t>(s.replay_bytes) && j.get("replay").get("note").str().empty());
        // in the list of rooms (GET /rooms) a kept match has its replay too, and the room's own call (GET /rooms/<code>) is the same as the status above
        {
            const ctl::JsonValue rooms_json = replay_json_of(replay_call(w.mgr, "GET", "/rooms"));
            ASSERT_TRUE(rooms_json.get("rooms").size() == 1 && rooms_json.get("rooms").at(0).get("code").str() == "REC-1");
            const ctl::JsonValue& entry = rooms_json.get("rooms").at(0);
            ASSERT_TRUE(entry.get("replay").is_object() && entry.get("replay").get("kept").as_bool_or(false) && entry.get("replay").get("file").str() == s.replay_file);
            ASSERT_TRUE(entry.get("replay").get("bytes").as_int_or(0) == static_cast<int64_t>(s.replay_bytes));
            ASSERT_EQ(ctl::to_json(replay_json_of(replay_call(w.mgr, "GET", "/rooms/REC-1")).get("replay")), ctl::to_json(j.get("replay")));
        }
        // the log does not say anything about a match that was kept as it should
        ASSERT_TRUE(w.mgr.take_notices().size() == 1);                                                   // (what the store found when it opened: no files)
    } TEST_END();

    TEST_CASE("S3.161 A Computer Player Keeps Its Name In The File (\"Bot (Medium)\") Next To The People's; A Match That The Owner Closes After 30 Seconds Of Play Is Kept (Not Finished) And Plays Out To The Same Hashes") {
        ReplayClock clock;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("replay-bot", &clock), false, why));
        RoomSpec spec = spec_of("REC-BOT", 3);
        spec.bots = {ai::BotSpec{2, "standard", ai::Level::Medium}};
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        start_pair(w, "REC-BOT", 38000);
        RoomStatus s = w.status("REC-BOT");
        ASSERT_TRUE(s.state == RoomState::Running && s.turns >= 600 && s.bot_controller);
        ASSERT_TRUE(w.mgr.close_room("REC-BOT", w.now));
        s = w.status("REC-BOT");
        ASSERT_TRUE(s.state == RoomState::Failed && s.reason == "closed by the owner");
        ASSERT_TRUE(s.replay_kept && s.replay_note.empty() && ReplayStore::valid_file_name(s.replay_file));
        const ReplayStore& store = *w.mgr.replay_store();
        const StoredReplay f = load_stored(store, s.replay_file);
        ASSERT_TRUE(f.ok && f.rep.complete && !f.rep.match_over && f.rep.head.roster == 0x07 && f.rep.total_turns >= 600);
        ASSERT_EQ(f.rep.head.names, (std::array<std::string, sim::MAX_PLAYERS>{{"Ann", "Bob", "Bot (Medium)", ""}}));
        for (const char* kept : {"Ann", "Bob", "Bot (Medium)"}) ASSERT_TRUE(bytes_contain(f.bytes, kept));
        ASSERT_FALSE(bytes_contain(f.bytes, "REC-BOT"));
        const std::vector<ReplayEntry> list = store.list();
        ASSERT_TRUE(list.size() == 1 && !list[0].finished);
        ASSERT_EQ(list[0].players, (std::vector<std::string>{"Green (Ann)", "Red (Bob)", "Blue (Bot (Medium))"}));
        replay::Outcome played;
        ASSERT_TRUE(plays_out(f.rep, played));
        ASSERT_TRUE(played.complete && !played.match_over && played.turns == f.rep.total_turns && played.hashes_checked >= 5);
    } TEST_END();

    TEST_CASE("S3.162 A Match That Ran Less Than 30 Seconds Is Not Kept Whatever Ended It (Closed, Or Quit So That The Rules End It), And A Room That Never Began Has No Match: The Status Says So, Nothing Is Written; A Match Of Exactly The Length That Counts Is Kept") {
        ReplayClock clock;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("replay-short", &clock), false, why));
        ASSERT_TRUE(w.mgr.create_room(spec_of("SHORT-1", 2), w.now).ok);
        start_pair(w, "SHORT-1", 8000);
        RoomStatus s = w.status("SHORT-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.turns > 100 && s.turns < 600);
        ASSERT_TRUE(w.mgr.close_room("SHORT-1", w.now));
        s = w.status("SHORT-1");
        ASSERT_TRUE(!s.replay_kept && s.replay_file.empty() && s.replay_note.find("ran less than 30 seconds") != std::string::npos);
        ASSERT_EQ(status_to_json(s).get("replay").get("kept").as_bool_or(true), false);
        ASSERT_TRUE(status_to_json(s).get("replay").get("note").str().find("30 seconds") != std::string::npos);
        // a Quit ends a match of two at once, by the rules: one that is quit after a few seconds is over and is still not kept (a loop of starts and Quits must not fill the store)
        ASSERT_TRUE(w.mgr.create_room(spec_of("QUIT-1", 2), w.now).ok);
        Client& leaver = w.connect("Fay", "QUIT-1");
        w.connect("Gus", "QUIT-1");
        for (int guard = 0; guard < 600 && !(w.status("QUIT-1").state == RoomState::Running && w.status("QUIT-1").ticks > 20); ++guard) w.run(100);
        ASSERT_TRUE(w.status("QUIT-1").state == RoomState::Running && w.status("QUIT-1").ticks > 20);
        sim::Command quit;
        quit.type = sim::CommandType::Quit;
        quit.issuer = leaver.lobby->my_seat();
        ASSERT_TRUE(leaver.session->submit(quit));
        for (int guard = 0; guard < 400 && w.status("QUIT-1").state != RoomState::Finished; ++guard) w.run(100);
        s = w.status("QUIT-1");
        ASSERT_MSG(s.state == RoomState::Finished && s.ticks > 20 && s.ticks < 600, "state " + std::string(room_state_name(s.state)) + " ticks " + std::to_string(s.ticks));
        ASSERT_TRUE(!s.replay_kept && s.replay_file.empty() && s.replay_note.find("ran less than 30 seconds") != std::string::npos);
        ASSERT_EQ(w.mgr.replay_store()->count(), size_t{0});
        // a room that never started
        ASSERT_TRUE(w.mgr.create_room(spec_of("NEVER-1", 2), w.now).ok);
        w.connect("Cy", "NEVER-1");
        w.run(500);
        ASSERT_TRUE(w.mgr.close_room("NEVER-1", w.now));
        s = w.status("NEVER-1");
        ASSERT_TRUE(!s.replay_kept && s.replay_note == "no match was played");
        ASSERT_EQ(w.mgr.replay_store()->count(), size_t{0});
        ASSERT_TRUE(fs::is_empty(w.mgr.replay_store()->config().dir));
        // 600 turns are 30 seconds: a match that is left after its 599th turn is not kept, after its 600th it is
        ASSERT_TRUE(w.mgr.create_room(spec_of("EDGE-A", 2), w.now).ok && w.mgr.create_room(spec_of("EDGE-B", 2), w.now).ok);
        for (const char* code : {"EDGE-A", "EDGE-B"}) {
            w.connect("Di", code);
            w.connect("Ed", code);
        }
        for (int guard = 0; guard < 6000 && w.status("EDGE-A").ticks < 599; ++guard) w.run(10);
        s = w.status("EDGE-A");
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks == 599);
        ASSERT_TRUE(w.mgr.close_room("EDGE-A", w.now));
        ASSERT_TRUE(!w.status("EDGE-A").replay_kept && w.status("EDGE-A").replay_note.find("ran less than 30 seconds") != std::string::npos);
        for (int guard = 0; guard < 100 && w.status("EDGE-B").ticks < 600; ++guard) w.run(10);
        s = w.status("EDGE-B");
        ASSERT_TRUE(s.state == RoomState::Running && s.ticks >= 600 && s.ticks < 610);
        ASSERT_TRUE(w.mgr.close_room("EDGE-B", w.now));
        ASSERT_TRUE(w.status("EDGE-B").replay_kept);
        ASSERT_EQ(w.mgr.replay_store()->count(), size_t{1});
    } TEST_END();

    TEST_CASE("S3.163 A Room Made With \"record\": false Keeps Nothing, A Server That Keeps No Replays Keeps Nothing: The Status Says Which; The Control Interface Takes \"record\" As A Boolean Only") {
        ReplayClock clock;
        {
            World w;
            std::string why;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("replay-norec", &clock), false, why));
            RoomSpec spec = spec_of("NOREC-1", 2);
            spec.record_replay = false;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            ASSERT_TRUE(w.status("NOREC-1").replay_note.find("\"record\": false") != std::string::npos);
            start_pair(w, "NOREC-1", 38000);
            ASSERT_TRUE(w.status("NOREC-1").state == RoomState::Running && w.status("NOREC-1").turns >= 600);
            ASSERT_TRUE(w.mgr.close_room("NOREC-1", w.now));
            const RoomStatus s = w.status("NOREC-1");
            ASSERT_TRUE(!s.replay_kept && s.replay_note.find("\"record\": false") != std::string::npos);
            ASSERT_EQ(w.mgr.replay_store()->count(), size_t{0});
            // through the control interface
            ctl::HttpResponse r = replay_call(w.mgr, "POST", "/rooms", "", R"({"map":"TINY.LVL","players":2,"code":"NOREC-2","record":false})");
            ASSERT_EQ(r.status, 201);
            ASSERT_TRUE(replay_json_of(r).get("replay").get("note").str().find("record") != std::string::npos);
            r = replay_call(w.mgr, "POST", "/rooms", "", R"({"map":"TINY.LVL","players":2,"code":"NOREC-3","record":true})");
            ASSERT_TRUE(r.status == 201 && replay_json_of(r).get("replay").get("note").str().empty());
            for (const char* bad : {R"({"map":"TINY.LVL","record":"no"})", R"({"map":"TINY.LVL","record":0})", R"({"map":"TINY.LVL","record":null})"}) {
                r = replay_call(w.mgr, "POST", "/rooms", "", bad);
                ASSERT_TRUE(r.status == 400 && replay_json_of(r).get("error").str().find("record") != std::string::npos);
            }
        }
        {
            World w;                                                                                    // a server with no store
            ASSERT_TRUE(w.mgr.replay_store() == nullptr);
            ASSERT_TRUE(w.mgr.create_room(spec_of("NONE-1", 2), w.now).ok);
            ASSERT_EQ(w.status("NONE-1").replay_note, std::string("this server keeps no replays"));
            {   // the server's own reason comes first: neither "record": false nor a demo room (one that a visitor's create block made) hides it
                RoomSpec off = spec_of("NONE-2", 2);
                off.record_replay = false;
                ASSERT_TRUE(w.mgr.create_room(off, w.now).ok);
                ASSERT_EQ(w.status("NONE-2").replay_note, std::string("this server keeps no replays"));
                RoomSpec visitors = spec_of("nonepub1", 2);
                visitors.public_room = true;
                ASSERT_TRUE(w.mgr.create_room(visitors, w.now).ok);
                ASSERT_EQ(w.status("nonepub1").replay_note, std::string("this server keeps no replays"));
            }
            start_pair(w, "NONE-1", 38000);
            ASSERT_TRUE(w.mgr.close_room("NONE-1", w.now));
            const RoomStatus s = w.status("NONE-1");
            ASSERT_TRUE(!s.replay_kept && s.replay_note == "this server keeps no replays");
            const ctl::HttpResponse r = replay_call(w.mgr, "GET", "/replays");
            ASSERT_EQ(r.status, 200);
            const ctl::JsonValue j = replay_json_of(r);
            ASSERT_TRUE(!j.get("enabled").as_bool_or(true) && j.get("replays").size() == 0 && j.get("count").is_null());
            ASSERT_EQ(replay_call(w.mgr, "GET", "/replays/ants-TINY-20261008-143209Z.antsrep").status, 404);
            ASSERT_EQ(replay_call(w.mgr, "DELETE", "/replays/ants-TINY-20261008-143209Z.antsrep").status, 404);
            ASSERT_EQ(public_call(w.mgr, "GET", "/replays").status, 404);
        }
    } TEST_END();

    TEST_CASE("S3.164 The Rooms That A Visitor's Create Block Makes (The Games Of The Front Page: The Demo Rooms) Are Kept Only When The Server Is Started With --replay-demo; The Rooms Of The Control Interface Are Kept Either Way, Whatever Their Code Begins With") {
        ReplayClock clock;
        ServerLimits limits;
        limits.demo_rooms = 2;
        limits.demo_map = "TINY.LVL";
        limits.demo_wait_ms = 60000;
        for (const bool include_demo : {false, true}) {
            World w(limits);
            std::string why;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config(include_demo ? "replay-demo-on" : "replay-demo-off", &clock), include_demo, why));
            w.connect_creating("Ann", "recpub1", block_of("", 2));                                        // (the front page's way: Ann's Hello carries the block that makes the room)
            w.connect("Bob", "recpub1");
            w.run(1500 + kPre + 38000);
            RoomStatus s = w.status("recpub1");
            ASSERT_TRUE(s.public_room && s.state == RoomState::Running && s.turns >= 600);
            if (include_demo) ASSERT_TRUE(s.replay_note.empty());
            else ASSERT_TRUE(s.replay_note.find("demo rooms") != std::string::npos && s.replay_note.find("--replay-demo") != std::string::npos);
            ASSERT_TRUE(w.mgr.close_room("recpub1", w.now));
            s = w.status("recpub1");
            ASSERT_EQ(s.replay_kept, include_demo);
            ASSERT_EQ(w.mgr.replay_store()->count(), include_demo ? size_t{1} : size_t{0});
            // a room of the control interface, with the same server: kept either way
            clock.now += 10;
            ASSERT_TRUE(w.mgr.create_room(spec_of("CTL-REC", 2), w.now).ok);
            ASSERT_FALSE(w.status("CTL-REC").public_room);
            start_pair(w, "CTL-REC", 38000, "Cy", "Di");
            ASSERT_TRUE(w.mgr.close_room("CTL-REC", w.now));
            ASSERT_TRUE(w.status("CTL-REC").replay_kept);
            ASSERT_EQ(w.mgr.replay_store()->count(), include_demo ? size_t{2} : size_t{1});
            // a code that begins with "demo-" is only a name since protocol 15 (it was the sign of a demo room before): a room of the control interface with such a code is an ordinary room, kept either way
            clock.now += 10;
            ASSERT_TRUE(w.mgr.create_room(spec_of("demo-ctl", 2), w.now).ok);
            ASSERT_TRUE(w.status("demo-ctl").replay_note.empty());
            start_pair(w, "demo-ctl", 38000, "Ed", "Flo");
            ASSERT_TRUE(w.mgr.close_room("demo-ctl", w.now));
            ASSERT_TRUE(w.status("demo-ctl").replay_kept);
            ASSERT_EQ(w.mgr.replay_store()->count(), include_demo ? size_t{3} : size_t{2});
        }
    } TEST_END();

    TEST_CASE("S3.165 A Server That Is Told To Stop Keeps The Match That Runs (A Room Without A Restart Record Is Closed, And Its Match Is Kept When It Ran 30 Seconds); A Server That Keeps Restart Records Leaves The Match For Its Next Start") {
        ReplayClock clock;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("replay-stop", &clock), false, why));
        ASSERT_TRUE(w.mgr.create_room(spec_of("STOP-1", 2), w.now).ok);
        start_pair(w, "STOP-1", 38000);
        ASSERT_TRUE(w.status("STOP-1").state == RoomState::Running && w.status("STOP-1").turns >= 600);
        ASSERT_EQ(w.mgr.shutdown(w.now), size_t{0});                                                     // (no restart record: nothing is left for a next start)
        const RoomStatus s = w.status("STOP-1");
        ASSERT_TRUE(s.state == RoomState::Failed && s.replay_kept);
        const StoredReplay f = load_stored(*w.mgr.replay_store(), s.replay_file);
        ASSERT_TRUE(f.ok && f.rep.complete && !f.rep.match_over);
        replay::Outcome played;
        ASSERT_TRUE(plays_out(f.rep, played) && played.turns == f.rep.total_turns);
    } TEST_END();

    TEST_CASE("S3.166 A Match That Was Brought Back After A Restart Of The Server Is Not Kept (The Part Before The Restart Was Not Seen): The File Of A Server That Is Stopped Is Not Made For A Room That Stays In Its Record, And The Restored Room Says Why It Records Nothing") {
        ReplayClock clock;
        PWorld w("replay-restart");
        const ReplayConfig config = replay_config("replay-restart-folder", &clock);
        w.configure = [&](RoomManager& m) {
            std::string why;
            if (!m.enable_replays(config, false, why)) throw std::runtime_error("enable_replays: " + why);
        };
        w.start_server(500);
        std::vector<RClient*> m = play_room(w, held_spec("RST-1", 2), 36000);
        RoomStatus s = w.status("RST-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.turns >= 600 && !s.restored && s.replay_note.empty() && s.record_kept);
        w.stop_server(true);                                                                              // (SIGTERM: the record is made durable and stays; the room is not closed)
        ASSERT_EQ(w.kept_at_stop, size_t{1});
        {
            ReplayStore after(config);
            std::string why;
            ASSERT_TRUE(after.prepare(why));
            ASSERT_EQ(after.count(), size_t{0});                                                          // (the room waits in its record: no file of its match yet)
        }
        w.start_server(500);
        s = w.status("RST-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.restored);
        ASSERT_TRUE(!s.replay_kept && s.replay_note.find("restart record") != std::string::npos);
        {   // the list of rooms leaves out the replay of a room that is not kept (a thousand restored rooms are ONE answer of at most 1 MiB: the restore check of tests/scripts/test_ants_server.sh), the room's own call has it
            const ctl::JsonValue list = replay_json_of(replay_call(*w.mgr, "GET", "/rooms"));
            ASSERT_TRUE(list.get("rooms").size() == 1 && list.get("rooms").at(0).get("code").str() == "RST-1" && !list.get("rooms").at(0).has("replay"));
            ASSERT_TRUE(replay_json_of(replay_call(*w.mgr, "GET", "/rooms/RST-1")).get("replay").get("note").str().find("restart record") != std::string::npos);
            ASSERT_TRUE(ctl::to_json(list.get("rooms").at(0)).size() * 1000 < ctl::HttpServer::kMaxResponseBytes);       // (and the thousand of the script fit)
        }
        ASSERT_TRUE(w.until([&]() { return !w.status("RST-1").paused; }, 90000));
        w.play_to_the_end("RST-1");
        const RoomStatus end = w.status("RST-1");
        ASSERT_TRUE(end.state == RoomState::Finished && end.restored);
        ASSERT_TRUE(!end.replay_kept && end.replay_file.empty() && end.replay_note.find("restart record") != std::string::npos);
        ASSERT_EQ(w.mgr->replay_store()->count(), size_t{0});
        ASSERT_TRUE(fs::is_empty(config.dir));
        (void)m;
        {   // a record that this server cannot bring back (it was written under another network protocol) makes a failed room: it says that no match of it was recorded, and the store holds nothing
            PWorld r("replay-refused");
            const ReplayConfig refused_config = replay_config("replay-refused-folder", &clock);
            r.configure = [&](RoomManager& mgr) {
                std::string why;
                if (!mgr.enable_replays(refused_config, false, why)) throw std::runtime_error("enable_replays: " + why);
            };
            r.start_server(500);
            std::vector<RClient*> p = play_room(r, held_spec("RST-2", 2), 8000);
            for (RClient* c : p) c->reconnects = false;
            r.stop_server(false);                                                                          // (a crash: the record stays where it is)
            r.restart.identity.protocol = static_cast<uint16_t>(net::kProtocolVersion + 1);
            r.start_server(500);
            ASSERT_TRUE(r.report.items.size() == 1 && r.report.items[0].outcome == RestoreItem::Outcome::Ended);
            const RoomStatus f = r.status("RST-2");
            ASSERT_TRUE(f.state == RoomState::Failed && !f.restored && !f.replay_kept && f.replay_file.empty());
            ASSERT_TRUE(f.replay_note.find("not brought back from its restart record") != std::string::npos);
            ASSERT_TRUE(replay_json_of(replay_call(*r.mgr, "GET", "/rooms/RST-2")).get("replay").get("note").str().find("not brought back") != std::string::npos);
            ASSERT_EQ(r.mgr->replay_store()->count(), size_t{0});
        }
    } TEST_END();

    TEST_CASE("S3.167 The Limits Show Where They Are Met: A Match That The Hour's Limit Refuses Is Not Kept, Its Status Says Why And The Server's Log Has The Line Once; The Next Hour Keeps Matches Again") {
        ReplayClock clock;
        World w;
        std::string why;
        ReplayConfig config = replay_config("replay-limit", &clock);
        config.max_saves_per_hour = 1;
        ASSERT_TRUE(w.mgr.enable_replays(config, false, why));
        for (int i = 0; i < 3; ++i) {
            const std::string code = "LIM-" + std::to_string(i);
            ASSERT_TRUE(w.mgr.create_room(spec_of(code, 2), w.now).ok);
            start_pair(w, code, 38000, "Ann", "Bob");
            ASSERT_TRUE(w.mgr.close_room(code, w.now));
            clock.now += i == 1 ? 3600 : 10;                                                               // (the third match is an hour after the first)
            const RoomStatus s = w.status(code);
            if (i == 1) ASSERT_TRUE(!s.replay_kept && s.replay_note.find("1 kept matches an hour") != std::string::npos);
            else ASSERT_TRUE(s.replay_kept);
        }
        ASSERT_EQ(w.mgr.replay_store()->count(), size_t{2});
        const std::vector<std::string> notices = w.mgr.take_notices();
        size_t told = 0;
        for (const std::string& n : notices) told += n.find("a match was not kept") != std::string::npos ? 1u : 0u;
        ASSERT_EQ(told, size_t{1});
        // the server's loop (RoomManager::update) is what makes the age limit work: two days later the files are past their 30 days
        clock.now += 31ll * 86400;
        w.run(100);
        ASSERT_EQ(w.mgr.replay_store()->count(), size_t{0});
        size_t purged = 0;
        for (const std::string& n : w.mgr.take_notices()) purged += n.find("2 file(s) deleted") != std::string::npos ? 1u : 0u;
        ASSERT_EQ(purged, size_t{1});
    } TEST_END();

    TEST_CASE("S3.168 The Control Interface Lists, Gives And Deletes The Files: The List Is The Newest First With What The Head Says (Colours With The Names That Were Typed), \"limit\" Is 1 To 1000, A File Is Given Byte For Byte, A Name That Is No File Of The Store Is 404 Whatever It Holds, DELETE Takes The File Away") {
        ReplayClock clock;
        World w;
        std::string why;
        std::string stuck;                                                                                      // the one file that the "disk" will not let go (the test sets it)
        ReplayConfig routes_config = replay_config("replay-routes", &clock);
        routes_config.remove_file = [&stuck](const std::string& path, std::error_code& ec) {
            if (!stuck.empty() && fs::path(path).filename().string() == stuck) ec = std::make_error_code(std::errc::permission_denied);
            else fs::remove(path, ec);
        };
        ASSERT_TRUE(w.mgr.enable_replays(routes_config, false, why));
        std::vector<std::string> files;
        for (int i = 0; i < 2; ++i) {
            const std::string code = "ROUTE-" + std::to_string(i);
            ASSERT_TRUE(w.mgr.create_room(spec_of(code, 2), w.now).ok);
            start_pair(w, code, 38000, "Ann", "Bob");
            ASSERT_TRUE(w.mgr.close_room(code, w.now));
            ASSERT_TRUE(w.status(code).replay_kept);
            files.push_back(w.status(code).replay_file);
            clock.now += 60;
        }
        ASSERT_TRUE(files.size() == 2 && files[0] != files[1]);
        ReplayStore& store = *w.mgr.replay_store();
        // the list
        ctl::HttpResponse r = replay_call(w.mgr, "GET", "/replays");
        ASSERT_TRUE(r.status == 200 && r.content_type == "application/json");
        ctl::JsonValue j = replay_json_of(r);
        ASSERT_TRUE(j.get("enabled").as_bool_or(false) && j.get("count").as_int_or(0) == 2 && j.get("bytes").as_int_or(0) == static_cast<int64_t>(store.total_bytes()));
        ASSERT_TRUE(j.get("keep_days").as_int_or(0) == 30 && j.get("max_bytes").as_int_or(0) == 100 * 1024 * 1024 && j.get("replays").size() == 2);
        const ctl::JsonValue& first = j.get("replays").at(0);
        ASSERT_TRUE(first.get("file").str() == files[1] && j.get("replays").at(1).get("file").str() == files[0]);           // newest first
        ASSERT_TRUE(first.get("readable").as_bool_or(false) && first.get("map").str() == "TINY.LVL" && first.get("players").size() == 2 && first.get("players").at(0).str() == "Green (Ann)" && first.get("players").at(1).str() == "Red (Bob)");
        ASSERT_TRUE(first.get("turns").as_int_or(0) >= 600 && first.get("seconds").as_int_or(0) == first.get("turns").as_int_or(0) / 20 && !first.get("finished").as_bool_or(true));
        ASSERT_TRUE(first.get("game").str() == "v0.0.0-test" && first.get("rules").as_int_or(0) == net::kProtocolVersion && first.get("ended").as_int_or(0) == kReplayT0 + 60);
        ASSERT_TRUE(first.get("bytes").as_int_or(0) == static_cast<int64_t>(store.find(files[1])->bytes));
        for (const char* typed : {"Ann", "Bob"}) ASSERT_TRUE(r.body.find(typed) != std::string::npos);
        for (const char* never : {"ROUTE", "127.0.0.1"}) ASSERT_TRUE(r.body.find(never) == std::string::npos);        // (the names are in the list, the room's code and the address are not)
        // limit
        r = replay_call(w.mgr, "GET", "/replays", "limit=1");
        ASSERT_TRUE(r.status == 200 && replay_json_of(r).get("replays").size() == 1 && replay_json_of(r).get("count").as_int_or(0) == 2 && replay_json_of(r).get("replays").at(0).get("file").str() == files[1]);
        for (const char* ok : {"limit=2", "limit=1000", "limit=0001"}) ASSERT_EQ(replay_call(w.mgr, "GET", "/replays", ok).status, 200);
        for (const char* bad : {"limit=0", "limit=1001", "limit=", "limit=-1", "limit=1x", "limit=99999", "limit=1&x=2", "x=1", "limit=1,2", "LIMIT=1", "limit= 1"}) ASSERT_EQ(replay_call(w.mgr, "GET", "/replays", bad).status, 400);
        ASSERT_EQ(replay_call(w.mgr, "POST", "/replays").status, 405);
        ASSERT_EQ(replay_call(w.mgr, "DELETE", "/replays").status, 405);
        ASSERT_EQ(replay_call(w.mgr, "PUT", "/replays").status, 405);
        // a file, byte for byte
        std::vector<uint8_t> on_disk;
        ASSERT_TRUE(store.read(files[0], on_disk));
        r = replay_call(w.mgr, "GET", "/replays/" + files[0]);
        ASSERT_TRUE(r.status == 200 && r.content_type == "application/octet-stream" && r.body.size() == on_disk.size());
        ASSERT_TRUE(std::equal(on_disk.begin(), on_disk.end(), r.body.begin(), [](uint8_t x, char y) { return x == static_cast<uint8_t>(y); }));
        // names that are no file of the store
        write_bytes(fs::path(store.config().dir) / "notes.txt", 10, 'n');
        write_bytes(fs::path(store.config().dir).parent_path() / "outside.antsrep", 10, 'o');
        for (const char* bad : {"/replays/", "/replays/notes.txt", "/replays/..", "/replays/../outside.antsrep", "/replays/%2e%2e/outside.antsrep", "/replays/ants-TINY-20261008-143210Z.antsrep",
                                "/replays/ants-TINY-20261008-143209Z.antsrep/", "/replays//ants-TINY-20261008-143209Z.antsrep", "/replays/a/b"}) {
            ASSERT_EQ(replay_call(w.mgr, "GET", bad).status, 404);
            ASSERT_EQ(replay_call(w.mgr, "DELETE", bad).status, 404);
        }
        ASSERT_EQ(replay_call(w.mgr, "GET", "/replays/" + files[0], "x=1").status, 404);                      // (no query on a file)
        ASSERT_EQ(replay_call(w.mgr, "PUT", "/replays/" + files[0]).status, 405);
        ASSERT_EQ(replay_call(w.mgr, "POST", "/replays/" + files[0]).status, 405);
        ASSERT_EQ(replay_call(w.mgr, "GET", "/replaysx").status, 404);
        ASSERT_TRUE(fs::exists(fs::path(store.config().dir) / "notes.txt") && fs::exists(fs::path(store.config().dir).parent_path() / "outside.antsrep"));
        // delete
        r = replay_call(w.mgr, "DELETE", "/replays/" + files[0]);
        ASSERT_TRUE(r.status == 200 && replay_json_of(r).get("deleted").str() == files[0]);
        ASSERT_TRUE(!fs::exists(fs::path(store.config().dir) / files[0]) && store.count() == 1);
        ASSERT_EQ(replay_call(w.mgr, "GET", "/replays/" + files[0]).status, 404);
        ASSERT_EQ(replay_call(w.mgr, "DELETE", "/replays/" + files[0]).status, 404);
        ASSERT_EQ(replay_json_of(replay_call(w.mgr, "GET", "/replays")).get("count").as_int_or(0), 1);
        // a file that the disk will not let go: DELETE says 500, the file stays on the disk, in the list and given out; once the disk allows it, it goes
        stuck = files[1];
        r = replay_call(w.mgr, "DELETE", "/replays/" + files[1]);
        ASSERT_TRUE(r.status == 500 && replay_json_of(r).get("error").str().find("could not be deleted") != std::string::npos);
        ASSERT_TRUE(fs::exists(fs::path(store.config().dir) / files[1]) && store.count() == 1);
        ASSERT_TRUE(replay_call(w.mgr, "GET", "/replays/" + files[1]).status == 200 && replay_json_of(replay_call(w.mgr, "GET", "/replays")).get("count").as_int_or(0) == 1);
        stuck.clear();
        ASSERT_EQ(replay_call(w.mgr, "DELETE", "/replays/" + files[1]).status, 200);
        ASSERT_TRUE(store.count() == 0 && !fs::exists(fs::path(store.config().dir) / files[1]));
    } TEST_END();

    TEST_CASE("S3.169 The Public Door Answers Two Calls And Nothing Else: The List (The Newest 200 Of The Files That This Build Can Read, With The Players' Names) And A File; Every Other Method, Path Or Query Is 404; A File That Cannot Be Read Is Not Listed Or Given, Though The Control Interface Gives It To Its Owner") {
        ReplayClock clock;
        RoomManager mgr{MapStore(maps_dir())};
        std::string why;
        ReplayConfig config = replay_config("replay-public", &clock);
        fs::create_directories(config.dir);
        const std::string junk = "ants-JUNK-20261008-160000Z.antsrep";                                      // (dated in the middle of the saves below: among the newest 200)
        write_bytes(fs::path(config.dir) / junk, 40, 'j');                                                    // a file of the store's own name that no build can read
        ASSERT_TRUE(mgr.enable_replays(config, false, why));
        ASSERT_EQ(mgr.replay_store()->count(), size_t{1});
        std::vector<std::string> names;
        for (uint32_t i = 0; i < 205; ++i) {
            clock.now += 60;
            const ReplaySave saved = mgr.replay_store()->save(synthetic_replay(i + 1));
            ASSERT_TRUE(saved.kept);
            names.push_back(saved.file);
        }
        ASSERT_EQ(mgr.replay_store()->count(), size_t{206});
        ctl::HttpResponse r = public_call(mgr, "GET", "/replays");
        ASSERT_TRUE(r.status == 200 && r.content_type == "application/json");
        ctl::JsonValue j = replay_json_of(r);
        ASSERT_TRUE(j.get("replays").size() == 200 && j.get("count").as_int_or(0) == 205 && j.get("keep_days").as_int_or(0) == 30);
        ASSERT_TRUE(j.get("replays").at(0).get("file").str() == names.back() && j.get("replays").at(199).get("file").str() == names[5]);       // the newest 200
        const ctl::JsonValue& e = j.get("replays").at(0);
        ASSERT_TRUE(e.get("readable").is_null() && e.get("map").str() == "TINY.LVL" && e.get("players").size() == 2 && e.get("turns").as_int_or(0) == 700 && e.get("seconds").as_int_or(0) == 35);
        ASSERT_TRUE(e.get("players").at(0).str() == "Green (Ann)" && e.get("players").at(1).str() == "Red");                // (the public list gives the names: a seat with none is its colour)
        ASSERT_TRUE(e.get("finished").as_bool_or(false) && e.get("game").str() == "v0.0.0-test" && e.get("ended").as_int_or(0) == clock.now && e.get("bytes").as_int_or(0) > 100);
        ASSERT_TRUE(r.body.find("JUNK") == std::string::npos && r.body.find("enabled") == std::string::npos && r.body.find("max_bytes") == std::string::npos);
        // a file
        std::vector<uint8_t> on_disk;
        ASSERT_TRUE(mgr.replay_store()->read(names[3], on_disk));
        r = public_call(mgr, "GET", "/replays/" + names[3]);
        ASSERT_TRUE(r.status == 200 && r.content_type == "application/octet-stream" && r.body.size() == on_disk.size());
        ASSERT_TRUE(std::equal(on_disk.begin(), on_disk.end(), r.body.begin(), [](uint8_t x, char y) { return x == static_cast<uint8_t>(y); }));
        // the junk: the owner has it, the public has not
        ASSERT_EQ(replay_call(mgr, "GET", "/replays/" + junk).status, 200);
        ASSERT_EQ(public_call(mgr, "GET", "/replays/" + junk).status, 404);
        {   // the owner's list has every file of the folder (the junk says readable: false and has no more than its size and end), and counts them all; the public count is the files that can be read
            const ctl::JsonValue owner = replay_json_of(replay_call(mgr, "GET", "/replays", "limit=1000"));
            ASSERT_TRUE(owner.get("replays").size() == 206 && owner.get("count").as_int_or(0) == 206);
            size_t junk_entries = 0;
            for (size_t i = 0; i < owner.get("replays").size(); ++i) {
                const ctl::JsonValue& entry = owner.get("replays").at(i);
                const bool is_junk = entry.get("file").str() == junk;
                junk_entries += is_junk ? 1u : 0u;
                ASSERT_EQ(entry.get("readable").as_bool_or(is_junk), !is_junk);                       // (the default is the wrong answer: a missing key fails)
                ASSERT_EQ(entry.get("map").is_null(), is_junk);
            }
            ASSERT_EQ(junk_entries, size_t{1});
        }
        // nothing else
        for (const char* method : {"POST", "DELETE", "PUT", "HEAD", "OPTIONS", "PATCH", ""}) {
            ASSERT_EQ(public_call(mgr, method, "/replays").status, 404);
            ASSERT_EQ(public_call(mgr, method, "/replays/" + names[3]).status, 404);
        }
        for (const std::string& path : std::vector<std::string>{"/", "/rooms", "/rooms/ROOM-1", "/stats", "/busy", "/replays/", "/replays/..", "/replays/../x", "/replays/notes.txt", "/replaysx",
                                                                 "/replays//" + names[3], "/replays/" + names[3] + "/", "/Replays", "/healthz"}) {
            ASSERT_EQ(public_call(mgr, "GET", path).status, 404);
        }
        ASSERT_EQ(public_call(mgr, "GET", "/replays", "limit=1").status, 404);
        ASSERT_EQ(public_call(mgr, "GET", "/replays/" + names[3], "x").status, 404);
        // a server with no store has no list
        RoomManager none{MapStore(maps_dir())};
        ASSERT_EQ(public_call(none, "GET", "/replays").status, 404);
        ASSERT_EQ(public_call(none, "GET", "/replays/" + names[3]).status, 404);
    } TEST_END();

    TEST_CASE("S3.170 The Teams And The Fog Of War Of A Match Are In The Head Of Its File, And The File Plays Out On A Fresh Engine To Every Hash (A File That Lacked Them Would Diverge At The First Check)") {
        ReplayClock clock;
        {   // one person and three bots, Green + Blue against Red + Black (as S3.126): the Start of the match has the teams, so has the head, and the person and the bots have their names
            World w;
            std::string why;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("replay-teams", &clock), false, why));
            ASSERT_TRUE(w.mgr.create_room(spec_of("RT-1", 4), w.now).ok);
            Client& ann = w.connect("Ann", "RT-1");
            w.run(500);
            ASSERT_TRUE(ann.lobby->request_start(net::StartRequestMsg::all(net::FillLevel::Medium).fill, sim::StartTeams{true, 0, 2}));
            w.run(1500);
            for (int guard = 0; guard < 4000 && w.status("RT-1").state == RoomState::Running; ++guard) w.run(250);
            const RoomStatus s = w.status("RT-1");
            ASSERT_TRUE(s.state == RoomState::Finished && s.replay_kept && s.ticks >= 600);
            ASSERT_EQ(s.teams, std::string("0+2"));
            const StoredReplay f = load_stored(*w.mgr.replay_store(), s.replay_file);
            ASSERT_TRUE(f.ok && f.rep.complete && f.rep.match_over && f.rep.head.roster == 0x0F && !f.rep.head.fog);
            ASSERT_TRUE(f.rep.head.teams == sim::StartTeams({true, 0, 2}));
            ASSERT_TRUE(f.rep.head.names[0] == "Ann" && f.rep.head.names[1] == "Bot (Medium)" && f.rep.head.names[2] == "Bot (Medium)" && f.rep.head.names[3] == "Bot (Medium)");
            replay::Outcome played;
            ASSERT_TRUE(plays_out(f.rep, played));
            ASSERT_TRUE(played.complete && played.match_over && played.turns == f.rep.total_turns && played.hashes_checked > 5 && played.hash == s.referee_hash);
        }
        {   // Fog of War (no bots with it): two people
            World w;
            std::string why;
            ASSERT_TRUE(w.mgr.enable_replays(replay_config("replay-fog", &clock), false, why));
            RoomSpec spec = spec_of("RT-2", 2);
            spec.fog = true;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            start_pair(w, "RT-2", 38000);
            ASSERT_TRUE(w.status("RT-2").state == RoomState::Running && w.status("RT-2").fog && w.status("RT-2").turns >= 600);
            ASSERT_TRUE(w.mgr.close_room("RT-2", w.now));
            const RoomStatus s = w.status("RT-2");
            ASSERT_TRUE(s.replay_kept);
            const StoredReplay f = load_stored(*w.mgr.replay_store(), s.replay_file);
            ASSERT_TRUE(f.ok && f.rep.complete && f.rep.head.fog && !f.rep.head.teams.set);
            replay::Outcome played;
            ASSERT_TRUE(plays_out(f.rep, played));
            ASSERT_TRUE(played.complete && played.turns == f.rep.total_turns && played.hashes_checked >= 5);
        }
    } TEST_END();

    TEST_CASE("S3.171 A File Of Another Protocol Is Listed As What It Is, On The Public Door As Well (Every Release That Moves The Protocol Leaves Such Files): Its Rules Number Is In The Entry, The File Is Given, And It Is Not Hidden As One That Cannot Be Read") {
        ReplayClock clock;
        RoomManager mgr{MapStore(maps_dir())};
        std::string why;
        ASSERT_TRUE(mgr.enable_replays(replay_config("replay-rules", &clock), false, why));
        const uint16_t older = static_cast<uint16_t>(net::kProtocolVersion - 1);
        const ReplaySave before = mgr.replay_store()->save(synthetic_replay(1, 700, older));
        clock.now += 60;
        const ReplaySave now = mgr.replay_store()->save(synthetic_replay(2));
        ASSERT_TRUE(before.kept && now.kept);
        const ctl::JsonValue j = replay_json_of(public_call(mgr, "GET", "/replays"));
        ASSERT_TRUE(j.get("replays").size() == 2 && j.get("count").as_int_or(0) == 2);
        ASSERT_TRUE(j.get("replays").at(0).get("file").str() == now.file && j.get("replays").at(0).get("rules").as_int_or(0) == net::kProtocolVersion);
        ASSERT_TRUE(j.get("replays").at(1).get("file").str() == before.file && j.get("replays").at(1).get("rules").as_int_or(0) == older);
        ASSERT_TRUE(j.get("sim_rules").as_int_or(0) == replay::kSimRules && j.get("replays").at(0).get("sim_rules").as_int_or(0) == replay::kSimRules && j.get("replays").at(1).get("sim_rules").as_int_or(0) == replay::kSimRules);     // (the entry says the simulation's rules: a protocol number that moved alone is no other rules)
        ASSERT_EQ(public_call(mgr, "GET", "/replays/" + before.file).status, 200);
    } TEST_END();

    TEST_CASE("S3.172 A Refusal That Keeps Coming Is Told Once In The Server's Log And Then Counted, And The Count Is Told When The Server Stops (Matches Past The Hour's Limit Must Not Fill The Log)") {
        ReplayClock clock;
        World w;
        std::string why;
        ReplayConfig config = replay_config("replay-repeat", &clock);
        config.max_saves_per_hour = 1;
        ASSERT_TRUE(w.mgr.enable_replays(config, false, why));
        for (int i = 0; i < 4; ++i) {                                                                      // (the first match is kept, the three after it are past the hour's limit)
            const std::string code = "REP-" + std::to_string(i);
            ASSERT_TRUE(w.mgr.create_room(spec_of(code, 2), w.now).ok);
            start_pair(w, code, 38000, "Ann", "Bob");
            ASSERT_TRUE(w.mgr.close_room(code, w.now));
            clock.now += 10;
            ASSERT_TRUE(w.status(code).replay_kept == (i == 0));
        }
        size_t told = 0;
        size_t counted = 0;
        for (const std::string& n : w.mgr.take_notices()) {
            told += n.find("a match was not kept") != std::string::npos ? 1u : 0u;
            counted += n.find("repeated") != std::string::npos ? 1u : 0u;
        }
        ASSERT_EQ(told, size_t{1});                                                                        // the line comes once ...
        ASSERT_EQ(counted, size_t{0});                                                                     // ... and its two repeats are only counted
        ASSERT_EQ(w.mgr.shutdown(w.now), size_t{0});
        size_t at_stop = 0;
        for (const std::string& n : w.mgr.take_notices()) at_stop += n.find("repeated 2 more time(s)") != std::string::npos ? 1u : 0u;
        ASSERT_EQ(at_stop, size_t{1});                                                                     // the count comes when the server stops ...
        ASSERT_EQ(w.mgr.take_notices().size(), size_t{0});                                                 // ... once
    } TEST_END();

    TEST_CASE("S3.173 The Words The Game Uses For A Seat Without A Name Are Not Kept As A Name: A Person Who Typed Nothing, One Whose Name The Lobby Replaced With \"Player 4\", Are Their Colours In The File, The Owner's List, The Store's And The Public One; The Person Who Typed A Name And The Bot Have Theirs") {
        ReplayClock clock;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("replay-names", &clock), false, why));
        RoomSpec spec = spec_of("NAMES-1", 4);
        spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        Client& ann = w.connect("Ann", "NAMES-1", 0);
        w.connect("", "NAMES-1", 2);                                                                           // typed nothing
        w.connect("Bot (Hard)", "NAMES-1", 3);                                                                 // a name that looks like a bot's: the lobby calls this person "Player 4"
        w.run(1500 + kPre + 38000);
        RoomStatus s = w.status("NAMES-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.turns >= 600 && s.bot_controller);
        ASSERT_EQ(ann.lobby->start_info().names, (std::array<std::string, sim::MAX_PLAYERS>{{"Ann", "Bot (Medium)", "", "Player 4"}}));      // (what the room showed everybody)
        ASSERT_TRUE(w.mgr.close_room("NAMES-1", w.now));
        s = w.status("NAMES-1");
        ASSERT_TRUE(s.replay_kept && s.replay_note.empty());
        const ReplayStore& store = *w.mgr.replay_store();
        const StoredReplay f = load_stored(store, s.replay_file);
        ASSERT_TRUE(f.ok && f.rep.complete && f.rep.head.roster == 0x0F);
        ASSERT_EQ(f.rep.head.names, (std::array<std::string, sim::MAX_PLAYERS>{{"Ann", "Bot (Medium)", "", ""}}));
        for (const char* kept : {"Ann", "Bot (Medium)"}) ASSERT_TRUE(bytes_contain(f.bytes, kept));
        for (const char* never : {"Player", "Bot (Hard)", "NAMES-1", "127.0.0.1"}) ASSERT_FALSE(bytes_contain(f.bytes, never));
        const std::vector<std::string> shown = {"Green (Ann)", "Red (Bot (Medium))", "Blue", "Black"};
        ASSERT_EQ(store.list().at(0).players, shown);                                                            // the store's list
        for (const bool owner : {true, false}) {                                                               // the owner's list and the public one
            const ctl::HttpResponse r = owner ? replay_call(w.mgr, "GET", "/replays") : public_call(w.mgr, "GET", "/replays");
            ASSERT_EQ(r.status, 200);
            const ctl::JsonValue j = replay_json_of(r);
            ASSERT_TRUE(j.get("replays").size() == 1 && j.get("replays").at(0).get("players").size() == 4);
            for (size_t i = 0; i < shown.size(); ++i) ASSERT_EQ(j.get("replays").at(0).get("players").at(i).str(), shown[i]);
            ASSERT_TRUE(r.body.find("Player") == std::string::npos && r.body.find("NAMES-1") == std::string::npos);
        }
        replay::Outcome played;
        ASSERT_TRUE(plays_out(f.rep, played));
        ASSERT_TRUE(played.complete && played.turns == f.rep.total_turns && played.hashes_checked >= 5);
    } TEST_END();

    TEST_CASE("S3.174 A Name With A Quote, A Backslash And Angle Brackets Is Kept As Typed, And Both Lists Stay Valid JSON That Gives It Back") {
        ReplayClock clock;
        World w;
        std::string why;
        ASSERT_TRUE(w.mgr.enable_replays(replay_config("replay-quotes", &clock), false, why));
        ASSERT_TRUE(w.mgr.create_room(spec_of("QUOTE-1", 2), w.now).ok);
        const std::string first = "Q\"x\\y";                                                                    // Q"x\y
        const std::string second = "<b>&'</b>";
        start_pair(w, "QUOTE-1", 38000, first.c_str(), second.c_str());
        ASSERT_TRUE(w.mgr.close_room("QUOTE-1", w.now));
        const RoomStatus s = w.status("QUOTE-1");
        ASSERT_TRUE(s.replay_kept);
        const StoredReplay f = load_stored(*w.mgr.replay_store(), s.replay_file);
        ASSERT_TRUE(f.ok && f.rep.head.names[0] == first && f.rep.head.names[1] == second);
        ASSERT_TRUE(bytes_contain(f.bytes, first) && bytes_contain(f.bytes, second));
        for (const bool owner : {true, false}) {
            const ctl::HttpResponse r = owner ? replay_call(w.mgr, "GET", "/replays") : public_call(w.mgr, "GET", "/replays");
            ASSERT_EQ(r.status, 200);
            ctl::JsonValue j;
            std::string parse_error;
            ASSERT_MSG(ctl::parse_json(r.body, j, &parse_error), parse_error + ": " + r.body);                 // (valid JSON: the quote and the backslash are escaped)
            ASSERT_TRUE(j.get("replays").size() == 1 && j.get("replays").at(0).get("players").size() == 2);
            ASSERT_EQ(j.get("replays").at(0).get("players").at(0).str(), "Green (" + first + ")");
            ASSERT_EQ(j.get("replays").at(0).get("players").at(1).str(), "Red (" + second + ")");
        }
        // and the reader shows them as typed
        ASSERT_EQ(replay::seat_label(f.rep.head, 0), "Green (" + first + ")");
        ASSERT_EQ(replay::seat_label(f.rep.head, 1), "Red (" + second + ")");
    } TEST_END();

    TEST_CASE("S3.175 The Name That A File Keeps For A Person (replay_person_name): The Name As It Is In Printable ASCII, At Most 32 Characters And Without Blanks At Its Ends; \"Player\" And \"Player 1\" To \"Player 4\" And Nothing At All Give Nothing; A Name That Only Starts Like Them Is A Name; Whatever Comes In, A File Can Be Written With It") {
        const std::string c32 = "0123456789abcdefghijklmnopqrstuv";                                            // 32 characters
        ASSERT_EQ(c32.size(), size_t{32});
        const std::vector<std::pair<std::string, std::string>> table = {
            // nothing typed, or only blanks
            {"", ""}, {" ", ""}, {"   ", ""},
            // the words of the game for a seat without a name (the blanks around them do not make them a name)
            {"Player", ""}, {" Player", ""}, {"Player ", ""}, {"   Player   ", ""}, {"Player 1", ""}, {"Player 2", ""}, {"Player 3", ""}, {"Player 4", ""}, {"  Player 3  ", ""}, {"Player 1 ", ""},
            // names that only start like them, or differ in case, blanks or digits
            {"Player 5", "Player 5"}, {"Player 0", "Player 0"}, {"Player 10", "Player 10"}, {"Player 11", "Player 11"}, {"Players", "Players"}, {"player", "player"}, {"PLAYER", "PLAYER"},
            {"Player  2", "Player  2"}, {"Player2", "Player2"}, {"Player 2x", "Player 2x"}, {"Player-2", "Player-2"}, {"Player 04", "Player 04"}, {"My Player", "My Player"}, {"Player One", "Player One"},
            {"player 1", "player 1"}, {"Playe", "Playe"},
            // a name is kept as it is, the blanks at its ends cut and those inside left
            {"Ann", "Ann"}, {"  Ann  ", "Ann"}, {"Ann Lee", "Ann Lee"}, {"A  B", "A  B"}, {"x", "x"}, {"~", "~"}, {" !", "!"},
            // the signs of a text and of a page
            {"Q\"x\\y", "Q\"x\\y"}, {"<b>&'</b>", "<b>&'</b>"}, {"\\", "\\"}, {"\"", "\""}, {"a,b;c", "a,b;c"}, {"=SUM(1+1)", "=SUM(1+1)"},
            // at most 32 characters
            {c32, c32}, {c32 + "w", c32}, {c32 + std::string(20, 'z'), c32}, {" " + c32.substr(0, 31), c32.substr(0, 31)},
            // anything but the space to the tilde becomes a '?' (a letter with an accent is two bytes of UTF-8, a tab and a line end are control characters, DEL is one too)
            {"Zo\xC3\xAB", "Zo??"}, {"Ann\tBob", "Ann?Bob"}, {"Ann\nBob", "Ann?Bob"}, {std::string("a\0b", 3), "a?b"}, {"\x7F", "?"}, {"\x1F", "?"}, {"\x80\xFF", "??"}, {" \xC3\xAB ", "??"},
        };
        for (const auto& row : table) ASSERT_MSG(replay_person_name(row.first) == row.second, "'" + row.first + "' gave '" + replay_person_name(row.first) + "', not '" + row.second + "'");
        // the display names that a computer player can have ("Bot (Easy)", "Bot (Idle)" ...) are plain ASCII, short, with no blank at an end and none of the words of the game: the rule for a person's name leaves them as they
        // are, so a seat's name does not depend on which rule made it
        for (const char* kind : {"idle", "worker", "standard"}) {
            for (const ai::Level level : {ai::Level::Easy, ai::Level::Medium, ai::Level::Hard}) {
                const std::string bot = ai::bot_display_name(ai::BotSpec{0, kind, level});
                ASSERT_MSG(bot.rfind("Bot (", 0) == 0 && bot.size() <= 32 && replay_person_name(bot) == bot, "'" + bot + "' is not kept as it is");
            }
        }
        // whatever the bytes are, the name can be written into a file and read back as it is: printable, at most 32, no blank at an end, none of the words of the game
        uint32_t rng = 12345;
        const auto next = [&rng]() {
            rng = rng * 1664525u + 1013904223u;
            return rng >> 8;
        };
        for (int round = 0; round < 3000; ++round) {
            std::string raw;
            const uint32_t length = next() % 90;
            for (uint32_t i = 0; i < length; ++i) raw.push_back(next() % 5 == 0 ? ' ' : static_cast<char>(next() % 256));
            if (round % 7 == 0) raw = "Player " + std::string(1, static_cast<char>('0' + next() % 8)) + std::string(next() % 3, ' ');
            const std::string name = replay_person_name(raw);
            ASSERT_TRUE(name.size() <= 32 && name == replay_person_name(name));                               // (and it is its own fixed point)
            for (const char ch : name) ASSERT_TRUE(ch >= 0x20 && ch <= 0x7E);
            ASSERT_TRUE(name.empty() || (name.front() != ' ' && name.back() != ' '));
            ASSERT_TRUE(name != "Player" && name != "Player 1" && name != "Player 2" && name != "Player 3" && name != "Player 4");
            if (round % 50 == 0) {
                replay::Replay r;
                r.head.map_name = "TINY.LVL";
                r.head.roster = 0x0F;
                r.head.names = {name, name, name, name};
                r.complete = true;
                r.total_turns = 1;
                std::string error;
                ASSERT_MSG(!replay::encode(r, error).empty(), error);
            }
        }
    } TEST_END();
}
