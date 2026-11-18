// Tests of the network handicap of a room's computer players (docs/BOTS.md, "Fairness in detail"; ants_server/bot_lag.hpp): the hold that the people's links give (its rounding, cap, median, hysteresis and
// dwell), the sink that holds a bot's commands back (order, no loss, the pass-through at a hold of 0, the turn that applied_at names), and whole rooms: a person on a link of 30, 90, 180 and 300 ms
// round trip gets a bot that waits 1, 2, 4 and 6 ticks; a person on a LAN link, or nobody, a bot that does not; two people give the median; a pause, a slow machine and a person who leaves leave the room
// right; and a match with held commands plays out on a fresh engine to the same hashes (the replay case, S3.290, is in replay_tests.hpp, and a match of bots alone holds nothing: S3.180).
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
        {   // people are here and none can be read (a pause, a window that was hidden): what was known stays
            BotLag lag;
            feed(lag, 100, 1, {200});
            for (uint64_t t = 101; t < 400; ++t) feed(lag, t, 2, {});
            ASSERT_EQ(lag.hold_ticks(), 4u);
            BotLag fresh;
            feed(fresh, 100, 1, {});
            ASSERT_EQ(fresh.hold_ticks(), 0u);                                           // (nothing known yet: no hold)
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

    TEST_CASE("S3.289 A Match That Waits For An Absent Person Holds Nothing (Nobody Is Connected) And Nothing Runs; The Person Comes Back And The Hold Is The Link's Again Within A Few Seconds; A Person Whose Machine Says Nothing For A While Leaves The Hold Where It Was; A Machine That Draws A Frame Every 400 ms Holds Nothing Past The Cap And The Match Keeps Its Pace") {
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
            ASSERT_TRUE(s.bot_lag_ticks <= kBotLagMaxTicks);                             // (a machine that runs several turns in a frame is read at the newest of them: its link is what counts, not its frame rate)
            ASSERT_TRUE(s.ticks >= 170);                                                 // 10 s of play at 20 ticks a second: nothing here slows the match
        }
    } TEST_END();
}
