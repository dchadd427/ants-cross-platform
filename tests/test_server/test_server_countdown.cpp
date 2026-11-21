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

// A room of `people` who have all said Hello, whose match has begun (Running, the Begin delivered): the clients are the caller's, `begin` is the moment it began
struct HeldBegin {
    uint32_t begin{0};
};

HeldBegin wait_for_begin(RWorld& w, const char* code) {
    HeldBegin out;
    for (int i = 0; i < 600 && out.begin == 0; ++i) {
        w.run(10);
        if (w.status(code).state == RoomState::Running) out.begin = w.now;
    }
    w.run(300);                                                                       // (the Begin reaches every machine)
    return out;
}

// The ticks that both machines ran, and the number of them at which their states are the same (0 when one of them differs anywhere: the caller asks for more than 20)
size_t agree_on_ticks(const RClient& a, const RClient& b) {
    size_t same = 0;
    for (const auto& kv : a.hash_at) {
        const auto other = b.hash_at.find(kv.first);
        if (other == b.hash_at.end()) continue;
        if (other->second != kv.second) return 0;
        ++same;
    }
    return same;
}

// A room for `players` persons with the countdown of a server's room (30 s), the resume countdown of one (10 s) and `hold_ms` for the countdown hold; the persons give no orders (nothing is done in the wait)
RoomSpec countdown_hold_spec(const char* code, uint8_t players, uint32_t hold_ms) {
    RoomSpec spec = held_spec(code, players);
    spec.countdown_s = net::kMatchCountdownSeconds;
    spec.solo_countdown_s = net::kSoloCountdownSeconds;
    spec.resume_countdown_ms = net::kResumeCountdownMs;
    spec.countdown_hold_ms = hold_ms;
    return spec;
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
        {   // the list of rooms keeps its size: every room's countdown is in its own status, the solo one is not repeated in the list (a thousand rooms must fit the answer's cap)
            ctl::HttpRequest lq;
            lq.method = "GET";
            lq.path = "/rooms";
            const ctl::HttpResponse listed = handle_control(mgr, lq, 5000);
            ASSERT_EQ(listed.status, 200);
            ASSERT_TRUE(listed.body.find("\"countdown_seconds\"") != std::string::npos && listed.body.find("solo_countdown_seconds") == std::string::npos);
        }
        RoomManager plain{MapStore(maps_dir())};                                          // a manager with the library's limits: no countdown for a room that does not ask
        r = handle_control(plain, [&]() { ctl::HttpRequest rq; rq.method = "POST"; rq.path = "/rooms"; rq.body = R"({"map":"TINY.LVL","code":"CS-5"})"; return rq; }(), 5000);
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_seconds").as_int_or(-1), 0);
        ASSERT_EQ(parse(r).get("solo_countdown_seconds").as_int_or(-1), 0);
    } TEST_END();
    TEST_CASE("S3.305 Protocol 21, The Countdown Hold Through The Manager: A Seat Lost In The Countdown Is Held At Most countdown_hold_ms (Here 20 s), The Status Says Who Is Missing, Then The Seat Is Dropped (drops_by_countdown_hold), The Resume Countdown Follows And The First Turn Carries The Drop; The Survivor Wins And The Dropped Machine Is Told; A Room With Nobody Else Ends \"everybody left\"; The Vote Goes On Sooner; A Spec With No Hold, And A Match That Plays, Keep The Seat")
    {
        {   // two people: Bob is cut 6 s into the countdown and never comes back
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(countdown_hold_spec("CH-1", 2, 20000), w.now).ok);
            RClient& ann = w.connect("Ann", "CH-1");
            RClient& bob = w.connect("Bob", "CH-1");
            ann.orders = bob.orders = false;
            const HeldBegin b = wait_for_begin(w, "CH-1");
            ASSERT_TRUE(b.begin != 0);
            const uint8_t bob_seat = bob.lobby->my_seat();
            w.run(6000);
            bob.reconnects = false;
            w.cut(bob);
            const uint32_t cut_at = w.now;
            w.run(1500);
            RoomStatus s = w.status("CH-1");
            ASSERT_TRUE(s.state == RoomState::Running && s.paused && s.turns == 0);
            ASSERT_TRUE(s.absent.size() == 1 && s.absent[0].name == "Bob" && !s.absent[0].catching_up);
            ASSERT_EQ(s.countdown_hold_ms, 20000u);                                      // the status says what the room holds
            w.run(17000);                                                                // 18.5 s after the cut: still held
            s = w.status("CH-1");
            ASSERT_TRUE(s.paused && s.absent.size() == 1 && s.drops_by_countdown_hold == 0);
            uint32_t dropped_after_ms = 0;
            for (int i = 0; i < 400 && dropped_after_ms == 0; ++i) {
                w.run(10);
                if (w.status("CH-1").drops_by_countdown_hold > 0) dropped_after_ms = w.now - cut_at;
            }
            ASSERT_TRUE(dropped_after_ms >= 20000 && dropped_after_ms <= 20100);        // the hold, to the 10 ms step of the manager
            s = w.status("CH-1");
            ASSERT_TRUE(s.drops_by_cap == 0 && s.drops_by_vote == 0 && s.absent.empty() && s.state == RoomState::Running);
            ASSERT_TRUE(s.paused && s.resume_s > 0 && s.resume_s <= 10);                 // the resume countdown that follows any pause of 3 s or more
            bob.reconnects = true;                                                       // Bob's machine finds a link: its seat is gone, and it is told so
            w.run(1500);
            ASSERT_TRUE(bob.lost && bob.was_rejected && bob.rejected == net::RejectReason::Dropped);
            uint32_t first_turn_after_ms = 0;
            for (int i = 0; i < 1500 && first_turn_after_ms == 0; ++i) {
                w.run(10);
                if (w.status("CH-1").turns > 0) first_turn_after_ms = w.now - cut_at;
            }
            ASSERT_TRUE(first_turn_after_ms >= 20000 + 10000 - 20 && first_turn_after_ms <= 20000 + 10000 + 200);      // the longest that a lost seat can hold the countdown: the hold and the resume countdown
            w.run(3000);
            s = w.status("CH-1");
            ASSERT_TRUE(s.state == RoomState::Finished && s.reason == "the match ended");
            ASSERT_TRUE(!s.rows.empty() && s.rows[0].name == "Ann" && s.rows[0].winner);        // Ann has won: the drop leaves her alone
            ASSERT_TRUE(ann.sim.is_player_dropped(bob_seat) && ann.sim.is_match_over());
        }
        {   // one person with a computer player: nobody can vote, and nobody is held up either; the seat goes with the hold and the room ends
            RWorld w;
            RoomSpec spec = countdown_hold_spec("CH-2", 2, 20000);
            spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            RClient& ann = w.connect("Ann", "CH-2");
            ann.orders = false;
            ASSERT_TRUE(wait_for_begin(w, "CH-2").begin != 0);
            w.run(4000);
            ann.reconnects = false;
            w.cut(ann);
            const uint32_t cut_at = w.now;
            w.run(10000);
            RoomStatus s = w.status("CH-2");
            ASSERT_TRUE(s.state == RoomState::Running && s.paused && s.vote_seat == 255 && s.turns == 0);      // held, and no vote to end it
            uint32_t ended_after_ms = 0;
            for (int i = 0; i < 1500 && ended_after_ms == 0; ++i) {
                w.run(10);
                if (w.status("CH-2").state != RoomState::Running) ended_after_ms = w.now - cut_at;
            }
            s = w.status("CH-2");
            ASSERT_TRUE(s.state == RoomState::Finished && s.reason == "everybody left");
            ASSERT_TRUE(ended_after_ms >= 20000 && ended_after_ms <= 20100);
            ASSERT_TRUE(s.drops_by_countdown_hold == 1 && s.drops_by_cap == 0);
        }
        {   // a spec with no hold (a spec built by code): the seat is held for the cap, as before, and a vote of the others is the way out
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(countdown_hold_spec("CH-3", 2, 0), w.now).ok);
            RClient& ann = w.connect("Ann", "CH-3");
            RClient& bob = w.connect("Bob", "CH-3");
            ann.orders = bob.orders = false;
            ASSERT_TRUE(wait_for_begin(w, "CH-3").begin != 0);
            const uint8_t bob_seat = bob.lobby->my_seat();
            w.run(6000);
            bob.reconnects = false;
            w.cut(bob);
            w.run(90000);                                                                // far beyond any hold
            RoomStatus s = w.status("CH-3");
            ASSERT_TRUE(s.state == RoomState::Running && s.paused && s.drops_by_countdown_hold == 0 && s.absent.size() == 1);
            ASSERT_TRUE(s.vote_seat == bob_seat && s.voters == 1);
            ASSERT_TRUE(ann.session->vote(bob_seat, true));
            w.run(15000);
            s = w.status("CH-3");
            ASSERT_TRUE(s.drops_by_vote == 1 && s.drops_by_countdown_hold == 0);
        }
        {   // the vote goes on sooner: three people, the hold is the server's own 60 s, and two of them vote 31 s after the cut
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(countdown_hold_spec("CH-4", 3, net::kCountdownHoldMs), w.now).ok);
            RClient& ann = w.connect("Ann", "CH-4");
            RClient& bob = w.connect("Bob", "CH-4");
            RClient& cat = w.connect("Cat", "CH-4");
            ann.orders = bob.orders = cat.orders = false;
            ASSERT_TRUE(wait_for_begin(w, "CH-4").begin != 0);
            const uint8_t cat_seat = cat.lobby->my_seat();
            w.run(2000);
            cat.reconnects = false;
            w.cut(cat);
            const uint32_t cut_at = w.now;
            w.run(31000);
            RoomStatus s = w.status("CH-4");
            ASSERT_TRUE(s.paused && s.vote_seat == cat_seat && s.voters == 2);
            ASSERT_TRUE(ann.session->vote(cat_seat, true) && bob.session->vote(cat_seat, true));
            w.run(500);
            s = w.status("CH-4");
            ASSERT_TRUE(s.drops_by_vote == 1 && s.drops_by_countdown_hold == 0 && s.absent.empty());
            ASSERT_TRUE(w.now - cut_at < net::kCountdownHoldMs);                         // (well before the hold would have dropped the seat)
            w.run(net::kResumeCountdownMs + 4000);
            ASSERT_TRUE(w.status("CH-4").turns > 0 && w.status("CH-4").state == RoomState::Running);
            ASSERT_TRUE(ann.sim.is_player_dropped(cat_seat) && bob.sim.is_player_dropped(cat_seat));
        }
        {   // once the match plays the hold does not apply: a seat that is lost after the first turn is held for the cap (and comes back after 35 s)
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(countdown_hold_spec("CH-5", 3, 20000), w.now).ok);
            RClient& ann = w.connect("Ann", "CH-5");
            RClient& bob = w.connect("Bob", "CH-5");
            RClient& cat = w.connect("Cat", "CH-5");
            ann.orders = bob.orders = cat.orders = false;
            ASSERT_TRUE(wait_for_begin(w, "CH-5").begin != 0);
            w.run(net::kMatchCountdownSeconds * 1000u);
            ASSERT_TRUE(w.until([&]() { return w.status("CH-5").turns > 20; }, 5000));
            cat.reconnects = false;
            w.cut(cat);
            w.run(35000);
            RoomStatus s = w.status("CH-5");
            ASSERT_TRUE(s.state == RoomState::Running && s.paused && s.absent.size() == 1 && s.drops_by_countdown_hold == 0);
            cat.reconnects = true;
            ASSERT_TRUE(w.until([&]() { return !w.status("CH-5").paused; }, 60000));
            s = w.status("CH-5");
            ASSERT_TRUE(s.rejoins == 1 && s.drops_by_countdown_hold == 0 && s.drops_by_cap == 0);
        }
    } TEST_END();

    TEST_CASE("S3.306 The Countdown Hold's Setting: 10 s .. 1 h In The Control Interface's \"countdown_hold_seconds\" (Refused Beyond, A Spec Built By Code Says 0: No Hold), The Server's --countdown-hold-seconds In RoomManager::default_spec (60 s), The Status Of One Room Says It And Counts Its Drops, The List Of Rooms Does Not")
    {
        ASSERT_EQ(RoomSpec().countdown_hold_ms, 0u);                                    // (a spec and the limits of a manager built by code have none)
        ASSERT_EQ(ServerLimits().countdown_hold_ms, 0u);
        ASSERT_EQ(net::kCountdownHoldMs, 60000u);                                       // the product's default: one minute
        ServerLimits limits;
        limits.countdown_hold_ms = net::kCountdownHoldMs;
        RoomManager mgr{MapStore(maps_dir()), limits};
        ASSERT_EQ(mgr.default_spec().countdown_hold_ms, net::kCountdownHoldMs);        // what the server was started with reaches the rooms that say nothing
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
        ctl::HttpResponse r = call(R"({"map":"TINY.LVL","code":"CHS-1"})");
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_hold_seconds").as_int_or(-1), 60);
        ASSERT_EQ(parse(r).get("drops_by_countdown_hold").as_int_or(-1), 0);
        r = call(R"({"map":"TINY.LVL","code":"CHS-2","countdown_hold_seconds":10})");     // the edges are good
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_hold_seconds").as_int_or(-1), 10);
        r = call(R"({"map":"TINY.LVL","code":"CHS-3","countdown_hold_seconds":3600})");
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_hold_seconds").as_int_or(-1), 3600);
        for (const char* bad : {R"("countdown_hold_seconds":9)", R"("countdown_hold_seconds":3601)", R"("countdown_hold_seconds":0)", R"("countdown_hold_seconds":-1)", R"("countdown_hold_seconds":"60")", R"("countdown_hold_seconds":60.5)", R"("countdown_hold_seconds":true)"}) {
            r = call(std::string(R"({"map":"TINY.LVL",)") + bad + "}");
            ASSERT_MSG(r.status == 400, bad);
            ASSERT_MSG(parse(r).get("error").str() == "\"countdown_hold_seconds\" must be an integer from 10 to 3600", parse(r).get("error").str());
        }
        ASSERT_EQ(mgr.room_count(), size_t{3});
        for (const uint32_t bad_ms : {1000u, 9999u, 3600001u}) {                        // (the manager checks it for every caller, not only the control interface's)
            RoomSpec bad = spec_of("CHS-4", 2);
            bad.countdown_hold_ms = bad_ms;
            const CreateResult refused = mgr.create_room(bad, 5000);
            ASSERT_TRUE(!refused.ok && refused.http_status == 400 && refused.error == "countdown_hold_seconds must be 10 to 3600");
        }
        {   // the list of rooms keeps its size: the setting and its counter are in the status of one room only
            ctl::HttpRequest lq;
            lq.method = "GET";
            lq.path = "/rooms";
            const ctl::HttpResponse listed = handle_control(mgr, lq, 5000);
            ASSERT_EQ(listed.status, 200);
            ASSERT_TRUE(listed.body.find("\"drops_by_cap\"") != std::string::npos && listed.body.find("countdown_hold_seconds") == std::string::npos && listed.body.find("drops_by_countdown_hold") == std::string::npos);
        }
        RoomManager plain{MapStore(maps_dir())};                                        // a manager with the library's limits: no hold for a room that does not ask
        r = handle_control(plain, [&]() { ctl::HttpRequest rq; rq.method = "POST"; rq.path = "/rooms"; rq.body = R"({"map":"TINY.LVL","code":"CHS-5"})"; return rq; }(), 5000);
        ASSERT_EQ(r.status, 201);
        ASSERT_EQ(parse(r).get("countdown_hold_seconds").as_int_or(-1), 0);
    } TEST_END();
    TEST_CASE("S3.307 Protocol 21, A Soak Of Leave And Rejoin Cycles In The Countdown: 24 Rooms Of Three People In Each Of Which A Seat Is Cut At A Moment Of The Countdown That The Cycle Chooses And Never Comes Back, Comes Back In Time, Comes Back Late, Flaps, Leaves On Purpose, Or Two Seats Are Cut: No Room Is Held Longer Than The Hold And The Resume Countdown, A Seat That Came Back In Time Plays In Step, A Seat That Did Not Is Dropped At The Hold And Told So, The Machines That Stayed Agree With The Referee")
    {
        const uint32_t hold_ms = 20000;
        const uint32_t resume_ms = net::kResumeCountdownMs;
        size_t by_kind[6] = {0, 0, 0, 0, 0, 0};
        for (int cycle = 0; cycle < 24; ++cycle) {
            const int kind = cycle % 6;
            const std::string code = "SK-" + std::to_string(cycle);
            RWorld w;
            ASSERT_TRUE(w.mgr.create_room(countdown_hold_spec(code.c_str(), 3, hold_ms), w.now).ok);
            RClient* c[3] = {&w.connect("Ann", code), &w.connect("Bob", code), &w.connect("Cat", code)};
            for (RClient* p : c) {
                p->orders = false;
                p->record_hashes = true;
            }
            const HeldBegin b = wait_for_begin(w, code.c_str());
            ASSERT_TRUE(b.begin != 0);
            const uint32_t cut_in_ms = 1000u + static_cast<uint32_t>(cycle * 4300) % 25000u;       // 1 s .. 26 s into the countdown, a different moment every cycle
            while (w.now - b.begin < cut_in_ms) w.run(10);
            RClient& victim = *c[1];
            const uint8_t victim_seat = victim.lobby->my_seat();
            bool dropped_by_hold = false;
            bool back_in_time = false;
            switch (kind) {
                case 0:   // never back
                    victim.reconnects = false;
                    w.cut(victim);
                    dropped_by_hold = true;
                    break;
                case 1:   // back after 8 s
                    victim.reconnects = false;
                    w.cut(victim);
                    w.run(8000);
                    victim.reconnects = true;
                    back_in_time = true;
                    break;
                case 2:   // back after the hold and 3 s: too late
                    victim.reconnects = false;
                    w.cut(victim);
                    w.run(hold_ms + 3000);
                    victim.reconnects = true;
                    dropped_by_hold = true;
                    break;
                case 3:   // flaps: cut, back after 2 s, cut again 4 s after the first cut, back 3 s later
                    victim.reconnects = false;
                    w.cut(victim);
                    w.run(2000);
                    victim.reconnects = true;
                    w.run(2000);
                    victim.reconnects = false;
                    w.cut(victim);
                    w.run(3000);
                    victim.reconnects = true;
                    back_in_time = true;
                    break;
                case 4:   // leaves on purpose: the seat is dropped at once and nobody waits for it, the countdown runs on
                    ASSERT_TRUE(victim.session != nullptr);
                    victim.session->leave();
                    break;
                default:  // two seats cut, Cat never comes back, Bob does after 5 s
                    victim.reconnects = false;
                    c[2]->reconnects = false;
                    w.cut(victim);
                    w.cut(*c[2]);
                    w.run(5000);
                    victim.reconnects = true;
                    back_in_time = true;
                    break;
            }
            uint32_t first_turn_at = 0;
            for (int i = 0; i < 9000 && first_turn_at == 0; ++i) {
                w.run(10);
                if (w.status(code).turns > 0) first_turn_at = w.now - b.begin;
            }
            ASSERT_MSG(first_turn_at != 0, code);
            ASSERT_MSG(first_turn_at <= 30000u + hold_ms + resume_ms + 2500u, code + " first turn after " + std::to_string(first_turn_at));      // held up by a seat for at most the hold and the resume countdown
            if (kind == 4) ASSERT_MSG(first_turn_at >= 29900u && first_turn_at <= 30200u, code + " first turn after " + std::to_string(first_turn_at));      // a leaver holds nobody: the countdown is as long as ever
            ASSERT_MSG(w.until([&]() { return !w.status(code).paused; }, 20000), code + " is held for good");           // (a seat that flapped lost its link again after the first turn: that pause is the cap's, and ends when the seat is back and its resume countdown is over)
            w.run(6000);
            const RoomStatus s = w.status(code);
            ASSERT_MSG(s.state == RoomState::Running && !s.paused, code + " is still held or over: " + s.reason + " (state " + std::to_string(static_cast<int>(s.state)) + ", paused " + std::to_string(s.paused) + ", absent " + std::to_string(s.absent.size()) + ", resume " + std::to_string(s.resume_s) + ", turns " + std::to_string(s.turns) + ", vote seat " + std::to_string(s.vote_seat) + ", rejoins " + std::to_string(s.rejoins) + ", first turn after " + std::to_string(first_turn_at) + ")");
            ASSERT_MSG(s.absent.empty(), code);
            if (kind == 5) {                                                             // Cat's seat was dropped by the hold, Bob came back: the two that stayed run in step
                ASSERT_MSG(s.drops_by_countdown_hold == 1 && s.rejoins >= 1, code);
                ASSERT_MSG(c[0]->sim.is_player_dropped(c[2]->lobby->my_seat()) && !c[0]->sim.is_player_dropped(victim_seat), code);
                ASSERT_MSG(agree_on_ticks(*c[0], victim) > 20, code);
            } else if (dropped_by_hold) {
                ASSERT_MSG(s.drops_by_countdown_hold == 1 && s.drops_by_cap == 0 && s.drops_by_vote == 0, code);
                ASSERT_MSG(c[0]->sim.is_player_dropped(victim_seat) && c[2]->sim.is_player_dropped(victim_seat), code);
                if (kind == 2) ASSERT_MSG(victim.lost && victim.was_rejected && victim.rejected == net::RejectReason::Dropped, code);      // the machine that came late is told
            } else if (back_in_time) {
                ASSERT_MSG(s.drops_by_countdown_hold == 0 && s.rejoins >= 1, code);
                ASSERT_MSG(!c[0]->sim.is_player_dropped(victim_seat), code);
            } else {
                ASSERT_MSG(s.drops_by_countdown_hold == 0, code);
            }
            for (RClient* p : c) {
                if (p->session && !p->lost) ASSERT_MSG(!p->session->desynced(), code);
            }
            ASSERT_TRUE(c[0]->session != nullptr && c[0]->sim.current_tick() > 20);
            if (kind != 5) ASSERT_MSG(agree_on_ticks(*c[0], *c[2]) > 20, code);                               // the machines that stayed agree at every tick that both ran
            ++by_kind[kind];
            ASSERT_TRUE(w.mgr.close_room(code, w.now));
        }
        for (const size_t n : by_kind) ASSERT_EQ(n, size_t{4});
    } TEST_END();
}
