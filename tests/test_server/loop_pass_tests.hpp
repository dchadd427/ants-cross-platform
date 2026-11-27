// The tests of the server loop's slow-pass log (loop_pass.hpp, docs/SERVER.md "The slow-pass log"): a pass over the threshold is told in one line with its parts by name, the lines are rate-limited and
// the passes that were slow meanwhile are counted, and the room manager lays the time of a pass on its parts. Included by test_server_files.cpp after bot_queue_tests.hpp, whose fixtures it uses; test_server.cpp calls run_loop_pass_tests().
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
}
