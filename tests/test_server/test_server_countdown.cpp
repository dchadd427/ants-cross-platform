// Protocol 21, the countdown before the first turn, in a server's room (docs/MATCH_COUNTDOWN.md, docs/SERVER.md): a room with two or more people holds the first turn for its
// `countdown_s` (the server's --start-countdown-seconds, 30 by default) and the Begin that it sends tells that many seconds; a room of exactly one person (with or without computer players) holds it the
// shorter of `countdown_s` and `solo_countdown_s` (--solo-countdown-seconds, 10 by default), and a room whose setting is 0 keeps the 5 s dialog of protocol 12 and tells 0. The settings' bounds, their
// defaults and the control interface's keys are tested here too.
#include "server_fixtures.hpp"

namespace {

// What a room of exactly one person does at its start: the seconds that its Begin tells, and when its first turn is sealed after the match began (the wait is the room's own, whoever sits in the seats)
struct SoloStart {
    uint8_t begin_s{255};
    uint32_t sealed_at{0};
};

SoloStart solo_room_start(uint32_t countdown_s, uint32_t solo_countdown_s, bool with_bot, const char* code) {
    World w;
    RoomSpec spec = spec_of(code, with_bot ? 2 : 4);          // (without a computer player in the specification: a room for four, which its leader starts alone with a fill)
    spec.countdown_s = countdown_s;
    spec.solo_countdown_s = solo_countdown_s;
    if (with_bot) spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
    SoloStart out;
    if (!w.mgr.create_room(spec, w.now).ok) return out;
    Client& fay = w.connect("Fay", code);
    fay.next_order_ms = 0xFFFFFFFFu;
    if (!with_bot) {
        w.run(500);
        if (!fay.lobby->request_start(net::FillLevel::Medium)) return out;
    }
    uint32_t begin = 0;
    for (int i = 0; i < 400 && begin == 0; ++i) {
        w.run(10);
        if (w.status(code).state == RoomState::Running) begin = w.now;
    }
    if (begin == 0) return out;
    w.run(300);
    out.begin_s = fay.lobby->countdown_seconds();
    for (int i = 0; i < 6600 && out.sealed_at == 0; ++i) {
        w.run(10);
        if (w.status(code).turns > 0) out.sealed_at = w.now - begin;
    }
    return out;
}

// A room of one person with a computer player, played with the same seed and the same orders from the same moments after its first turn: what its machine saw of the turns and the state after every tick
struct SoloPlayed {
    uint8_t begin_s{255};
    uint32_t sealed_at{0};
    std::vector<std::pair<uint64_t, sim::Command>> commands;
    std::map<uint64_t, uint64_t> hashes;
    uint64_t ticks{0};
    bool desync{false};
};

SoloPlayed solo_room_played(uint32_t solo_countdown_s) {
    World w;
    RoomSpec spec = spec_of("CD-9", 2);
    spec.has_seed = true;
    spec.seed = 777;
    spec.countdown_s = net::kMatchCountdownSeconds;
    spec.solo_countdown_s = solo_countdown_s;
    spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
    SoloPlayed out;
    if (!w.mgr.create_room(spec, w.now).ok) return out;
    Client& fay = w.connect("Fay", "CD-9");
    fay.record_hashes = true;
    fay.record_commands = true;
    fay.next_order_ms = 0xFFFFFFFFu;
    uint32_t begin = 0;
    for (int i = 0; i < 400 && begin == 0; ++i) {
        w.run(10);
        if (w.status("CD-9").state == RoomState::Running) begin = w.now;
    }
    if (begin == 0) return out;
    w.run(300);
    out.begin_s = fay.lobby->countdown_seconds();
    const uint32_t wait_ms = (out.begin_s != 0 ? out.begin_s : net::kMatchStartDelayMs / 1000u) * 1000u;
    fay.next_order_ms = begin + wait_ms + 2000;                                       // the orders start at the same moment after the first turn, whatever the wait was
    while (w.now - begin < wait_ms + 14000) {
        w.run(10);
        if (out.sealed_at == 0 && w.status("CD-9").turns > 0) out.sealed_at = w.now - begin;
    }
    out.commands = fay.saw;
    out.hashes = fay.hash_at;
    out.ticks = fay.sim.current_tick();
    out.desync = fay.session != nullptr && fay.session->desynced();
    return out;
}

}  // namespace

