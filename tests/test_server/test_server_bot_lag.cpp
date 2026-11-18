// Tests of the network handicap of a room's computer players (docs/BOTS.md, "Fairness in detail"; ants_server/bot_lag.hpp): the hold that the people's links give (its rounding, cap, median, hysteresis and
// dwell), the sink that holds a bot's commands back (order, no loss, the pass-through at a hold of 0, the turn that applied_at names), and whole rooms: a person on a link of 30, 90, 180 and 300 ms
// round trip gets a bot that waits 1, 2, 4 and 6 ticks; a person on a LAN link, or nobody, a bot that does not; two people give the median; a pause, a slow machine and a person who leaves leave the room
// right; a second window that takes a seat is read on its own link; the point of it all, measured (a person's order that answers a tick is never carried after a bot's that answers the same tick); and a match with held commands plays out on a fresh engine to the same hashes (the replay case, S3.290, is in replay_tests.hpp, a match of
// bots alone holds nothing: S3.180, and a room that a restart of the server brought back holds its bots again: S3.93).
#include "server_fixtures.hpp"

namespace {

// What a sink does with what it is given: the numbers of the commands (their tile) that reached the sequencer, in order, and the room's numbers that the sink asks for
struct FakeLine {
    uint64_t sealed{10};
    uint32_t hold{0};
    bool accepting{true};
    std::vector<int> delivered;
    LaggedSink::Line line() {
        LaggedSink::Line l;
        l.accepts = [this]() { return accepting; };
        l.deliver = [this](const sim::Command& c) {
            if (!accepting) return false;
            delivered.push_back(c.tile_x);
            return true;
        };
        l.turns_sealed = [this]() { return sealed; };
        l.hold_ticks = [this]() { return hold; };
        return l;
    }
};

sim::Command numbered(int n) {
    sim::Command c;
    c.type = sim::CommandType::GroupMove;
    c.tile_x = static_cast<int16_t>(n);
    c.tile_y = 1;
    c.ants = {1};
    return c;
}

// One pass of the policy: `humans` persons are connected and `ms` are the readings that they have
void feed(BotLag& lag, uint64_t tick, size_t humans, std::initializer_list<uint32_t> ms) {
    std::vector<uint32_t> v(ms);
    lag.update(tick, humans, v.data(), v.size());
}

// A room of one person (seat 0, on a link of `one_way_ms` each way) and a Hard bot (seat 1) that has played `play_ms` after the start dialog
struct LagRoom {
    RWorld w;
    RoomSpec spec;
    RClient* ann{nullptr};
    explicit LagRoom(const char* code, uint32_t one_way_ms) {
        spec = held_spec(code, 2);
        spec.bots = {ai::BotSpec{1, "standard", ai::Level::Hard}};
        w.link = {one_way_ms, 0};
        w.mgr.create_room(spec, w.now);
        ann = &w.connect("Ann", code);
    }
};

// What the hold is for, measured: a referee's session with a bot's seat (seat 0) and a person (seat 1) over a link of `one_way_ms` each way, the bot's seat wired as Room::start_bots wires it (the hold from
// the session's readings, the sink's release, then the bot's order). Every 40th tick from tick 200 on, both answer the same tick at once: the bot hands an order to its sink in the referee's hook of that
// tick, the person hands one to his session in the frame in which his own engine runs it. The turn that carries each is read off the sealed turns. `behind` is the person's turn less the bot's, the
// least and the most over the events. `held` false: the room's hold stays 0 (the sink of before the hold).
struct Lead {
    int least{1000};
    int most{-1000};
    int events{0};
    uint32_t hold{0};
};

Lead measure_lead(uint32_t one_way_ms, uint32_t first_frame_ms, bool held) {
    const auto build = [](sim::SimulationEngine& e, uint32_t& ant_of_0, uint32_t& ant_of_1) {
        e.init_test_world(60, 60, 1, 720000);
        e.grid_mut().set_anthill(0, sim::TileCoord{4, 4});
        e.grid_mut().set_anthill(1, sim::TileCoord{50, 50});
        ant_of_0 = e.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 10});
        ant_of_1 = e.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{51, 56});
    };
    net::LoopbackNetwork network(7);
    sim::SimulationEngine referee;
    sim::SimulationEngine machine;
    uint32_t bot_ant = 0;
    uint32_t person_ant = 0;
    build(referee, bot_ant, person_ant);
    build(machine, bot_ant, person_ant);
    auto ends = network.connect(net::LoopbackNetwork::Link{one_way_ms, 0});
    net::HostSession::Config hc;
    hc.host_player = net::kNoSeat;
    net::HostSession host(referee, hc);
    host.add_client(1, ends.first);
    host.add_bot_seat(0);
    net::ClientSession::Config cc;
    cc.player = 1;
    cc.host = net::kNoSeat;
    cc.migration = false;
    net::ClientSession person(machine, cc);
    person.set_connection(ends.second);

    BotLag lag;
    LaggedSink::Line line;
    line.accepts = [&host]() { return host.accepts_bot(0); };
    line.deliver = [&host](const sim::Command& c) { return host.submit_bot(0, c); };
    line.turns_sealed = [&host]() -> uint64_t { return host.turns_sealed(); };
    line.hold_ticks = [&lag]() { return lag.hold_ticks(); };
    LaggedSink sink(std::move(line));

    const auto answer = [](uint64_t tick, uint32_t ant) {                              // the order that answers `tick`: its tile says which
        sim::Command c;
        c.type = sim::CommandType::GroupMove;
        c.tile_x = static_cast<int16_t>(tick / 40);
        c.tile_y = 20;
        c.ants = {ant};
        return c;
    };
    std::map<int, uint32_t> bot_turn;
    std::map<int, uint32_t> person_turn;
    host.set_on_seal([&](const net::TurnMsg& t) {
        for (const sim::Command& c : t.commands) {
            if (c.type == sim::CommandType::GroupMove) (c.issuer == 0 ? bot_turn : person_turn)[c.tile_x] = t.turn;
        }
    });
    host.runner().set_on_tick([&]() {
        const uint64_t tick = referee.current_tick();
        if (held) {
            uint32_t trip = 0;
            const bool known = host.turn_trip_ms(1, trip);
            lag.update(tick, size_t{1}, &trip, known ? size_t{1} : size_t{0});
        }
        sink.release();
        if (tick >= 200 && tick % 40 == 0) sink.submit(answer(tick, bot_ant));
    });
    std::vector<sim::Command> clicks;
    person.runner().set_on_tick([&]() {
        const uint64_t tick = machine.current_tick();
        if (tick >= 200 && tick % 40 == 0) clicks.push_back(answer(tick, person_ant));
    });
    uint32_t now = 100000;
    host.start(now);
    person.start(now);
    uint32_t next_frame = now + first_frame_ms;
    uint32_t frames = 0;
    for (uint32_t elapsed = 0; elapsed < 45000; ++elapsed) {
        ++now;
        network.set_time(now);
        if (now % 2 == 0) host.update(now);                                             // the server's loop
        if (static_cast<int32_t>(now - next_frame) >= 0) {                              // the person's frames, 60 a second (17, 17 and 16 ms): never in step with the turns
            person.update(now);
            for (const sim::Command& c : clicks) person.submit(c);
            clicks.clear();
            next_frame += ++frames % 3 == 0 ? 16u : 17u;
        }
    }
    Lead out;
    out.hold = lag.hold_ticks();
    for (const auto& entry : bot_turn) {
        const auto his = person_turn.find(entry.first);
        if (his == person_turn.end()) continue;
        const int behind = static_cast<int>(his->second) - static_cast<int>(entry.second);
        out.least = std::min(out.least, behind);
        out.most = std::max(out.most, behind);
        ++out.events;
    }
    return out;
}

}  // namespace

