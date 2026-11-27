// The tests of the server loop's slow-pass log (loop_pass.hpp, docs/SERVER.md "The slow-pass log"): a pass over the threshold is told in one line with its parts by name, the lines are rate-limited and
// the passes that were slow meanwhile are counted, the room manager lays the time of a pass on its parts, a due start of a match of computer players (its room, and its beginning in the room) waits for a pass with
// headroom while a match with people is in play. Included by test_server_files.cpp after bot_queue_tests.hpp, whose fixtures it uses; test_server.cpp calls run_loop_pass_tests().
#pragma once

#include "ants_server/loop_pass.hpp"

namespace {

// A pass whose clock is the test's: `parts` are given as the laps that the loop would make (microseconds each)
LoopPass timed_pass(int64_t& clock, const std::vector<std::pair<PassPart, int64_t>>& laps) {
    LoopPass pass([&clock]() { return clock; });
    pass.begin();
    for (const auto& lap : laps) {
        clock += lap.second;
        pass.lap(lap.first);
    }
    return pass;
}

bool line_has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

void run_loop_pass_tests() {
    TEST_CASE("S3.364 The Slow-Pass Log Tells A Pass Over 25 Ms In One Line With Its Total And Its Parts By Name (A Part Under 0.05 Ms Is Left Out), A Pass Of Exactly 25 Ms Or Less Is Not Told, And A Part That Was Moved To Another Keeps The Total") {
        int64_t clock = 0;
        SlowPassLog log;
        ASSERT_EQ(SlowPassLog::kThresholdUs, int64_t{25000});
        ASSERT_TRUE(log.end(1000, timed_pass(clock, {{PassPart::Rooms, 3000}, {PassPart::Store, 22000}})).empty());                                  // (exactly 25.0 ms: not over it)
        const std::string line = log.end(2000, timed_pass(clock, {{PassPart::Rooms, 5100}, {PassPart::Starts, 9800}, {PassPart::Store, 21000}, {PassPart::Doors, 49}, {PassPart::Other, 51}}));
        ASSERT_EQ(line, std::string("slow pass: 36.0 ms (rooms 5.1, starts 9.8, store 21.0, other 0.1)"));
        ASSERT_TRUE(line.find("doors") == std::string::npos);
        // a part that was moved: a room's flush was inside the rooms' time
        int64_t c2 = 0;
        LoopPass pass([&c2]() { return c2; });
        pass.begin();
        c2 += 30000;
        pass.lap(PassPart::Rooms);
        pass.move(PassPart::Rooms, PassPart::Flush, 4000);
        pass.move(PassPart::Rooms, PassPart::Starts, 900000);                          // (more than there is: what is there)
        ASSERT_TRUE(pass.us(PassPart::Rooms) == 0 && pass.us(PassPart::Flush) == 4000 && pass.us(PassPart::Starts) == 26000 && pass.total_us() == 30000);
        ASSERT_TRUE(pass.elapsed_us() == 30000);
        pass.begin();                                                                  // (a new pass starts from nothing)
        ASSERT_TRUE(pass.total_us() == 0 && pass.us(PassPart::Starts) == 0);
        // a line holds no name, no code and no address: its words are the parts' and the numbers
        for (const char c : line) ASSERT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == ',' || c == '.' || c == ':' || c == '(' || c == ')');
    } TEST_END();