void run_countdown_tests() {
    TEST_CASE("S3.191 Protocol 21, A Room Of Two People Tells The Begin 30 s And Seals The First Turn Exactly 30 s After The Match Began; Chat In The Wait Reaches The Other; The Match Then Runs In Step") {
        World w;
        RoomSpec spec = spec_of("CD-1", 2);
        spec.countdown_s = net::kMatchCountdownSeconds;
        spec.solo_countdown_s = 5;                                                    // (a room of two people holds the room's countdown, not the solo one)
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        Client& ann = w.connect("Ann", "CD-1");
        Client& bob = w.connect("Bob", "CD-1");
        ann.next_order_ms = bob.next_order_ms = 0xFFFFFFFFu;                          // (the rig's own orders are off: its machines know no dialog; this test sends nothing in the wait)
        ann.record_hashes = bob.record_hashes = true;
        uint32_t begin = 0;
        for (int i = 0; i < 400 && begin == 0; ++i) {
            w.run(10);
            if (w.status("CD-1").state == RoomState::Running) begin = w.now;
        }
        ASSERT_TRUE(begin != 0);
        w.run(300);
        ASSERT_TRUE(ann.lobby->phase() == net::ClientLobby::Phase::Begun && bob.lobby->phase() == net::ClientLobby::Phase::Begun);
        ASSERT_EQ(ann.lobby->countdown_seconds(), net::kMatchCountdownSeconds);       // the Begin of this match said 30, to every machine
        ASSERT_EQ(bob.lobby->countdown_seconds(), net::kMatchCountdownSeconds);
        ASSERT_EQ(w.status("CD-1").countdown_s, net::kMatchCountdownSeconds);          // the status says what the room holds
        ASSERT_TRUE(ann.session != nullptr && bob.session != nullptr);
        while (w.now - begin < 12000) w.run(10);
        ASSERT_TRUE(ann.session->chat("hi Bob", false));                              // twelve seconds in: the others hear it, now and not at the start
        w.run(300);
        ASSERT_TRUE(bob.chats.size() == 1 && bob.chats[0].text == "hi Bob" && bob.chats[0].sender == ann.lobby->my_seat());
        while (w.now - begin < 29900) w.run(10);
        RoomStatus s = w.status("CD-1");
        ASSERT_TRUE(s.state == RoomState::Running && s.turns == 0 && s.ticks == 0 && !s.paused);      // 29.9 s in: nothing sealed, nothing run
        ASSERT_TRUE(ann.sim.current_tick() == 0 && bob.sim.current_tick() == 0);
        uint32_t sealed_at = 0;
        for (int i = 0; i < 100 && sealed_at == 0; ++i) {
            w.run(10);
            if (w.status("CD-1").turns > 0) sealed_at = w.now - begin;
        }
        ASSERT_TRUE(sealed_at >= net::kMatchCountdownSeconds * 1000u - 10 && sealed_at <= net::kMatchCountdownSeconds * 1000u + 60);      // the first turn: 30 s after the match began
        w.run(6000);
        ASSERT_TRUE(w.status("CD-1").ticks > 60 && ann.sim.current_tick() > 60 && bob.sim.current_tick() > 60);
        ASSERT_FALSE(ann.session->desynced() || bob.session->desynced());
        size_t compared = 0;
        for (const auto& kv : ann.hash_at) {                                           // the states of the two machines agree at every tick that both ran
            const auto other = bob.hash_at.find(kv.first);
            if (other == bob.hash_at.end()) continue;
            ASSERT_EQ(other->second, kv.second);
            ++compared;
        }
        ASSERT_TRUE(compared > 40);
    } TEST_END();

    TEST_CASE("S3.192 Protocol 21, A Room Of Exactly One Person (With Or Without A Computer Player) Holds The First Turn The Shorter Of The Room's And The Solo Countdown And Tells It In The Begin; 0 There, A Room Whose Setting Is 0 And A Room That Never Heard Of The Countdown Keep The 5 s Dialog And Tell 0") {
        {   // one person, one computer player: the card of a short count, the setting's seconds in the Begin and in the first turn
            const SoloStart s = solo_room_start(net::kMatchCountdownSeconds, 10, true, "CD-2");
            ASSERT_EQ(s.begin_s, 10);
            ASSERT_TRUE(s.sealed_at >= 10000 - 10 && s.sealed_at <= 10000 + 60);
        }
        {   // one person who starts a room alone with a fill of computer players: the same
            const SoloStart s = solo_room_start(net::kMatchCountdownSeconds, 7, false, "CD-2A");
            ASSERT_EQ(s.begin_s, 7);
            ASSERT_TRUE(s.sealed_at >= 7000 - 10 && s.sealed_at <= 7000 + 60);
        }
        {   // the shorter of the two: a room whose own countdown is shorter than the solo one holds that
            const SoloStart s = solo_room_start(4, 10, true, "CD-2B");
            ASSERT_EQ(s.begin_s, 4);
            ASSERT_TRUE(s.sealed_at >= 4000 - 10 && s.sealed_at <= 4000 + 60);
        }
        {   // the solo countdown is 0: the 5 s dialog of protocol 12, as before, with and without a computer player
            for (const bool with_bot : {true, false}) {
                const SoloStart s = solo_room_start(net::kMatchCountdownSeconds, 0, with_bot, with_bot ? "CD-2C" : "CD-2D");
                ASSERT_EQ(s.begin_s, 0);
                ASSERT_TRUE(s.sealed_at >= kPre - 10 && s.sealed_at <= kPre + 60);
            }
        }
        {   // the room's own countdown is 0: no card for anybody, whatever the solo setting says
            const SoloStart s = solo_room_start(0, 10, true, "CD-2E");
            ASSERT_EQ(s.begin_s, 0);
            ASSERT_TRUE(s.sealed_at >= kPre - 10 && s.sealed_at <= kPre + 60);
        }
        {   // nobody in the room (the server's own match of computer players): no card, whatever the settings say
            World w;
            RoomSpec spec = spec_of("CD-2G", 2);
            for (uint8_t seat = 0; seat < 2; ++seat) {
                ai::BotSpec bot;
                bot.seat = seat;
                bot.level = ai::Level::Medium;
                spec.bots.push_back(bot);
            }
            spec.bots_only = true;
            spec.early_start = false;
            spec.reconnect = false;
            spec.wait_ms = 5u * 1000u;
            spec.run_ms = 10u * 60u * 1000u;
            spec.countdown_s = net::kMatchCountdownSeconds;
            spec.solo_countdown_s = 10;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            uint32_t begin = 0;
            for (int i = 0; i < 1500 && begin == 0; ++i) {
                w.run(10);
                if (w.status("CD-2G").state == RoomState::Running) begin = w.now;
            }
            ASSERT_TRUE(begin != 0);
            uint32_t sealed_at = 0;
            for (int i = 0; i < 1400 && sealed_at == 0; ++i) {
                w.run(10);
                if (w.status("CD-2G").turns > 0) sealed_at = w.now - begin;
            }
            ASSERT_TRUE(sealed_at >= kPre - 10 && sealed_at <= kPre + 60);
        }
        {   // a spec built by code says nothing about it: the dialog
            const SoloStart s = solo_room_start(0, 0, true, "CD-2F");
            ASSERT_EQ(s.begin_s, 0);
            ASSERT_TRUE(s.sealed_at >= kPre - 10 && s.sealed_at <= kPre + 60);
        }
        for (const bool listed : {true, false}) {   // two people: a room whose setting is 0, and a spec that says nothing about it
            World w;
            RoomSpec spec = spec_of("CD-3", 2);
            spec.solo_countdown_s = 10;                                                 // (the solo setting is for one person only)
            if (listed) spec.countdown_s = 0;
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& gus = w.connect("Gus", "CD-3");
            Client& hal = w.connect("Hal", "CD-3");
            gus.next_order_ms = hal.next_order_ms = 0xFFFFFFFFu;
            uint32_t begin = 0;
            for (int i = 0; i < 400 && begin == 0; ++i) {
                w.run(10);
                if (w.status("CD-3").state == RoomState::Running) begin = w.now;
            }
            ASSERT_TRUE(begin != 0);
            w.run(300);
            ASSERT_EQ(gus.lobby->countdown_seconds(), 0);
            ASSERT_EQ(hal.lobby->countdown_seconds(), 0);
            uint32_t sealed_at = 0;
            for (int i = 0; i < 700 && sealed_at == 0; ++i) {
                w.run(10);
                if (w.status("CD-3").turns > 0) sealed_at = w.now - begin;
            }
            ASSERT_TRUE(sealed_at >= kPre - 10 && sealed_at <= kPre + 60);
        }
    } TEST_END();

    TEST_CASE("S3.264 Protocol 21, The Wait Changes No Turn: A Room Of One Person Held 10 s And One Held 5 s, With The Same Seed And Orders From The Same Moments After Their First Turn, See The Same Commands And Reach The Same State After Every Tick") {
        const SoloPlayed ten = solo_room_played(10);
        const SoloPlayed five = solo_room_played(0);                                      // (0: the plain 5 s dialog)
        ASSERT_EQ(ten.begin_s, 10);
        ASSERT_EQ(five.begin_s, 0);
        ASSERT_TRUE(ten.sealed_at >= 10000 - 10 && ten.sealed_at <= 10000 + 60);
        ASSERT_TRUE(five.sealed_at >= kPre - 10 && five.sealed_at <= kPre + 60);
        ASSERT_TRUE(ten.ticks > 100 && five.ticks > 100);
        ASSERT_EQ(ten.ticks, five.ticks);
        ASSERT_FALSE(ten.desync || five.desync);
        ASSERT_TRUE(!ten.commands.empty());                                               // (the test did give orders)
        ASSERT_TRUE(ten.commands == five.commands);                                       // every command that a turn applied, with the tick it came at
        ASSERT_TRUE(ten.hashes == five.hashes);                                           // and the state after every tick, from tick 1 on
        ASSERT_TRUE(ten.hashes.size() > 100);
    } TEST_END();

    TEST_CASE("S3.193 Protocol 21, The Setting: 0 .. 60 Seconds, Off In A Library's Defaults, The Server's --start-countdown-seconds In RoomManager::default_spec, And The Control Interface's \"countdown_seconds\" With Its Status Key") {
        ASSERT_EQ(RoomSpec().countdown_s, 0u);                                          // (a spec built by code, and the limits of a manager built by code, have none)
        ASSERT_EQ(ServerLimits().countdown_s, 0u);
        ASSERT_EQ(RoomSpec().solo_countdown_s, 0u);
        ASSERT_EQ(ServerLimits().solo_countdown_s, 0u);
        ServerLimits limits;
        limits.countdown_s = net::kMatchCountdownSeconds;
        limits.solo_countdown_s = net::kSoloCountdownSeconds;
        RoomManager mgr{MapStore(maps_dir()), limits};
        ASSERT_EQ(mgr.default_spec().countdown_s, net::kMatchCountdownSeconds);          // what the server was started with reaches the rooms that say nothing
        ASSERT_EQ(mgr.default_spec().solo_countdown_s, net::kSoloCountdownSeconds);
        const auto call = [&](const std::string& body) {
            ctl::HttpRequest rq;
            rq.method = "POST";
            rq.path = "/rooms";
            rq.body = body;
            return handle_control(mgr, rq, 5000);
        };
        const auto parse = [](const ctl::HttpResponse& r) {
            ctl::JsonValue v;
            std::string why;
            ctl::parse_json(r.body, v, &why);
            return v;
        };
        ctl::HttpResponse r = call(R"({"map":"TINY.LVL","code":"CS-1"})");
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_seconds").as_int_or(-1), 30);                 // the room's own status says what it holds
        ASSERT_EQ(parse(r).get("solo_countdown_seconds").as_int_or(-1), 10);
        r = call(R"({"map":"TINY.LVL","code":"CS-2","countdown_seconds":0})");           // the edges are good
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_seconds").as_int_or(-1), 0);
        r = call(R"({"map":"TINY.LVL","code":"CS-3","countdown_seconds":60})");
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_seconds").as_int_or(-1), 60);
        for (const char* bad : {R"("countdown_seconds":61)", R"("countdown_seconds":-1)", R"("countdown_seconds":"30")", R"("countdown_seconds":30.5)", R"("countdown_seconds":true)"}) {
            r = call(std::string(R"({"map":"TINY.LVL",)") + bad + "}");
            ASSERT_MSG(r.status == 400, bad);
            ASSERT_MSG(parse(r).get("error").str() == "\"countdown_seconds\" must be an integer from 0 to 60", parse(r).get("error").str());
        }
        r = call(R"({"map":"TINY.LVL","code":"CS-6","solo_countdown_seconds":0})");       // the solo setting has the same edges
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("solo_countdown_seconds").as_int_or(-1), 0);
        r = call(R"({"map":"TINY.LVL","code":"CS-7","solo_countdown_seconds":60})");
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("solo_countdown_seconds").as_int_or(-1), 60);
        for (const char* bad : {R"("solo_countdown_seconds":61)", R"("solo_countdown_seconds":-1)", R"("solo_countdown_seconds":"10")", R"("solo_countdown_seconds":10.5)", R"("solo_countdown_seconds":true)"}) {
            r = call(std::string(R"({"map":"TINY.LVL",)") + bad + "}");
            ASSERT_MSG(r.status == 400, bad);
            ASSERT_MSG(parse(r).get("error").str() == "\"solo_countdown_seconds\" must be an integer from 0 to 60", parse(r).get("error").str());
        }
        ASSERT_EQ(mgr.room_count(), size_t{5});
        RoomSpec too_long = spec_of("CS-4", 2);                                           // (the manager checks it for every caller, not only the control interface's)
        too_long.countdown_s = net::kMaxCountdownSeconds + 1;
        const CreateResult refused = mgr.create_room(too_long, 5000);
        ASSERT_TRUE(!refused.ok && refused.http_status == 400 && refused.error == "countdown_seconds must be 0 to 60");
        RoomSpec solo_too_long = spec_of("CS-8", 2);
        solo_too_long.solo_countdown_s = net::kMaxCountdownSeconds + 1;
        const CreateResult solo_refused = mgr.create_room(solo_too_long, 5000);
        ASSERT_TRUE(!solo_refused.ok && solo_refused.http_status == 400 && solo_refused.error == "solo_countdown_seconds must be 0 to 60");
        RoomManager plain{MapStore(maps_dir())};                                          // a manager with the library's limits: no countdown for a room that does not ask
        r = handle_control(plain, [&]() { ctl::HttpRequest rq; rq.method = "POST"; rq.path = "/rooms"; rq.body = R"({"map":"TINY.LVL","code":"CS-5"})"; return rq; }(), 5000);
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_seconds").as_int_or(-1), 0);
        ASSERT_EQ(parse(r).get("solo_countdown_seconds").as_int_or(-1), 0);
    } TEST_END();
}
