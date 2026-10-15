// Protocol 21, the countdown before the first turn, in a server's room (docs/MATCH_COUNTDOWN.md, docs/SERVER.md): a room with two or more people holds the first turn for its
// `countdown_s` (the server's --start-countdown-seconds, 30 by default) and the Begin that it sends tells that many seconds; a room of one person (computer players beside it) and a room whose
// setting is 0 keep the 5 s dialog of protocol 12 and tell 0. The setting's bounds, its default and the control interface's key are tested here too.
#include "server_fixtures.hpp"

void run_countdown_tests() {
    TEST_CASE("S3.191 Protocol 21, A Room Of Two People Tells The Begin 30 s And Seals The First Turn Exactly 30 s After The Match Began; Chat In The Wait Reaches The Other; The Match Then Runs In Step") {
        World w;
        RoomSpec spec = spec_of("CD-1", 2);
        spec.countdown_s = net::kMatchCountdownSeconds;
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

    TEST_CASE("S3.192 Protocol 21, A Room Of One Person With A Computer Player, A Room Whose Setting Is 0 And A Room That Never Heard Of The Countdown Keep The 5 s Dialog And Tell 0") {
        {   // one person, one computer player: the countdown is for people to talk to each other
            World w;
            RoomSpec spec = spec_of("CD-2", 2);
            spec.countdown_s = net::kMatchCountdownSeconds;
            spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            Client& fay = w.connect("Fay", "CD-2");
            fay.next_order_ms = 0xFFFFFFFFu;
            uint32_t begin = 0;
            for (int i = 0; i < 400 && begin == 0; ++i) {
                w.run(10);
                if (w.status("CD-2").state == RoomState::Running) begin = w.now;
            }
            ASSERT_TRUE(begin != 0);
            w.run(300);
            ASSERT_EQ(fay.lobby->countdown_seconds(), 0);
            uint32_t sealed_at = 0;
            for (int i = 0; i < 700 && sealed_at == 0; ++i) {
                w.run(10);
                if (w.status("CD-2").turns > 0) sealed_at = w.now - begin;
            }
            ASSERT_TRUE(sealed_at >= kPre - 10 && sealed_at <= kPre + 60);              // 5 s, as protocol 12
        }
        for (const bool listed : {true, false}) {   // two people: a room whose setting is 0, and a spec that says nothing about it
            World w;
            RoomSpec spec = spec_of("CD-3", 2);
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

    TEST_CASE("S3.193 Protocol 21, The Setting: 0 .. 60 Seconds, Off In A Library's Defaults, The Server's --start-countdown-seconds In RoomManager::default_spec, And The Control Interface's \"countdown_seconds\" With Its Status Key") {
        ASSERT_EQ(RoomSpec().countdown_s, 0u);                                          // (a spec built by code, and the limits of a manager built by code, have none)
        ASSERT_EQ(ServerLimits().countdown_s, 0u);
        ServerLimits limits;
        limits.countdown_s = net::kMatchCountdownSeconds;
        RoomManager mgr{MapStore(maps_dir()), limits};
        ASSERT_EQ(mgr.default_spec().countdown_s, net::kMatchCountdownSeconds);          // what the server was started with reaches the rooms that say nothing
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
        ASSERT_EQ(mgr.room_count(), size_t{3});
        RoomSpec too_long = spec_of("CS-4", 2);                                           // (the manager checks it for every caller, not only the control interface's)
        too_long.countdown_s = net::kMaxCountdownSeconds + 1;
        const CreateResult refused = mgr.create_room(too_long, 5000);
        ASSERT_TRUE(!refused.ok && refused.http_status == 400 && refused.error == "countdown_seconds must be 0 to 60");
        RoomManager plain{MapStore(maps_dir())};                                          // a manager with the library's limits: no countdown for a room that does not ask
        r = handle_control(plain, [&]() { ctl::HttpRequest rq; rq.method = "POST"; rq.path = "/rooms"; rq.body = R"({"map":"TINY.LVL","code":"CS-5"})"; return rq; }(), 5000);
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_seconds").as_int_or(-1), 0);
    } TEST_END();
}