    TEST_CASE("S3.365 The Slow-Pass Log Writes At Most One Line In Ten Seconds: The Slow Passes Between Are Counted And Told Together, With The Longest Of Them, When The Time Is Over (By A Pass That Is Quick Too); A Quiet Time Costs Nothing And The Next Slow Pass Is Told At Once; A Clock That Wrapped Round Changes Nothing") {
        for (const uint32_t start : {uint32_t{1000}, uint32_t{0xFFFFFFFFu} - 3000u}) {                  // (the second run crosses the wrap of the 32-bit clock)
            int64_t clock = 0;
            SlowPassLog log;
            const auto slow = [&](int64_t rooms_us) { return timed_pass(clock, {{PassPart::Rooms, rooms_us}}); };
            const auto fast = [&]() { return timed_pass(clock, {{PassPart::Rooms, 2000}}); };
            ASSERT_TRUE(line_has(log.end(start, slow(40000)), "slow pass: 40.0 ms (rooms 40.0)"));
            ASSERT_TRUE(log.end(start + 100, slow(30000)).empty());                    // (told with the next line, not now)
            ASSERT_TRUE(log.end(start + 2000, fast()).empty());
            ASSERT_TRUE(log.end(start + 5000, slow(60000)).empty());
            ASSERT_TRUE(log.end(start + 9999, slow(26000)).empty());
            const std::string summary = log.end(start + 10000, fast());                // (the time is over: a pass that is quick tells it)
            ASSERT_EQ(summary, std::string("slow pass: 3 more in the last 10 s, the longest 60.0 ms (rooms 60.0)"));
            ASSERT_TRUE(log.end(start + 10050, slow(33000)).empty());                  // (the next time begins: this pass waits for its summary)
            ASSERT_TRUE(log.end(start + 19999, fast()).empty());
            ASSERT_EQ(log.end(start + 20000, fast()), std::string("slow pass: 1 more in the last 10 s, the longest 33.0 ms (rooms 33.0)"));
            // nothing was slow for a while: the next slow pass is told at once
            ASSERT_TRUE(log.end(start + 40000, fast()).empty());
            ASSERT_TRUE(line_has(log.end(start + 40010, slow(27000)), "slow pass: 27.0 ms"));
        }
    } TEST_END();

    TEST_CASE("S3.366 The Room Manager Lays The Time Of A Pass On The Pass's Parts: The Store's Look, The History Feeder, The Rooms And The Rest Each Have Their Own, Together The Time Of The Pass Up To The Last Lap; Without A Pass Nothing Is Timed") {
        World w;
        int64_t reading = 0;
        LoopPass pass([&reading]() { return reading += 250; });                         // (every reading of the clock costs 250 microseconds)
        w.mgr.set_loop_pass(&pass);
        pass.begin();
        w.mgr.update(w.now);
        ASSERT_TRUE(pass.us(PassPart::Rooms) > 0 && pass.us(PassPart::Other) > 0);
        ASSERT_TRUE(pass.us(PassPart::Store) == 0 && pass.us(PassPart::History) == 0 && pass.us(PassPart::Bots) == 0 && pass.us(PassPart::Doors) == 0);       // (this world keeps no replays and has no doors)
        int64_t sum = 0;
        for (const int64_t p : pass.parts()) sum += p;
        ASSERT_TRUE(sum == pass.total_us() && pass.total_us() <= reading);
        w.mgr.set_loop_pass(nullptr);
        const int64_t before = reading;
        w.mgr.update(w.now + 10);
        ASSERT_EQ(reading, before);                                                    // (no pass: the clock is never read)
        // a world that keeps replays lays the store's time on its own part
        ReplayClock clock;
        World keeping;
        std::string why;
        ASSERT_TRUE(keeping.mgr.enable_replays(replay_config("looppass-store", &clock), false, why));
        LoopPass second([&reading]() { return reading += 250; });
        keeping.mgr.set_loop_pass(&second);
        second.begin();
        keeping.mgr.update(keeping.now);
        ASSERT_TRUE(second.us(PassPart::Store) > 0);
        keeping.mgr.set_loop_pass(nullptr);
    } TEST_END();