void run_bot_lag_tests() {
    TEST_CASE("S3.285 The Hold Of A Room's Bots: The Round Trip In Whole Ticks (Halves Up, At Most 6), The Median Of The People's Readings, Nobody Connected Is 0 At Once, People With No Reading Leave The Hold Where It Was, A Band Of 10 ms Against Flicker, And One Second Between Two Changes") {
        ASSERT_EQ(bot_lag_ticks_for(0), 0u);
        ASSERT_EQ(bot_lag_ticks_for(24), 0u);
        ASSERT_EQ(bot_lag_ticks_for(25), 1u);
        ASSERT_EQ(bot_lag_ticks_for(74), 1u);
        ASSERT_EQ(bot_lag_ticks_for(75), 2u);
        ASSERT_EQ(bot_lag_ticks_for(125), 3u);
        ASSERT_EQ(bot_lag_ticks_for(274), 5u);
        ASSERT_EQ(bot_lag_ticks_for(275), 6u);
        ASSERT_EQ(bot_lag_ticks_for(300), 6u);
        ASSERT_EQ(bot_lag_ticks_for(100000), 6u);
        ASSERT_EQ(bot_lag_ticks_for(0xFFFFFFFFu), 6u);                                  // (no wrap in the sum)
        {
            const uint32_t none[1] = {0};
            ASSERT_EQ(bot_lag_median(none, 0), 0u);
            const uint32_t one[1] = {7};
            ASSERT_EQ(bot_lag_median(one, 1), 7u);
            const uint32_t two[2] = {31, 10};
            ASSERT_EQ(bot_lag_median(two, 2), 21u);                                      // the mean of the two in the middle, halves up
            const uint32_t three[3] = {5, 100, 9};
            ASSERT_EQ(bot_lag_median(three, 3), 9u);
            const uint32_t four[4] = {4, 1, 3, 2};
            ASSERT_EQ(bot_lag_median(four, 4), 3u);
        }
        {   // the median of the people, not their mean or their worst
            BotLag lag;
            feed(lag, 100, 3, {30, 100, 400});
            ASSERT_EQ(lag.hold_ticks(), 2u);
            BotLag two;
            feed(two, 100, 2, {30, 300});
            ASSERT_EQ(two.hold_ticks(), 3u);                                             // (30 + 300) / 2 = 165
            BotLag wild;
            feed(wild, 100, 3, {100, 100, 3000});
            ASSERT_EQ(wild.hold_ticks(), 2u);                                            // one reading far out of the way moves nothing
            BotLag capped;
            feed(capped, 100, 1, {5000});
            ASSERT_EQ(capped.hold_ticks(), kBotLagMaxTicks);                             // never more than 300 ms, whatever a person's machine says
            BotLag huge;
            feed(huge, 100, 1, {0xFFFFFFFFu});
            ASSERT_EQ(huge.hold_ticks(), kBotLagMaxTicks);                               // (and no wrap in the sums: a reading that is past any clock is the cap, not 0)
            BotLag edge;
            feed(edge, 100, 1, {284});
            ASSERT_EQ(edge.hold_ticks(), 5u);                                            // (the band: 285 is where the sixth tick begins)
            BotLag edge2;
            feed(edge2, 100, 1, {285});
            ASSERT_EQ(edge2.hold_ticks(), 6u);
        }
        {   // nobody connected: 0 at once, and the way up is free again
            BotLag lag;
            feed(lag, 100, 1, {200});
            ASSERT_EQ(lag.hold_ticks(), 4u);
            feed(lag, 105, 0, {});
            ASSERT_EQ(lag.hold_ticks(), 0u);                                             // (five ticks after the change: the dwell does not hold it)
            feed(lag, 106, 1, {200});
            ASSERT_EQ(lag.hold_ticks(), 4u);
        }
        {   // people are here and none can be read (a window that was hidden, a machine that says nothing): what was known stays for ten seconds, then there is no hold
            BotLag lag;
            feed(lag, 100, 1, {200});
            for (uint64_t t = 101; t <= 300; ++t) feed(lag, t, 2, {});                   // (the first tick without a reading is 101: 199 ticks later it is still 4)
            ASSERT_EQ(lag.hold_ticks(), 4u);
            feed(lag, 301, 2, {});
            ASSERT_EQ(lag.hold_ticks(), 0u);                                             // 200 ticks: no reading for ten seconds is no reading at all
            feed(lag, 302, 1, {200});
            ASSERT_EQ(lag.hold_ticks(), 4u);                                             // (and the way up is free again)
            BotLag again;                                                                // the ten seconds count from the last time that nobody could be read
            feed(again, 100, 1, {200});
            for (uint64_t t = 101; t < 250; ++t) feed(again, t, 1, {});
            feed(again, 250, 1, {200});                                                  // a reading, 149 ticks into the silence
            for (uint64_t t = 251; t <= 450; ++t) feed(again, t, 1, {});
            ASSERT_EQ(again.hold_ticks(), 4u);                                           // 200 ticks of a new silence, counted from 251: its 200th tick is the next one
            feed(again, 451, 1, {});
            ASSERT_EQ(again.hold_ticks(), 0u);
            BotLag fresh;
            feed(fresh, 100, 1, {});
            ASSERT_EQ(fresh.hold_ticks(), 0u);                                           // (nothing known yet: no hold)
        }
        {   // a reading that is past any clock does not wrap the sums, also when the hold is 6 already
            BotLag lag;
            feed(lag, 100, 1, {300});
            ASSERT_EQ(lag.hold_ticks(), kBotLagMaxTicks);
            feed(lag, 200, 1, {0xFFFFFFFFu});
            ASSERT_EQ(lag.hold_ticks(), kBotLagMaxTicks);
        }
        {   // the band against flicker, and the dwell
            BotLag lag;
            feed(lag, 100, 1, {34});
            ASSERT_EQ(lag.hold_ticks(), 0u);                                             // the first tick begins at 35, not at 25
            feed(lag, 101, 1, {35});
            ASSERT_EQ(lag.hold_ticks(), 1u);
            feed(lag, 140, 1, {20});
            ASSERT_EQ(lag.hold_ticks(), 1u);                                             // 20 is inside the band round 25: it stays
            feed(lag, 141, 1, {49});
            ASSERT_EQ(lag.hold_ticks(), 1u);
            feed(lag, 142, 1, {14});
            ASSERT_EQ(lag.hold_ticks(), 0u);                                             // under 15 it goes back
            feed(lag, 161, 1, {200});
            ASSERT_EQ(lag.hold_ticks(), 0u);                                             // 19 ticks after the last change: not yet
            feed(lag, 162, 1, {200});
            ASSERT_EQ(lag.hold_ticks(), 4u);                                             // 20: now
            for (uint64_t t = 163; t < 400; ++t) feed(lag, t, 1, {t % 2 == 0 ? 74u : 76u});
            ASSERT_EQ(lag.hold_ticks(), 2u);                                             // a reading that flickers over the boundary of 1 and 2 ticks moves it once, to 2, and 75 +- 1 does not move it again
        }
    } TEST_END();

    TEST_CASE("S3.286 The Sink Of A Bot's Seat: A Hold Of 0 Goes Straight Through (The Sink Of Before), A Hold Waits That Many Turns, The Order Of The Commands Is Kept When The Hold Falls Or Rises, None Is Lost Or Doubled, applied_At Names The Turn That Really Carries A Command, And A Match That Waits Keeps The Commands Where They Are") {
        {   // a hold of 0
            FakeLine f;
            LaggedSink sink(f.line());
            ASSERT_EQ(sink.applied_at(0), 10u);
            const sim::CommandResult r = sink.submit(numbered(1));
            ASSERT_TRUE(r.accepted());
            ASSERT_EQ(f.delivered, (std::vector<int>{1}));                               // at once, in the same call
            ASSERT_EQ(sink.held(), size_t{0});
            ASSERT_EQ(sink.held_total(), uint64_t{0});
            f.accepting = false;                                                         // the session refuses (a pause): the answer is the refusal's, as it always was
            ASSERT_FALSE(sink.submit(numbered(2)).accepted());
            ASSERT_EQ(f.delivered.size(), size_t{1});
        }
        {   // a hold of 3
            FakeLine f;
            f.hold = 3;
            LaggedSink sink(f.line());
            ASSERT_EQ(sink.applied_at(0), 13u);
            ASSERT_TRUE(sink.submit(numbered(1)).accepted());
            ASSERT_TRUE(f.delivered.empty() && sink.held() == 1);
            f.sealed = 12;
            sink.release();
            ASSERT_TRUE(f.delivered.empty());
            f.sealed = 13;
            sink.release();
            ASSERT_EQ(f.delivered, (std::vector<int>{1}));
            ASSERT_EQ(sink.held(), size_t{0});
            ASSERT_EQ(sink.held_total(), uint64_t{1});
            ASSERT_EQ(sink.longest_wait(), 3u);
        }
        {   // the hold falls: a later command does not pass an earlier one
            FakeLine f;
            f.hold = 3;
            LaggedSink sink(f.line());
            sink.submit(numbered(1));                                                    // due 13
            f.sealed = 11;
            f.hold = 0;
            ASSERT_EQ(sink.applied_at(0), 13u);                                          // (behind the one that waits: it is what a command now would get)
            sink.submit(numbered(2));
            sink.submit(numbered(3));
            ASSERT_TRUE(f.delivered.empty());
            ASSERT_EQ(sink.applied_at(0), 13u);                                          // (still behind the first one: the commands after it go with it)
            f.sealed = 13;
            sink.release();
            ASSERT_EQ(f.delivered, (std::vector<int>{1, 2, 3}));
            sink.submit(numbered(4));                                                    // the queue is empty and the hold 0: straight through again
            ASSERT_EQ(f.delivered, (std::vector<int>{1, 2, 3, 4}));
        }
        {   // a command that is handed over in the tick in which an older one is due, before the sink has been asked to release it, still goes behind it
            FakeLine f;
            f.hold = 2;
            LaggedSink sink(f.line());
            sink.submit(numbered(1));                                                    // due 12
            f.sealed = 12;
            f.hold = 0;
            sink.submit(numbered(2));
            ASSERT_TRUE(f.delivered.empty());
            sink.release();
            ASSERT_EQ(f.delivered, (std::vector<int>{1, 2}));
        }
        {   // the hold rises
            FakeLine f;
            f.hold = 1;
            LaggedSink sink(f.line());
            sink.submit(numbered(1));                                                    // due 11
            f.hold = 4;
            sink.submit(numbered(2));                                                    // due 14
            f.sealed = 11;
            sink.release();
            ASSERT_EQ(f.delivered, (std::vector<int>{1}));
            f.sealed = 13;
            sink.release();
            ASSERT_EQ(f.delivered, (std::vector<int>{1}));
            f.sealed = 14;
            sink.release();
            ASSERT_EQ(f.delivered, (std::vector<int>{1, 2}));
        }
        {   // 300 commands with the hold moving all the time: every one arrives once, in the order given
            FakeLine f;
            LaggedSink sink(f.line());
            int next = 0;
            for (int step = 0; step < 300; ++step) {
                f.hold = static_cast<uint32_t>((step / 7 + step % 5) % 7);               // 0 .. 6, in no order
                sink.release();
                sink.submit(numbered(next++));
                if (step % 3 == 0) sink.submit(numbered(next++));
                ++f.sealed;
            }
            for (int i = 0; i < 7; ++i) {
                ++f.sealed;
                sink.release();
            }
            ASSERT_EQ(sink.held(), size_t{0});
            ASSERT_EQ(f.delivered.size(), static_cast<size_t>(next));
            for (int i = 0; i < next; ++i) ASSERT_EQ(f.delivered[static_cast<size_t>(i)], i);
            ASSERT_TRUE(sink.longest_wait() <= kBotLagMaxTicks);
        }
        {   // applied_at is the turn that carries the command (the timed chains of a bot count on it)
            FakeLine f;
            f.hold = 2;
            LaggedSink sink(f.line());
            const uint64_t named = sink.applied_at(0);
            sink.submit(numbered(1));
            uint64_t sealed_when_delivered = 0;
            for (int i = 0; i < 6 && f.delivered.empty(); ++i) {
                ++f.sealed;
                sink.release();
                if (!f.delivered.empty()) sealed_when_delivered = f.sealed;
            }
            ASSERT_EQ(sealed_when_delivered, named);
        }
        {   // a session that refuses (a match that waits for a person who is away): a command is not taken, and the ones that wait keep their place
            FakeLine f;
            f.hold = 2;
            LaggedSink sink(f.line());
            ASSERT_TRUE(sink.submit(numbered(1)).accepted());
            f.accepting = false;
            ASSERT_FALSE(sink.submit(numbered(2)).accepted());                           // (refused as the session would)
            f.sealed = 50;
            sink.release();
            ASSERT_TRUE(f.delivered.empty() && sink.held() == 1);                        // it waits; the world does not move meanwhile
            f.accepting = true;
            sink.release();
            ASSERT_EQ(f.delivered, (std::vector<int>{1}));
        }
    } TEST_END();

    TEST_CASE("S3.287 A Person On A Link Of 40, 110, 200 And 300 ms Of Round Trip Plays A Bot That Waits 1, 2, 4 And 6 Ticks (800 ms Waits 6 Too); On A Link With No Delay It Waits None, And No Command Of Its Is Held; The Match Of Each Ends Identical On The Referee And The Person's Machine") {
        struct Case {
            const char* code;
            uint32_t one_way;
            uint32_t ticks;
        };
        for (const Case c : {Case{"LAG-0", 0, 0}, Case{"LAG-1", 20, 1}, Case{"LAG-2", 55, 2}, Case{"LAG-4", 100, 4}, Case{"LAG-6", 150, 6}, Case{"LAG-7", 400, 6}}) {
            LagRoom r(c.code, c.one_way);
            r.ann->record_hashes = true;
            r.w.run(kPre + 8000);
            RoomStatus s = r.w.status(c.code);
            ASSERT_TRUE(s.state == RoomState::Running && s.bot_controller && s.bot_decisions > 20);
            ASSERT_EQ(s.bot_lag_ticks, c.ticks);
            if (c.ticks == 0) {
                ASSERT_EQ(s.bot_held_commands, 0u);                                      // nothing waited: the sink of before
            } else {
                ASSERT_TRUE(s.bot_held_commands > 0);
                ASSERT_TRUE(s.bot_held_longest >= c.ticks && s.bot_held_longest <= kBotLagMaxTicks + 1);
            }
            r.w.play_to_the_end(c.code);
            s = r.w.status(c.code);
            ASSERT_TRUE(s.state == RoomState::Finished);
            ASSERT_FALSE(r.ann->session->desynced());
            ASSERT_TRUE(s.referee_hash != 0 && r.ann->hash_at.count(s.ticks) == 1);
            ASSERT_TRUE(r.ann->hash_at[s.ticks] == s.referee_hash);                       // the person's machine and the referee, the same state at the same tick
            ASSERT_TRUE(s.bot_held_longest <= kBotLagMaxTicks + 1);
        }
    } TEST_END();

    TEST_CASE("S3.288 Two People And A Bot: The Hold Follows The Median Of The Two Links (The Mean Of Two), Not The Farther One's; When The Farther One Is Dropped The Hold Is The Near One's") {
        World w;
        RoomSpec spec = spec_of("LAG-M", 3);
        spec.bots = {ai::BotSpec{2, "standard", ai::Level::Hard}};
        ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
        w.connect("Ann", "LAG-M", 255, net::LoopbackNetwork::Link{15, 0});              // 30 ms of round trip
        Client& bob = w.connect("Bob", "LAG-M", 255, net::LoopbackNetwork::Link{120, 0});     // 240 ms
        w.run(kPre + 9000);
        RoomStatus s = w.status("LAG-M");
        ASSERT_TRUE(s.state == RoomState::Running && s.bot_controller && s.joined == 3);
        ASSERT_EQ(s.bot_lag_ticks, 3u);                                                  // about (52 + 262) / 2 = 157 ms: between the two persons' own holds of 1 and 5
        w.net.cut(bob.end);                                                              // Bob's link is lost (a room that does not hold seats drops him)
        w.run(6000);
        s = w.status("LAG-M");
        ASSERT_TRUE(s.state == RoomState::Running);
        ASSERT_EQ(s.bot_lag_ticks, 1u);                                                  // Ann alone: 30 ms of round trip
    } TEST_END();

    TEST_CASE("S3.288b Teams And More Than One Bot: Every Person Counts Whichever Team They Play On (A Bot Allied To The Near Person Waits For The Far One Too), And Every Bot Of The Room Waits The Same") {
        {
            World w;
            RoomSpec spec = spec_of("LAG-T", 3);
            spec.bots = {ai::BotSpec{2, "standard", ai::Level::Hard}};
            spec.teams = sim::StartTeams{true, 0, 2};                                    // Ann (seat 0) and the bot (seat 2) against Bob
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            w.connect("Ann", "LAG-T", 255, net::LoopbackNetwork::Link{15, 0});
            w.connect("Bob", "LAG-T", 255, net::LoopbackNetwork::Link{120, 0});
            w.run(kPre + 9000);
            const RoomStatus s = w.status("LAG-T");
            ASSERT_TRUE(s.state == RoomState::Running && s.bot_controller && !s.teams.empty() && s.teams != "ffa");
            ASSERT_EQ(s.bot_lag_ticks, 3u);                                              // the same as the free-for-all room of the same two links
        }
        {
            World w;
            RoomSpec spec = spec_of("LAG-2B", 3);
            spec.bots = {ai::BotSpec{1, "standard", ai::Level::Hard}, ai::BotSpec{2, "standard", ai::Level::Hard}};
            ASSERT_TRUE(w.mgr.create_room(spec, w.now).ok);
            w.connect("Ann", "LAG-2B", 255, net::LoopbackNetwork::Link{55, 0});
            w.run(kPre + 9000);
            const RoomStatus s = w.status("LAG-2B");
            ASSERT_TRUE(s.state == RoomState::Running && s.bots.size() == 2);
            ASSERT_EQ(s.bot_lag_ticks, 2u);
            ASSERT_TRUE(s.bot_held_commands > 5 && s.bot_held_longest == 2);              // (the commands of both bots wait)
        }
    } TEST_END();

    TEST_CASE("S3.289 A Match That Waits For An Absent Person Holds Nothing (Nobody Is Connected) And Nothing Runs; The Person Comes Back And The Hold Is The Link's Again Within A Few Seconds; A Person Whose Machine Says Nothing For A While Leaves The Hold Where It Was; A Machine That Draws A Frame Every 400 ms Cannot Slow The Match (It Is Read At The Newest Turn Of Each Frame: Its Link Is What Counts)") {
        {   // a pause and a return
            LagRoom r("LAG-P", 45);
            r.w.run(kPre + 8000);
            RoomStatus s = r.w.status("LAG-P");
            ASSERT_TRUE(s.state == RoomState::Running);
            ASSERT_EQ(s.bot_lag_ticks, 2u);
            r.ann->reconnects = false;
            r.w.cut(*r.ann);
            r.w.run(2000);
            s = r.w.status("LAG-P");
            ASSERT_TRUE(s.paused);
            const uint32_t ticks_at_pause = s.ticks;
            const uint32_t held_at_pause = s.bot_held_commands;
            r.w.run(8000);
            s = r.w.status("LAG-P");
            ASSERT_TRUE(s.ticks <= ticks_at_pause + 2);                                  // nothing runs
            ASSERT_EQ(s.bot_lag_ticks, 0u);                                              // ... and nobody is connected: no hold (the person's link is what the hold is about)
            ASSERT_TRUE(s.bot_held_commands <= held_at_pause + 12);                      // (what waited when the pause began goes out when the match runs again, nothing new is made meanwhile)
            r.ann->reconnects = true;
            ASSERT_TRUE(r.w.until([&]() { return !r.w.status("LAG-P").paused; }, 8000));
            r.w.run(6000);
            s = r.w.status("LAG-P");
            ASSERT_TRUE(s.state == RoomState::Running && !s.paused && s.ticks > ticks_at_pause + 60);
            ASSERT_EQ(s.bot_lag_ticks, 2u);
            r.w.play_to_the_end("LAG-P");
            ASSERT_TRUE(r.w.status("LAG-P").state == RoomState::Finished);
            ASSERT_FALSE(r.ann->session->desynced());
        }
        {   // a machine that says nothing for 4 s (a window that is not run): the room does not stop, and the hold stays
            LagRoom r("LAG-H", 45);
            r.w.run(kPre + 8000);
            ASSERT_EQ(r.w.status("LAG-H").bot_lag_ticks, 2u);
            const uint32_t ticks = r.w.status("LAG-H").ticks;
            r.ann->hung = true;
            r.w.run(4000);
            const RoomStatus s = r.w.status("LAG-H");
            ASSERT_TRUE(s.state == RoomState::Running && s.ticks >= ticks + 60);        // (the referee goes on at its pace)
            ASSERT_EQ(s.bot_lag_ticks, 2u);
        }
        {   // a slow machine
            LagRoom r("LAG-S", 25);
            r.ann->frame_every_ms = 400;
            r.w.run(kPre + 10000);
            const RoomStatus s = r.w.status("LAG-S");
            ASSERT_TRUE(s.state == RoomState::Running);
            ASSERT_TRUE(s.ticks >= 170);                                                 // 10 s of play at 20 ticks a second: nothing here slows the match
        }
    } TEST_END();

    TEST_CASE("S3.291 A Second Window Takes A Person's Seat Over A Nearer Link While The First Window's Link Is Still Alive: The First Is Told So, The Match Goes On, And Half A Second Later The Hold Is The New Link's (What The Old Link Read Is Forgotten With It, Not Mixed In)") {
        LagRoom r("LAG-W", 150);                                                         // 300 ms of round trip
        r.w.run(kPre + 8000);
        ASSERT_EQ(r.w.status("LAG-W").bot_lag_ticks, 6u);
        const net::SeatKey key = r.ann->lobby->key();
        r.ann->reconnects = false;                                                       // (the first window stops when it is told that another took its seat)
        r.w.link = {20, 0};                                                              // the second window is 40 ms of round trip away
        RClient& again = r.w.connect("Ann", "LAG-W", 255, key);
        ASSERT_TRUE(r.w.until([&]() { return again.session != nullptr && again.session->mode() == net::ClientSession::Mode::Normal && !r.w.status("LAG-W").paused; }, 20000));
        r.w.run(450);
        ASSERT_TRUE(r.ann->was_rejected && r.ann->rejected == net::RejectReason::Superseded);
        RoomStatus s = r.w.status("LAG-W");
        ASSERT_TRUE(s.state == RoomState::Running && !s.paused);
        ASSERT_EQ(s.bot_lag_ticks, 1u);                                                  // three acknowledgements over the new link are a reading; eleven would be needed to outvote what the old link said
        r.w.play_to_the_end("LAG-W");
        ASSERT_TRUE(r.w.status("LAG-W").state == RoomState::Finished);
        ASSERT_FALSE(again.session->desynced());
    } TEST_END();

    TEST_CASE("S3.292 What The Hold Is For: A Person And A Bot Answer The Same Tick At Once. Without A Hold The Person's Order Is Carried 2, 3 And 6 Turns After The Bot's At 100, 180 And 300 ms Of Round Trip; With It, At 30 To 300 ms, Never After The Bot's And At Most One Turn Before It; Past The Cap (500 ms) The Bot Keeps What Is Beyond 300 ms") {
        struct Case {
            uint32_t one_way;
            int before;                                                                  // the least that the person is behind without a hold, in turns
        };
        for (const Case c : {Case{15, 0}, Case{50, 2}, Case{90, 3}, Case{150, 6}}) {
            for (const uint32_t first_frame : {3u, 9u, 14u}) {                           // (the person's frames fall differently against the turns)
                const Lead before = measure_lead(c.one_way, first_frame, false);
                ASSERT_TRUE(before.events >= 15 && before.hold == 0);
                ASSERT_TRUE(before.least >= c.before && before.most <= c.before + 1);    // the bot's lead: the person's round trip, in whole turns
                const Lead after = measure_lead(c.one_way, first_frame, true);
                ASSERT_TRUE(after.events >= 15);
                ASSERT_TRUE(static_cast<int>(after.hold) >= before.most && static_cast<int>(after.hold) <= before.least + 1);      // the hold covers the lead, and is at most one turn more (180 ms reads at the boundary of 3 and 4 ticks)
                ASSERT_TRUE(after.most <= 0);                                            // the person's order is never carried after the bot's
                ASSERT_TRUE(after.least >= -1);                                          // ... and the bot waits at most one turn more than the person is behind
            }
        }
        const Lead capped = measure_lead(250, 9, true);                                  // 500 ms of round trip
        ASSERT_EQ(capped.hold, kBotLagMaxTicks);
        ASSERT_TRUE(capped.events >= 15 && capped.least == 4 && capped.most == 4);       // ten turns behind, six of them made up
    } TEST_END();
}