    TEST_CASE("S3.367 A Due Start Of A Match Of Computer Players Waits For A Pass That Has Headroom While A Match With People Is In Play (The Pass Has Spent Under 10 Ms), Is Made At Once When A Pass Has, Waits No Longer Than 5 Seconds Of Heavy Passes, And Is Never Held Back When Nobody Plays Or When No Pass Is Timed") {
        BotDoor d("looppass-headroom");
        int64_t clock = 0;
        LoopPass pass([&clock]() { return clock; });
        pass.begin();
        d.w.mgr.set_loop_pass(&pass);
        // nobody plays: a pass that has spent 50 ms already does not hold a start back
        clock = 50000;
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
        ASSERT_TRUE(d.w.mgr.create_room(spec_of("HR-P", 2), d.w.now).ok);
        d.w.connect("Ann", "HR-P");
        d.w.connect("Bob", "HR-P");
        clock = 0;
        ASSERT_TRUE(run_until(d.w, [&] { return d.w.status("HR-P").state == RoomState::Running; }, 15000));
        ASSERT_TRUE(d.w.mgr.people_in_play() && !d.w.mgr.history_may_run());
        // the pass has spent just under, and exactly, the headroom
        clock = kBotStartHeadroomUs - 1;
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
        clock = kBotStartHeadroomUs;
        ASSERT_FALSE(d.w.mgr.bot_start_headroom(d.w.now));
        clock = 0;
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
        // a batch with a spacing: the first match starts now (the admin's own call is answered at once), the next one is due after a minute
        ASSERT_EQ(d.post("bot-matches", spaced_body(3, 60)).status, 200);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 2);
        stagger_step(d, 59000);
        clock = kBotStartHeadroomUs;                                                   // (a heavy pass: the start that is due waits)
        stagger_step(d, 1000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 2);
        stagger_step(d, 1000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 2);
        clock = 0;                                                                     // (a pass with headroom: it starts at once)
        stagger_step(d, 10);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 1);
        // passes that are heavy for good: the start waits 5 seconds, not for ever
        stagger_step(d, 59000);
        clock = kBotStartHeadroomUs + 5000;
        stagger_step(d, 1000);                                                         // (the third is due now, and the passes are heavy)
        stagger_step(d, kBotStartPutOffMaxMs - 1);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 1);
        stagger_step(d, 1);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 3 && d.bots->waiting_count() == 0);
        // the match of people is over: a heavy pass holds nothing back; and without a pass nothing is asked
        d.post("bot-matches/stop-all");
        ASSERT_TRUE(d.w.mgr.close_room("HR-P", d.w.now));
        d.w.run(1000);
        ASSERT_FALSE(d.w.mgr.people_in_play());
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
        d.w.mgr.set_loop_pass(nullptr);
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
    } TEST_END();

    TEST_CASE("S3.368 The Wait Of A Start That Is Held Is Counted From The Pass In Which It Was First Held (Not From A Heavy Pass In Which Nothing Was Due), Ends When A Whole Pass Held No Start Or A Start Went Through, And Lets All The Starts That Are Due In The Pass In Which One Went Through By Its Wait Go With It") {
        BotDoor d("looppass-timer");
        int64_t clock = 0;
        LoopPass pass([&clock]() { return clock; });
        pass.begin();
        d.w.mgr.set_loop_pass(&pass);
        ASSERT_TRUE(d.w.mgr.create_room(spec_of("HT-P", 2), d.w.now).ok);
        d.w.connect("Ann", "HT-P");
        d.w.connect("Bob", "HT-P");
        ASSERT_TRUE(run_until(d.w, [&] { return d.w.status("HT-P").state == RoomState::Running; }, 15000));
        ASSERT_TRUE(d.w.mgr.people_in_play());
        const int64_t heavy = kBotStartHeadroomUs;
        const uint32_t wait_ms = kBotStartPutOffMaxMs;
        // a start is held, and a whole pass goes by in which none is: the wait is over, so the next start that is held waits its own time (not what is left of the first)
        clock = heavy;
        ASSERT_FALSE(d.w.mgr.bot_start_headroom(d.w.now));
        d.w.run(10);                                                                   // (the pass in which it was held)
        d.w.run(10);                                                                   // (a pass in which none was)
        d.w.run(wait_ms + 100);
        ASSERT_FALSE(d.w.mgr.bot_start_headroom(d.w.now));
        d.w.now += wait_ms - 1;
        ASSERT_FALSE(d.w.mgr.bot_start_headroom(d.w.now));
        // it goes at the end of its wait, and so does every other start that is due in that pass; the one of the pass after waits again
        d.w.now += 1;
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
        d.w.now += 1;
        ASSERT_FALSE(d.w.mgr.bot_start_headroom(d.w.now));
        d.w.now += wait_ms - 1;
        ASSERT_FALSE(d.w.mgr.bot_start_headroom(d.w.now));
        d.w.now += 1;
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
        // a start that goes in a light pass leaves nothing of its wait either
        d.w.now += 1;
        ASSERT_FALSE(d.w.mgr.bot_start_headroom(d.w.now));
        d.w.now += wait_ms - 1000;
        clock = 0;
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
        d.w.now += 1000;
        clock = heavy;
        ASSERT_FALSE(d.w.mgr.bot_start_headroom(d.w.now));
        clock = 0;
        ASSERT_TRUE(d.w.mgr.bot_start_headroom(d.w.now));
        clock = heavy;
        // the queue of a batch asks only for a start that is due: heavy passes in which nothing is due start no wait
        ASSERT_EQ(d.post("bot-matches", spaced_body(4, 60)).status, 200);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 3);
        for (int i = 0; i < 20; ++i) stagger_step(d, 1000);                            // (twenty heavy passes: none of them has a start that is due)
        stagger_step(d, 40000);                                                        // (the second start is due now)
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 3);
        stagger_step(d, wait_ms - 1);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 1 && d.bots->waiting_count() == 3);
        stagger_step(d, 1);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 2);
        // ... and the start that went through by its wait leaves nothing of it for the next one, which is due a minute later
        stagger_step(d, 59000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2);
        stagger_step(d, 1000);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 2);
        stagger_step(d, wait_ms - 1);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 2 && d.bots->waiting_count() == 2);
        stagger_step(d, 1);
        ASSERT_TRUE(d.w.mgr.bot_match_count() == 3 && d.bots->waiting_count() == 1);
        d.w.mgr.set_loop_pass(nullptr);
    } TEST_END();

    TEST_CASE("S3.369 The Beginning Of A Match Of Computer Players (Its Engine, Its Record, The Bots' First Look At The Map: Room::begin_match, In The Pass After Its Room Was Made) Is Held While A Match With People Is In Play And The Pass Has Spent 10 Ms, Is Made In The Next Pass That Has Headroom Or After 5 Seconds Of Heavy Passes, And Is Never Held When Nobody Plays") {
        BotDoor d("looppass-begin");
        int64_t clock = 0;
        LoopPass pass([&clock]() { return clock; });
        pass.begin();
        d.w.mgr.set_loop_pass(&pass);
        ASSERT_TRUE(d.w.mgr.create_room(spec_of("HB-P", 2), d.w.now).ok);
        d.w.connect("Ann", "HB-P");
        d.w.connect("Bob", "HB-P");
        ASSERT_TRUE(run_until(d.w, [&] { return d.w.status("HB-P").state == RoomState::Running; }, 15000));
        ASSERT_TRUE(d.w.mgr.people_in_play());
        const int64_t heavy = kBotStartHeadroomUs;
        const std::string one = batch_body(1, "TINY.LVL", "highest-score", "2");
        std::set<std::string> known;
        const auto fresh = [&]() {                                                     // (the code of the match that the last call made: its room is made at once, its match begins in a pass of the manager)
            for (const BotMatchInfo& m : d.w.mgr.bot_matches(d.w.now)) {
                if (known.insert(m.code).second) return m.code;
            }
            return std::string();
        };
        const auto state_of = [&](const std::string& code) {
            for (const BotMatchInfo& m : d.w.mgr.bot_matches(d.w.now)) {
                if (m.code == code) return m.state;
            }
            return RoomState::Failed;
        };
        // heavy passes: the room is made (the admin's call is answered at once) and its lobby has begun, but its match does not begin
        clock = heavy;
        ASSERT_EQ(d.post("bot-matches", one).status, 200);
        const std::string first = fresh();
        ASSERT_TRUE(!first.empty() && state_of(first) == RoomState::Waiting);
        d.w.run(10);
        ASSERT_TRUE(state_of(first) == RoomState::Loading);
        d.w.run(10);
        d.w.run(4000);
        ASSERT_TRUE(state_of(first) == RoomState::Loading);                            // (four seconds of heavy passes: still held)
        clock = heavy - 1;                                                             // (a pass that has spent 9.999 ms has headroom: the match begins in it)
        d.w.run(10);
        ASSERT_TRUE(state_of(first) == RoomState::Running);
        // the wait is over after 5 seconds of heavy passes: it begins whatever the pass
        clock = heavy;
        ASSERT_EQ(d.post("bot-matches", one).status, 200);
        const std::string second = fresh();
        ASSERT_TRUE(!second.empty() && second != first);
        d.w.run(10);
        d.w.run(10);                                                                   // (the beginning is due, and held from here)
        d.w.run(kBotStartPutOffMaxMs - 120);
        ASSERT_TRUE(state_of(second) == RoomState::Loading);
        d.w.run(130);
        ASSERT_TRUE(state_of(second) == RoomState::Running);
        // nobody plays: a heavy pass holds nothing
        ASSERT_TRUE(d.w.mgr.close_room("HB-P", d.w.now));
        d.w.run(100);
        ASSERT_FALSE(d.w.mgr.people_in_play());
        ASSERT_EQ(d.post("bot-matches", one).status, 200);
        const std::string third = fresh();
        ASSERT_TRUE(!third.empty() && state_of(third) == RoomState::Waiting);
        d.w.run(30);
        ASSERT_TRUE(state_of(third) == RoomState::Running);
        d.w.mgr.set_loop_pass(nullptr);
    } TEST_END();
}
