// Tests of the jitter buffer rule (ants_net/jitter.hpp) and of the way the lock-step runner uses it (ants_net/lockstep.hpp): ONE turn of buffer (50 ms) on a steady link, more
// where the jitter needs it (up to four), grown at once after a stall, shrunk slowly (a turn per ten seconds), rebuilt after a stall, and the queue that a bunch of turns leaves
// run down at up to four times normal speed. The rule is pure: every test here feeds it made-up read times.
#include "ants_net/jitter.hpp"
#include "ants_net/lockstep.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace ants;
using namespace ants::net;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

namespace {

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    uint32_t below(uint32_t n) {
        s = s * 1664525u + 1013904223u;
        return n == 0 ? 0u : (s >> 8) % n;
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------
// The rule alone: made-up arrivals. The host seals turn k at k * 50 ms; a link gives each turn `base + 0 .. jitter` (in order: a turn never arrives before the one before it);
// a frame loop of 60 Hz reads what has arrived.
// ---------------------------------------------------------------------------------------------------------------------------------

struct Feed {
    uint32_t base{40};
    uint32_t jitter{0};
    uint32_t spike_every{0};                                      // one turn in this many is held up by spike_ms (random)
    uint32_t spike_ms{0};
    std::vector<std::pair<uint32_t, uint32_t>> freezes;           // [from, to): nothing arrives; what is held comes at `to` in a bunch
    uint32_t seed{1};
    uint32_t frame_x10{167};                                      // 16.7 ms
    uint32_t hitch_from{0};                                       // from this time on frames are `hitch_frame_ms` long (0: never)
    uint32_t hitch_frame_ms{0};
};

struct Run {
    std::vector<uint32_t> target_each_second;                     // the target at every full second of the run
    std::vector<uint32_t> changes_at;                             // the times at which the target changed
    uint32_t final_target{1};
    uint32_t max_target{1};
    uint32_t final_lateness{0};
};

Run feed_rule(JitterBuffer& jb, const Feed& f, uint32_t duration_ms, const std::vector<uint32_t>& stalls_at = {}) {
    Lcg rng(f.seed);
    std::vector<uint32_t> arrive;
    uint32_t last = 0;
    for (uint32_t k = 0; static_cast<uint64_t>(k) * kTurnMs < duration_ms + 5000; ++k) {
        uint32_t d = f.base + rng.below(f.jitter + 1);
        if (f.spike_every != 0 && rng.below(f.spike_every) == 0) d += f.spike_ms;
        uint32_t at = std::max(k * kTurnMs + d, last);
        for (const auto& fr : f.freezes) {
            if (at >= fr.first && at < fr.second) at = fr.second;
        }
        last = at;
        arrive.push_back(at);
    }
    Run run;
    size_t next = 0;
    double frame_t = 0.0;
    uint32_t last_frame = 0;
    size_t stall_i = 0;
    uint32_t prev_target = jb.target();
    uint32_t second = 0;
    for (uint32_t now = 0; now < duration_ms; ++now) {
        if (static_cast<double>(now) >= frame_t) {
            const uint32_t dt = now - last_frame;
            last_frame = now;
            const size_t first = next;
            while (next < arrive.size() && arrive[next] <= now) ++next;
            if (next > first) jb.on_arrivals(static_cast<uint32_t>(first), static_cast<uint32_t>(next - first), now, dt);
            while (stall_i < stalls_at.size() && stalls_at[stall_i] <= now) {
                jb.on_stall(now);
                ++stall_i;
            }
            const double frame_len = (f.hitch_from != 0 && now >= f.hitch_from) ? f.hitch_frame_ms : f.frame_x10 / 10.0;
            frame_t += frame_len;
            if (frame_t < now + 1.0) frame_t = now + frame_len;
        }
        if (jb.target() != prev_target) {
            run.changes_at.push_back(now);
            prev_target = jb.target();
        }
        run.max_target = std::max(run.max_target, jb.target());
        if (now / 1000 >= second) {
            run.target_each_second.push_back(jb.target());
            ++second;
        }
    }
    run.final_target = jb.target();
    run.final_lateness = jb.lateness_ms();
    return run;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The runner with the rule: a frame loop of 60 Hz, the real turn queue and engine
// ---------------------------------------------------------------------------------------------------------------------------------

struct Drive {
    sim::SimulationEngine sim;
    LockstepRunner runner;
    std::vector<uint32_t> tick_times;
    uint32_t now{0};
    uint32_t next_turn{0};
    uint32_t stall_episodes{0};
    uint32_t last_stall_end{0};
    bool in_stall{false};
    uint32_t longest_stall_ms{0};

    explicit Drive(LockstepRunner::Config cfg = {}) : runner((sim.init_test_world(40, 40, 1, 720000), sim), cfg) {
        runner.set_on_tick([this]() { tick_times.push_back(now); });
    }

    // a frozen window: `real_ms` pass, the frame is told `told_ms` of them (the application hands the network at most a second); the turns that arrived meanwhile are queued first
    void freeze(uint32_t real_ms, uint32_t told_ms, const std::function<uint32_t(uint32_t)>& arrival) {
        now += real_ms;
        while (arrival(next_turn) <= now) {
            TurnMsg m;
            m.turn = next_turn++;
            runner.on_turn(m);
        }
        runner.update(told_ms);
        tick_times.push_back(now);
    }

    // a frame of `dt` ms: turns whose arrival time (given by `arrival(k)`) has passed are queued first
    void frame(uint32_t dt, const std::function<uint32_t(uint32_t)>& arrival) {
        now += dt;
        while (arrival(next_turn) <= now) {
            TurnMsg m;
            m.turn = next_turn++;
            runner.on_turn(m);
        }
        runner.update(dt);
        const uint32_t waited = runner.stalled_ms();
        if (waited > 0 && !in_stall) {
            in_stall = true;
            ++stall_episodes;
        }
        if (waited == 0 && in_stall) in_stall = false;
        longest_stall_ms = std::max(longest_stall_ms, waited);
    }
};

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: the jitter buffer (one turn on a steady link, up to four, grown at once, shrunk slowly)\n"
                 "=======================================================\n";

    TEST_CASE("J1.1 The Rule's Numbers: One Turn Of 50 ms To Four, A Margin Of 10 ms, The 95th Percentile Of The Last 200 Turns, Ten Seconds Per Step Down; A Turn Is 50 ms And One Tick") {
        JitterBuffer jb;
        const JitterBuffer::Config& c = jb.config();
        ASSERT_TRUE(c.turn_ms == 50 && c.min_turns == 1 && c.max_turns == 4 && c.margin_ms == 10 && c.percentile == 95 && c.shrink_after_ms == 10000 && c.hitch_ms == 100);
        ASSERT_TRUE(JitterBuffer::kSamples == 200 && JitterBuffer::kMinSamples == 20);
        ASSERT_EQ(jb.target(), 1u);
        ASSERT_EQ(jb.wanted(), 1u);
        ASSERT_EQ(jb.lateness_ms(), 0u);
        ASSERT_TRUE(kTurnMs == 50 && kTicksPerTurn == 1 && LockstepRunner::kTickMs == 50);
        JitterBuffer::Config odd;                                       // nonsense is made sane: a maximum below the minimum, a percentile of 0 or 1000
        odd.min_turns = 3;
        odd.max_turns = 1;
        odd.percentile = 1000;
        JitterBuffer fixed(odd);
        ASSERT_TRUE(fixed.target() == 3 && fixed.config().max_turns == 3 && fixed.config().percentile == 100);
    } TEST_END();

    TEST_CASE("J1.2 A Steady Link Gives One Turn And Keeps It: Frames Of 60 Hz, 30 Hz And 144 Hz Do Not Move It, And Nothing Is Said About The Link Before A Second Of Turns Has Been Read") {
        for (const uint32_t frame_x10 : {167u, 333u, 69u}) {
            for (const uint32_t jitter : {0u, 5u}) {
                JitterBuffer jb;
                Feed f;
                f.jitter = jitter;
                f.frame_x10 = frame_x10;
                const Run r = feed_rule(jb, f, 60000);
                ASSERT_EQ(r.final_target, 1u);
                ASSERT_EQ(r.max_target, 1u);                              // never grew, not for a moment
                ASSERT_TRUE(r.changes_at.empty());
                ASSERT_TRUE(r.final_lateness <= frame_x10 / 10 + jitter + 1);     // what it sees is the frame and the jitter
            }
        }
        JitterBuffer jb;                                                // fewer than 20 turns read say nothing: even a bunch of 19 turns is not "lateness"
        jb.on_arrivals(0, 19, 5000, 17);
        ASSERT_EQ(jb.lateness_ms(), 0u);
        ASSERT_EQ(jb.target(), 1u);
        jb.on_arrivals(19, 1, 5017, 17);
        ASSERT_TRUE(jb.lateness_ms() > 0);                              // the twentieth: now it counts
    } TEST_END();

    TEST_CASE("J1.3 A Jittery Link Grows The Buffer, At Once And To What The Jitter Needs, And Keeps It While The Jitter Lasts: 0 - 30 ms Needs Two Turns At Most, 0 - 60 Two, 0 - 120 Three, 0 - 200 Four") {
        struct Case {
            uint32_t jitter;
            uint32_t at_least;
            uint32_t at_most;
        };
        for (const Case c : {Case{30, 1, 2}, Case{60, 2, 3}, Case{120, 3, 4}, Case{200, 4, 4}}) {
            for (uint32_t seed = 1; seed <= 4; ++seed) {
                JitterBuffer jb;
                Feed f;
                f.jitter = c.jitter;
                f.seed = seed;
                const Run r = feed_rule(jb, f, 60000);
                ASSERT_TRUE(r.final_target >= c.at_least && r.final_target <= c.at_most);
                ASSERT_TRUE(r.max_target == r.final_target);              // it did not oscillate: it grew and stayed
                // grown at once: within the first 3 s (a second of turns for the first look at the link, and the growth is the same call)
                ASSERT_TRUE(r.target_each_second.size() > 3 && r.target_each_second[3] >= c.at_least);
            }
        }
    } TEST_END();

    TEST_CASE("J1.4 A Stall Grows The Buffer By A Turn At Once, Whatever The Numbers Say; Two Stalls Two; Never Beyond Four") {
        JitterBuffer jb;
        Feed f;
        const Run r = feed_rule(jb, f, 10000, {3000, 3010, 3020, 3030, 3040});    // five stalls in a row on a link that is steady
        ASSERT_EQ(r.max_target, 4u);
        ASSERT_EQ(jb.target(), 4u);
        JitterBuffer one;
        one.on_stall(100);
        ASSERT_EQ(one.target(), 2u);
        one.on_stall(200);
        ASSERT_EQ(one.target(), 3u);
        one.on_stall(300);
        ASSERT_EQ(one.target(), 4u);
        one.on_stall(400);
        ASSERT_EQ(one.target(), 4u);
    } TEST_END();

    TEST_CASE("J1.5 The Buffer Shrinks Slowly: A Turn Per Ten Seconds Without A Stall, Once The Lateness Asks For Less; Never Two Steps Together, Never Below One") {
        JitterBuffer jb;
        Feed f;
        f.freezes = {{5000, 6000}};                                     // the link froze for a second at 5 s: a bunch of twenty turns, up to a second late
        const Run r = feed_rule(jb, f, 60000);
        ASSERT_EQ(r.target_each_second[4], 1u);
        ASSERT_EQ(r.target_each_second[7], 4u);                         // grown at once (when the frame that read the bunch ran, at 6.0 s): the lateness of the bunch asks for the most
        ASSERT_EQ(r.target_each_second[14], 4u);                        // while the bunch is among the last 200 turns (10 s of play) nothing changes
        // the bunch left the last 200 turns at 16 s; one step down ten seconds after the growth, the next ten seconds later, and so on
        ASSERT_TRUE(r.changes_at.size() == 4);                          // 1 -> 4, then 3, 2, 1
        for (size_t i = 2; i < r.changes_at.size(); ++i) ASSERT_TRUE(r.changes_at[i] - r.changes_at[i - 1] >= 10000 && r.changes_at[i] - r.changes_at[i - 1] <= 10100);
        ASSERT_TRUE(r.changes_at[1] >= 15000 && r.changes_at[1] <= 17000);
        ASSERT_EQ(r.target_each_second[17], 3u);
        ASSERT_EQ(r.target_each_second[27], 2u);
        ASSERT_EQ(r.target_each_second[37], 1u);
        ASSERT_EQ(r.final_target, 1u);
        // a stall in the middle of that (the buffer is down to three turns then) takes it back to four and holds the shrink back for ten seconds from the stall
        JitterBuffer jb2;
        const Run held = feed_rule(jb2, f, 60000, {20000});
        ASSERT_EQ(held.target_each_second[19], 3u);
        ASSERT_EQ(held.target_each_second[21], 4u);
        ASSERT_EQ(held.target_each_second[29], 4u);
        uint32_t next_step = 0;
        for (const uint32_t at : held.changes_at) {
            if (at > 20100) {
                next_step = at;
                break;
            }
        }
        ASSERT_TRUE(next_step >= 30000 && next_step <= 30100);          // ten seconds from the stall, not from the earlier step
    } TEST_END();

    TEST_CASE("J1.9 A Stall Grows The Buffer Only When One More Turn Would Have Bridged It (a late turn within 100 ms); A Longer One (A Lost Packet's Resend) Is The Price And Grows Nothing; One Of 1.5 s Starts The Measuring Afresh") {
        JitterBuffer jb;
        jb.on_stall(1000, 60);                                          // the late turn came 60 ms after it was due: one more turn
        ASSERT_EQ(jb.target(), 2u);
        jb.on_stall(2000, 100);                                         // 100 ms: still
        ASSERT_EQ(jb.target(), 3u);
        jb.on_stall(3000, 101);                                         // 101 ms: no buffer here bridges it
        ASSERT_EQ(jb.target(), 3u);
        jb.on_stall(4000, 400);
        ASSERT_EQ(jb.target(), 3u);
        // a stall that is not told how long (0) is a short one
        JitterBuffer unknown;
        unknown.on_stall(100);
        ASSERT_EQ(unknown.target(), 2u);
        // the shrink clock is the last growth: a stall that grew nothing does not hold the shrink back
        JitterBuffer clock;
        clock.on_stall(1000, 50);                                       // 1 -> 2 at 1 s
        uint32_t two_until = 0;
        for (uint32_t k = 0; k < 600; ++k) {                            // a steady link from 1.1 s on
            const uint64_t now = 1100 + 50ull * k;
            if (now == 9000 || (now > 9000 && now < 9050)) clock.on_stall(now, 300);       // a lost packet at 9 s: nothing
            clock.on_arrivals(k, 1, now, 17);
            if (clock.target() == 2) two_until = static_cast<uint32_t>(now);
        }
        ASSERT_TRUE(two_until >= 10900 && two_until <= 11000);          // one step down ten seconds after the growth at 1 s, as if nothing had happened at 9 s
        ASSERT_EQ(clock.target(), 1u);
        // a wait of 1.5 s forgets what was read before it: a bunch of old samples that said "late" is gone, and the new link says "steady"
        JitterBuffer reset;
        for (uint32_t k = 0; k < 100; ++k) reset.on_arrivals(k, 1, 40 + k * 50 + (k % 10) * 20, 17);       // jitter of 180 ms: wants 4
        ASSERT_EQ(reset.target(), 4u);
        ASSERT_TRUE(reset.lateness_ms() > 100);
        reset.on_stall(6000, 1500);
        ASSERT_EQ(reset.lateness_ms(), 0u);
        ASSERT_EQ(reset.target(), 4u);                                  // (the buffer itself shrinks at its own pace: a step per ten seconds)
        for (uint32_t k = 120; k < 300; ++k) reset.on_arrivals(k, 1, 40 + k * 50 + 3000, 17);          // after the pause the link is steady, 3 s further on
        ASSERT_TRUE(reset.lateness_ms() <= 3);
        ASSERT_EQ(reset.wanted(), 1u);
    } TEST_END();

    TEST_CASE("J1.10 A Freeze Of The Link Is Not Jitter: The Turns That Come In A Bunch After A Wait Of 1.5 s Or More Are The Freeze's Own Lateness And Are Not Counted (The Buffer Does Not Sit At Four Turns For Half A Minute After One Freeze); A Jittery Link Keeps What It Needs") {
        // a steady link, then a freeze of 3 s: the wait is told when the late turn comes (the bunch of 60 turns is read in the same frame)
        {
            JitterBuffer jb;
            for (uint32_t k = 0; k < 300; ++k) jb.on_arrivals(k, 1, 40 + k * 50, 17);              // 15 s, steady: one turn
            ASSERT_EQ(jb.target(), 1u);
            const uint64_t at = 40 + 300 * 50 + 3000;                                               // turn 300 .. 359 were held for the freeze: they come together
            jb.on_stall(at, 3000);
            jb.on_arrivals(300, 60, at, 17);
            ASSERT_EQ(jb.target(), 1u);                                                             // no growth: no buffer bridges 3 s
            ASSERT_EQ(jb.lateness_ms(), 0u);                                                        // and the bunch is not a measure of the link
            // the frames that follow read the turns on time: the link is steady, and so the rule says
            for (uint32_t k = 360; k < 700; ++k) jb.on_arrivals(k, 1, 40 + k * 50 + 3000, 17);
            ASSERT_EQ(jb.target(), 1u);
            ASSERT_TRUE(jb.lateness_ms() <= 3);
        }
        // without the wait having been told (a bunch of 60 turns from nowhere), the same bunch is counted as before: it IS a measure of what the link did
        {
            JitterBuffer jb;
            for (uint32_t k = 0; k < 300; ++k) jb.on_arrivals(k, 1, 40 + k * 50, 17);
            jb.on_arrivals(300, 60, 40 + 300 * 50 + 3000, 17);
            ASSERT_EQ(jb.target(), 4u);
        }
        // a freeze of 400 ms or more (1000 here: 20 turns held) is left out like a longer one, but nothing that was read before it is forgotten (that takes 1.5 s)
        {
            JitterBuffer jb;
            for (uint32_t k = 0; k < 300; ++k) jb.on_arrivals(k, 1, 40 + k * 50 + (k % 3) * 20, 17);        // jitter of 40 ms: two turns at most
            const uint32_t lateness_before = jb.lateness_ms();
            const uint32_t target_before = jb.target();
            ASSERT_TRUE(lateness_before >= 20 && target_before <= 2);
            const uint64_t at = 40 + 300 * 50 + 1000;
            jb.on_stall(at, 1000);
            jb.on_arrivals(300, 20, at, 17);
            ASSERT_EQ(jb.lateness_ms(), lateness_before);                                                    // not counted, not forgotten
            ASSERT_EQ(jb.target(), target_before);
        }
        // a wait of less than 400 ms is counted as it always was: three stalls of 300 ms (bunches of six turns) within ten seconds are jitter, and the buffer grows to what they need
        {
            JitterBuffer jb;
            for (uint32_t k = 0; k < 300; ++k) {
                const uint64_t now = 40 + k * 50;
                if (k == 100 || k == 140 || k == 180) {                                                       // a stall of 300 ms: six turns come together
                    jb.on_stall(now + 300, 300);
                    jb.on_arrivals(k, 6, now + 300, 17);
                    k += 5;
                } else {
                    jb.on_arrivals(k, 1, now, 17);
                }
            }
            ASSERT_EQ(jb.target(), 4u);
        }
        // the skip lasts half a second of the rule's clock (a bunch that reaches the runner in two frames), then everything is counted again
        {
            JitterBuffer jb;
            for (uint32_t k = 0; k < 300; ++k) jb.on_arrivals(k, 1, 40 + k * 50, 17);
            const uint64_t at = 40 + 300 * 50 + 3000;
            jb.on_stall(at, 3000);
            jb.on_arrivals(300, 40, at, 17);                                                        // the first part of the bunch
            jb.on_arrivals(340, 20, at + 17, 17);                                                   // and the rest, a frame later
            ASSERT_EQ(jb.lateness_ms(), 0u);
            ASSERT_EQ(jb.target(), 1u);
            for (uint32_t k = 360; k < 400; ++k) jb.on_arrivals(k, 1, at + 600 + (k - 360) * 50 + (k % 4) * 60, 17);     // jitter of 180 ms, from 0.6 s after the stall on
            ASSERT_TRUE(jb.lateness_ms() >= 100);                                                   // counted again
            ASSERT_TRUE(jb.target() >= 3);
        }
        // a jittery link that freezes once keeps the buffer that its jitter needs (the freeze's wait forgets what was read, the jitter after it is read again)
        {
            JitterBuffer jb;
            for (uint32_t k = 0; k < 300; ++k) jb.on_arrivals(k, 1, 40 + k * 50 + (k % 4) * 60, 17);
            const uint32_t wanted_before = jb.target();
            ASSERT_TRUE(wanted_before >= 3);
            const uint64_t at = 40 + 300 * 50 + 3000;
            jb.on_stall(at, 3000);
            jb.on_arrivals(300, 60, at, 17);
            for (uint32_t k = 360; k < 700; ++k) jb.on_arrivals(k, 1, 40 + k * 50 + 3000 + (k % 4) * 60, 17);
            ASSERT_TRUE(jb.target() >= wanted_before);                                              // (the jitter went on: the buffer stays)
        }
    } TEST_END();

    TEST_CASE("J1.6 One Spike In Twenty Seconds Is Not A Reason For A Bigger Buffer (the 95th percentile ignores it) But A Stall Of It Grows The Buffer By One; Spikes Every Few Seconds Are") {
        {   // one lost packet's worth: a bunch of turns held up by 300 ms, 3 % of the last 200 turns
            JitterBuffer jb;
            Feed f;
            f.freezes = {{10000, 10300}};
            const Run r = feed_rule(jb, f, 30000);
            ASSERT_EQ(r.max_target, 1u);
        }
        {   // the same, and the runner stalled for it: one turn more, for ten seconds
            JitterBuffer jb;
            Feed f;
            f.freezes = {{10000, 10300}};
            const Run r = feed_rule(jb, f, 30000, {10200});
            ASSERT_EQ(r.max_target, 2u);
            ASSERT_EQ(r.final_target, 1u);
        }
        {   // a spike of 300 ms every 7 s (1 turn in 140): it is in six turns of the last 200 most of the time: 5 % and more
            JitterBuffer jb;
            Feed f;
            f.jitter = 10;
            f.spike_every = 25;
            f.spike_ms = 300;
            const Run r = feed_rule(jb, f, 60000);
            ASSERT_TRUE(r.max_target >= 3);
        }
    } TEST_END();

    TEST_CASE("J1.7 Frames That Stood For More Than 100 ms Are Not The Link's Lateness (a hitch, a window in the background): The Buffer Stays One Turn; The First Update Of A Runner Is Not Counted Either") {
        JitterBuffer jb;
        Feed f;
        f.hitch_from = 5000;                                            // from 5 s the frames are 400 ms apart: every frame reads eight turns at once
        f.hitch_frame_ms = 400;
        const Run r = feed_rule(jb, f, 40000);
        ASSERT_EQ(r.max_target, 1u);
        ASSERT_TRUE(jb.lateness_ms() <= 20);                            // nothing was counted after 5 s (the frames of 400 ms read eight turns each); before, a steady link
        // the runner: a bunch of turns that waited for the first update says nothing
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 1, 720000);
        LockstepRunner runner(sim);
        for (uint32_t k = 0; k < 40; ++k) {
            TurnMsg m;
            m.turn = k;
            runner.on_turn(m);
        }
        runner.update(16);
        ASSERT_EQ(runner.buffer_turns(), 1u);
        ASSERT_EQ(runner.jitter().lateness_ms(), 0u);
    } TEST_END();

    TEST_CASE("J1.8 The Rule Is A Function Of What It Is Told: The Same Arrivals Give The Same Targets Every Time, And A Rule With Another Range Keeps To It") {
        for (uint32_t seed = 1; seed <= 3; ++seed) {
            Feed f;
            f.jitter = 90;
            f.spike_every = 60;
            f.spike_ms = 250;
            f.seed = seed;
            JitterBuffer a;
            JitterBuffer b;
            const Run ra = feed_rule(a, f, 40000, {12000});
            const Run rb = feed_rule(b, f, 40000, {12000});
            ASSERT_TRUE(ra.target_each_second == rb.target_each_second && ra.changes_at == rb.changes_at);
        }
        JitterBuffer::Config cfg;
        cfg.min_turns = 2;
        cfg.max_turns = 3;
        JitterBuffer ranged(cfg);
        ASSERT_EQ(ranged.target(), 2u);                                 // a steady link: the minimum
        Feed rough;
        rough.jitter = 300;
        const Run r = feed_rule(ranged, rough, 30000, {5000, 6000, 7000});
        ASSERT_EQ(r.max_target, 3u);                                    // the maximum
        JitterBuffer::Config fixed_cfg;
        fixed_cfg.min_turns = fixed_cfg.max_turns = 2;                  // min == max: a fixed buffer
        JitterBuffer fixed(fixed_cfg);
        const Run rf = feed_rule(fixed, rough, 30000, {5000});
        ASSERT_TRUE(rf.max_target == 2 && rf.changes_at.empty());
    } TEST_END();

    TEST_CASE("J2.1 The Runner Waits For The Buffer It Needs: It Starts When Target + 1 Turns Are Queued, A Stall Grows The Target By A Turn And It Collects The Buffer Again Before It Goes On") {
        Drive d;
        std::vector<uint32_t> arrived;
        auto steady = [](uint32_t k) { return k * kTurnMs + 40; };
        // the start: two turns (target 1 + the one that runs)
        uint32_t first_tick = 0;
        for (uint32_t i = 0; i < 40 && first_tick == 0; ++i) {
            d.frame(17, steady);
            if (!d.tick_times.empty()) first_tick = d.tick_times[0];
        }
        ASSERT_TRUE(first_tick >= 40 + kTurnMs && first_tick <= 40 + kTurnMs + 17);       // when turn 1 had arrived
        ASSERT_EQ(d.runner.buffer_turns(), 1u);
        for (uint32_t i = 0; i < 300; ++i) d.frame(17, steady);                           // 5 s of a steady link: no wait, no growth
        ASSERT_EQ(d.stall_episodes, 0u);
        ASSERT_EQ(d.runner.buffer_turns(), 1u);
        // the link holds the turns up for 600 ms and then delivers them in a bunch of twelve
        const uint32_t held_from = d.now + 20;
        auto stalled_link = [&](uint32_t k) {
            const uint32_t at = k * kTurnMs + 40;
            return (at >= held_from && at < held_from + 600) ? held_from + 600 : at;
        };
        bool rebuilt_seen = false;
        uint32_t stall_ms_max = 0;
        size_t ticks_before = d.tick_times.size();
        for (uint32_t i = 0; i < 80; ++i) {
            d.frame(17, stalled_link);
            rebuilt_seen = rebuilt_seen || d.runner.rebuilding();
            stall_ms_max = std::max(stall_ms_max, d.runner.stalled_ms());
        }
        ASSERT_TRUE(rebuilt_seen);                                                        // it waited and collected the buffer again
        ASSERT_EQ(d.runner.buffer_turns(), 1u);                                           // a hold of 600 ms: no buffer bridges it (J2.7 has the ones that one turn would), and the bunch that it held back is the freeze's own lateness, not the link's
        ASSERT_TRUE(stall_ms_max >= 400 && stall_ms_max <= 650);                          // the wait was counted: from the tick that was due to the bunch
        ASSERT_EQ(d.stall_episodes, 1u);
        ASSERT_TRUE(d.tick_times.size() > ticks_before + 20);                             // and the game went on
        // the buffer is back: the queue holds the buffer's turns whenever a tick is due again, ticks are 50 ms apart (never together), nothing waits any more
        const uint32_t from = d.now;
        const size_t ticks_mark = d.tick_times.size();
        uint32_t min_queue = 100;
        for (uint32_t i = 0; i < 600; ++i) {
            d.frame(17, stalled_link);
            if (d.now > from + 3000) min_queue = std::min<uint32_t>(min_queue, static_cast<uint32_t>(d.runner.queued()));
        }
        ASSERT_EQ(d.stall_episodes, 1u);
        ASSERT_TRUE(min_queue >= 1);
        uint32_t closest = 1000000;
        for (size_t i = ticks_mark + 61; i < d.tick_times.size(); ++i) closest = std::min(closest, d.tick_times[i] - d.tick_times[i - 1]);
        ASSERT_TRUE(closest >= 30);                                                       // (at most the 1.25x of a run-down: never ticks in pairs)
    } TEST_END();

    TEST_CASE("J2.2 A Jittery Link: The Runner Learns It In The First Seconds, And After That The Game Does Not Stand Still For 100 ms (Uniform Jitter Of 0 - 100 And 0 - 160 ms, Ten Seeds)") {
        for (const uint32_t jitter : {100u, 160u}) {
            for (uint32_t seed = 1; seed <= 10; ++seed) {
                Lcg rng(seed);
                std::vector<uint32_t> arrive;
                uint32_t last = 0;
                for (uint32_t k = 0; k < 2000; ++k) {
                    const uint32_t at = std::max(k * kTurnMs + 40 + rng.below(jitter + 1), last);
                    arrive.push_back(at);
                    last = at;
                }
                Drive d;
                auto link = [&](uint32_t k) { return k < arrive.size() ? arrive[k] : UINT32_MAX; };
                uint32_t gaps_over_100_after_learning = 0;
                for (uint32_t i = 0; i < 3600; ++i) d.frame(17, link);                    // one minute
                for (size_t i = 1; i < d.tick_times.size(); ++i) {
                    if (d.tick_times[i] > 20000 && d.tick_times[i] - d.tick_times[i - 1] > 100) ++gaps_over_100_after_learning;
                }
                ASSERT_EQ(gaps_over_100_after_learning, 0u);
                ASSERT_TRUE(d.stall_episodes <= 4);                                       // a few stalls while it learned
                ASSERT_TRUE(d.runner.buffer_turns() >= 2);
                ASSERT_TRUE(d.tick_times.size() >= 3600 * 17 / 50 - 25);                  // and the game ran in real time: 20 ticks a second
            }
        }
    } TEST_END();

    TEST_CASE("J2.3 The Queue Is Run Down At Up To Four Times Normal Speed And Never Faster; A Queue Within The Buffer Runs At Normal Speed; The Slack Is Counted In Time") {
        Drive d;
        auto link = [](uint32_t k) { return k < 100 ? 0u : UINT32_MAX; };               // a hundred turns are all there at once
        d.frame(17, link);                                                              // the first update: the runner starts, the first tick is due at once
        ASSERT_EQ(d.tick_times.size(), 1u);
        ASSERT_TRUE(d.runner.queued() == 99);
        ASSERT_EQ(d.runner.speed_x4(), 16u);                                            // 4x: the limit
        ASSERT_TRUE(d.runner.slack_ms() >= 99 * 50 - 50 && d.runner.slack_ms() <= 99 * 50 + 50);
        uint32_t most_in_50 = 0;
        std::vector<uint32_t> times;
        for (uint32_t i = 0; i < 200 && d.runner.queued() > 0; ++i) {
            d.frame(10, link);
            times.push_back(d.now);
        }
        for (size_t i = 0; i < d.tick_times.size(); ++i) {
            uint32_t n = 0;
            for (size_t j = i; j < d.tick_times.size() && d.tick_times[j] < d.tick_times[i] + 50; ++j) ++n;
            most_in_50 = std::max(most_in_50, n);
        }
        ASSERT_TRUE(most_in_50 <= 5);                                                   // four in 50 ms (and one more at the edge of the window)
        ASSERT_TRUE(most_in_50 >= 4);
        ASSERT_EQ(d.runner.next_turn_to_execute(), 100u);                               // all of them ran: in about 99 * 50 / 4 = 1.2 s
        ASSERT_TRUE(d.now <= 17 + 1700);
        // the speed is a function of the slack: normal up to the buffer (target * 50 ms and 10 ms), then a quarter faster for every 50 ms more, 4x at 550 ms more
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 1, 720000);
        LockstepRunner r(sim);
        uint32_t k = 0;
        for (; k < 2; ++k) {
            TurnMsg m;
            m.turn = k;
            r.on_turn(m);
        }
        r.update(0);                                                                    // starts: runs turn 0; turn 1 is queued, due in 50 ms
        ASSERT_EQ(r.queued(), 1u);
        ASSERT_EQ(r.slack_ms(), 50u);
        ASSERT_EQ(r.speed_x4(), 4u);                                                    // within the buffer (60 ms)
        for (; k < 3; ++k) {
            TurnMsg m;
            m.turn = k;
            r.on_turn(m);
        }
        ASSERT_EQ(r.slack_ms(), 100u);                                                  // two queued: the newest is due in 100 ms: 40 ms beyond the buffer
        ASSERT_EQ(r.speed_x4(), 5u);                                                    // 1.25x
        for (; k < 5; ++k) {
            TurnMsg m;
            m.turn = k;
            r.on_turn(m);
        }
        ASSERT_EQ(r.slack_ms(), 200u);                                                  // 140 beyond: three steps
        ASSERT_EQ(r.speed_x4(), 7u);
        for (; k < 40; ++k) {
            TurnMsg m;
            m.turn = k;
            r.on_turn(m);
        }
        ASSERT_EQ(r.speed_x4(), 16u);                                                   // never beyond four times
        LockstepRunner::Config slow;
        slow.max_speed_x4 = 8;                                                          // a runner that is only allowed twice normal speed
        sim::SimulationEngine sim2;
        sim2.init_test_world(40, 40, 1, 720000);
        LockstepRunner capped(sim2, slow);
        for (uint32_t j = 0; j < 40; ++j) {
            TurnMsg m;
            m.turn = j;
            capped.on_turn(m);
        }
        capped.update(0);
        ASSERT_EQ(capped.speed_x4(), 8u);
    } TEST_END();

    TEST_CASE("J2.4 What A Bunch Leaves Behind Is Run Down To The Buffer And 10 ms, Not To A Whole Turn More: After A Freeze Of The Link, A Hitch And A Hidden Window The Slack Is What It Is On A Link That Never Had One") {
        // the mean slack (the time until the newest queued turn is due) at the end of the frames, from 42 s on (the buffer that an episode asks for shrinks a turn every ten
        // seconds: it is back at one turn by 34 s)
        auto mean_slack = [](int kind) {
            Drive d;
            const uint32_t event_at = 3000;
            auto link = [&](uint32_t k) {
                const uint32_t at = k * kTurnMs + 40;
                if (kind == 1 && at >= event_at && at < event_at + 1000) return event_at + 1000;         // the link froze for a second
                return at;
            };
            uint64_t slack_sum = 0;
            uint32_t slack_n = 0;
            for (uint32_t i = 0; i < 3000; ++i) {
                uint32_t dt = 17;
                if (kind == 2 && d.now >= event_at && d.now < event_at + 17) dt = 400;                   // a hitch of 400 ms
                if (kind == 3 && d.now >= event_at && d.now < event_at + 5000) dt = 750;                 // a hidden window for 5 s (one frame in 0.75 s)
                d.frame(dt, link);
                if (d.now > 42000) {
                    slack_sum += d.runner.slack_ms();
                    ++slack_n;
                }
            }
            return std::pair<uint32_t, uint32_t>{slack_n > 100 ? static_cast<uint32_t>(slack_sum / slack_n) : 9999u, static_cast<uint32_t>(d.runner.queued())};
        };
        const auto baseline = mean_slack(0);
        ASSERT_TRUE(baseline.first >= 20 && baseline.first <= 70);                                       // a steady link: what the one turn of buffer is, seen at the ends of 17 ms frames
        for (int kind = 1; kind <= 3; ++kind) {
            const auto after = mean_slack(kind);
            if (after.first + 10 < baseline.first || after.first > baseline.first + 10) std::cout << "\n    kind " << kind << ": mean slack " << after.first << ", steady " << baseline.first << "\n";
            ASSERT_TRUE(after.first + 10 >= baseline.first && after.first <= baseline.first + 10);      // the same: nothing stayed behind (a whole turn more would be 50 ms)
            ASSERT_TRUE(after.second <= 3);
        }
    } TEST_END();

    TEST_CASE("J2.5 A Long Frame Runs Its Time's Ticks And Leaves The Buffer Behind It (Nothing Stands In The Queue Beyond What Its Time Covers, So Normal Speed): A Hidden Window (0.75 s Frames) Neither Stalls, Nor Grows The Buffer, Nor Falls Behind") {
        Drive d;
        auto link = [](uint32_t k) { return k * kTurnMs + 40; };
        for (uint32_t i = 0; i < 300; ++i) d.frame(17, link);
        const size_t ticks_before = d.tick_times.size();
        const uint32_t from = d.now;
        for (uint32_t i = 0; i < 40; ++i) d.frame(750, link);                           // 30 s of frames that stand for 0.75 s each
        const uint32_t elapsed = d.now - from;
        const size_t ran = d.tick_times.size() - ticks_before;
        ASSERT_TRUE(ran + 3 >= elapsed / kTurnMs && ran <= elapsed / kTurnMs + 3);      // real time: a tick per 50 ms
        ASSERT_EQ(d.stall_episodes, 0u);
        ASSERT_EQ(d.runner.buffer_turns(), 1u);
        ASSERT_TRUE(d.runner.queued() <= 3);                                            // 1 - 2 turns stay, as on a steady link
        for (uint32_t i = 0; i < 300; ++i) d.frame(17, link);                           // shown again: nothing to catch up
        ASSERT_TRUE(d.runner.queued() <= 3);
        ASSERT_EQ(d.stall_episodes, 0u);
    } TEST_END();

    TEST_CASE("J2.7 The Runner Tells The Rule How Long A Stall Lasted When The Late Turn Comes: A Late Turn Within 100 ms Grows The Buffer A Turn, One That Is Later Than That (A Lost Packet) Does Not, And Neither Is Left Waiting For A Buffer It Does Not Get") {
        for (const uint32_t hold_ms : {40u, 90u, 140u, 300u}) {
            Drive d;
            auto steady = [](uint32_t k) { return k * kTurnMs + 40; };
            for (uint32_t i = 0; i < 400; ++i) d.frame(17, steady);                          // 7 s of a steady link: one turn
            ASSERT_EQ(d.runner.buffer_turns(), 1u);
            const uint32_t held_from = d.now + 30;
            // every turn that would arrive in the next hold_ms + 50 ms comes hold_ms + 50 ms later: the next one is late by hold_ms beyond the 50 ms that the buffer holds in hand
            auto late = [&](uint32_t k) {
                const uint32_t at = k * kTurnMs + 40;
                return (at >= held_from && at < held_from + hold_ms + 50) ? held_from + hold_ms + 50 : at;
            };
            uint32_t grown_to = 0;
            for (uint32_t i = 0; i < 200; ++i) {
                d.frame(17, late);
                if (i < 120) grown_to = std::max(grown_to, d.runner.buffer_turns());
            }
            ASSERT_TRUE(d.stall_episodes >= 1);
            if (hold_ms <= 90) ASSERT_TRUE(grown_to >= 2);                                    // one more turn would have bridged it (or nearly)
            else ASSERT_EQ(grown_to, 1u);                                                    // 140 and 300 ms: nothing would, and the burst of a few turns is under the 5 % of the percentile
        }
    } TEST_END();

    TEST_CASE("J2.8 A Window That Was Frozen For 10 s (the application hands the network one second of it) Catches Up At 4x And Leaves No Large Buffer Behind: Its Clock Jumped, So What The Rule Had Read Is Forgotten") {
        Drive d;
        auto steady = [](uint32_t k) { return k * kTurnMs + 40; };
        for (uint32_t i = 0; i < 600; ++i) d.frame(17, steady);                           // 10 s of a steady link
        ASSERT_EQ(d.runner.buffer_turns(), 1u);
        d.freeze(10000, 1000, steady);                                                    // frozen for 10 s: 200 turns are waiting, the runner's clock moved 1 s
        ASSERT_TRUE(d.runner.queued() >= 100);                                            // (the first frame ran 80 ticks: the second it stood for, at four times the speed; 200 were waiting)
        ASSERT_TRUE(d.runner.queued() <= 125);
        ASSERT_TRUE(d.runner.backlog_ms() >= 3000);
        for (uint32_t i = 0; i < 600; ++i) d.frame(17, steady);                           // 10 s more
        ASSERT_TRUE(d.runner.queued() <= 3);                                              // caught up
        ASSERT_EQ(d.runner.buffer_turns(), 1u);                                           // the buffer did not grow (what was read before the freeze said "10 s early" against what came after)
        ASSERT_TRUE(d.runner.slack_ms() <= 100);
    } TEST_END();

    TEST_CASE("J2.6 A Fixed Buffer (minimum = maximum) Never Moves; Nothing Is Owed For A Wait, So A Late Turn Is Never Followed By More Than Four Times Normal Speed") {
        LockstepRunner::Config cfg;
        cfg.jitter.min_turns = cfg.jitter.max_turns = 2;
        Drive d(cfg);
        auto link = [](uint32_t k) {
            const uint32_t at = k * kTurnMs + 40;
            return (at >= 4000 && at < 6000) ? uint32_t{6000} + (k - 80) * 5 : at;                  // a hole of two seconds, then the turns that were held come 5 ms apart
        };
        for (uint32_t i = 0; i < 600; ++i) d.frame(17, link);
        ASSERT_EQ(d.runner.buffer_turns(), 2u);
        ASSERT_EQ(d.stall_episodes, 1u);
        auto most_in_window = [](const std::vector<uint32_t>& times, uint32_t from, uint32_t window) {
            uint32_t most = 0;
            for (size_t i = 0; i < times.size(); ++i) {
                if (times[i] < from) continue;
                uint32_t n = 0;
                for (size_t j = i; j < times.size() && times[j] < times[i] + window; ++j) ++n;
                most = std::max(most, n);
            }
            return most;
        };
        ASSERT_TRUE(most_in_window(d.tick_times, 3000, 50) <= 6);        // 4x is four ticks in 50 ms; frames of 17 ms round that to at most six
        // one turn 400 ms late: the runner waits for it and runs on: no burst of ticks to make up for the wait
        Drive e;
        auto late_one = [](uint32_t k) { return k == 60 ? uint32_t{3000 + 40 + 400} : k * kTurnMs + 40; };
        for (uint32_t i = 0; i < 300; ++i) e.frame(17, late_one);
        ASSERT_TRUE(e.tick_times.size() > 90);
        ASSERT_TRUE(most_in_window(e.tick_times, 3000, 50) <= 6);
        ASSERT_EQ(e.stall_episodes, 1u);
    } TEST_END();

    TEST_CASE("J2.9 The Last Turns Of A Stream Run After A Stall: The Host Stops Sealing When The Match Ends, So Fewer Than Target + 1 Turns Are Queued For Ever (The Runner Went On Waiting For A Buffer That Could Not Come: The Player Never Saw The Match End)") {
        // The host freezes after the decisive turn T (the room ends the match): no turn comes after it. A runner that stalled shortly before collects the buffer again, waits for target + 1
        // queued turns, and used to wait for ever when the stall had grown the buffer (or the last turns came late): the last turn(s) were never run. Measured with the real client:
        // the last turn 60 ms late -> unexecuted. Every turn must run, whatever came late near the end.
        const uint32_t T = 240;                                                              // the last turn number
        struct Scenario {
            std::string name;
            std::function<uint32_t(uint32_t)> arrival;
            LockstepRunner::Config cfg;
        };
        const auto steady = [](uint32_t k) { return k > T ? UINT32_MAX : k * kTurnMs + 30; };
        std::vector<Scenario> all;
        all.push_back({"steady link", steady, {}});
        for (const uint32_t late : {10u, 30u, 60u, 90u, 120u, 200u, 500u, 2000u}) {
            all.push_back({"the last turn late by " + std::to_string(late) + " ms", [=](uint32_t k) { return k == T ? T * kTurnMs + 30 + late : steady(k); }, {}});
        }
        for (const uint32_t late : {60u, 120u, 300u}) {                                      // the last two come late, together
            all.push_back({"the last two turns late by " + std::to_string(late) + " ms", [=](uint32_t k) {
                               if (k == T - 1) return (T - 1) * kTurnMs + 30 + late;
                               if (k == T) return std::max(T * kTurnMs + 30, (T - 1) * kTurnMs + 30 + late);
                               return steady(k);
                           }, {}});
        }
        for (uint32_t before = 1; before <= 6; ++before) {                                   // a stall of 120 ms that ends `before` turns before the end: the rest is on time again
            all.push_back({"turn T-" + std::to_string(before) + " late by 120 ms, the rest on time", [=](uint32_t k) {
                               return k >= T - before ? std::max(steady(k), (T - before) * kTurnMs + 30 + 120) : steady(k);
                           }, {}});
        }
        LockstepRunner::Config four;
        four.jitter.min_turns = four.jitter.max_turns = 4;                                   // a rough link that the rule has learned: four turns to collect
        all.push_back({"a buffer of four turns, the last turn late by 120 ms", [=](uint32_t k) { return k == T ? T * kTurnMs + 30 + 120 : steady(k); }, four});
        for (const Scenario& sc : all) {
            for (const uint32_t phase : {0u, 1u, 2u}) {                                      // the frames (60 Hz) fall differently on the arrivals
                Drive d(sc.cfg);
                uint32_t i = phase;
                while (d.now < T * kTurnMs + 30 + 2500 + 3000) d.frame(i++ % 3 == 2 ? 17u : 16u, sc.arrival);
                if (d.runner.next_turn_to_execute() != T + 1) std::cout << "\n    " << sc.name << " (phase " << phase << "): executed " << d.runner.next_turn_to_execute() << " of " << T + 1 << ", queued " << d.runner.queued() << "\n";
                ASSERT_EQ(d.runner.next_turn_to_execute(), T + 1);
                ASSERT_EQ(d.runner.queued(), size_t{0});
            }
        }
    } TEST_END();

    TEST_CASE("J2.10 The Wait For The Buffer Is Bounded: After A Stall The Runner Goes On With What Is Queued Once The Oldest Turn Has Waited Target + 1 Turns (A Healthy Stream Would Have Brought Them); A Stream That Comes On Time Is Not Hurried") {
        // a runner with a buffer of two (collects three turns), a stream that has stopped for good after the stall: one turn is queued
        LockstepRunner::Config cfg;
        cfg.jitter.min_turns = cfg.jitter.max_turns = 2;
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 1, 720000);
        LockstepRunner r(sim, cfg);
        const auto feed = [&](uint32_t turn) {
            TurnMsg t;
            t.turn = turn;
            return r.on_turn(t);
        };
        for (uint32_t k = 0; k < 3; ++k) ASSERT_TRUE(feed(k));
        r.update(17);                                                                       // starts: three turns are queued
        for (uint32_t i = 0; i < 20; ++i) r.update(17);                                     // runs them (the stream stops after turn 2): the queue is empty, a tick is due
        ASSERT_EQ(r.next_turn_to_execute(), 3u);
        ASSERT_TRUE(r.rebuilding());
        ASSERT_TRUE(feed(3));                                                               // ONE more turn comes after a stall (the stream ends here: the host froze)
        r.update(16);                                                                       // read at this frame: it has waited 0 ms
        ASSERT_EQ(r.next_turn_to_execute(), 3u);
        uint32_t waited = 0;
        while (r.next_turn_to_execute() == 3 && waited < 1000) {
            r.update(16);
            waited += 16;
        }
        // (target 2 + 1) * 50 ms = 150 ms from the frame that read it; frames of 16 ms
        ASSERT_TRUE(waited >= 150 - 16 && waited <= 150 + 16);
        ASSERT_EQ(r.next_turn_to_execute(), 4u);
        ASSERT_EQ(r.queued(), size_t{0});
        // the wait is counted for the oldest queued turn: a second turn that comes 40 ms later does not make it wait longer, and with two queued the third is still awaited
        // (the buffer asks for three) until the bound
        ASSERT_TRUE(feed(4));
        r.update(16);
        for (uint32_t i = 0; i < 2; ++i) r.update(16);                                      // 32 ms
        ASSERT_TRUE(feed(5));
        waited = 0;
        while (r.next_turn_to_execute() == 4 && waited < 1000) {
            r.update(16);
            waited += 16;
        }
        ASSERT_TRUE(waited <= 150 - 32 + 16);                                               // from the first turn's reading, not the second's
        // the bound is for the rebuilding after a stall only: a runner that has never started waits for its full buffer however long it takes
        sim::SimulationEngine sim2;
        sim2.init_test_world(40, 40, 1, 720000);
        LockstepRunner fresh(sim2, cfg);
        TurnMsg t0;
        t0.turn = 0;
        fresh.on_turn(t0);
        for (uint32_t i = 0; i < 100; ++i) fresh.update(16);                                // 1.6 s with one turn
        ASSERT_EQ(fresh.next_turn_to_execute(), 0u);
        ASSERT_FALSE(fresh.rebuilding());
    } TEST_END();

    TEST_CASE("J2.11 A Window That Is Drawn Less Often Than Every 100 ms Runs A Standing Backlog Down: Long Frames Run At The Speed The Slack Asks For (Up To 4x), Not At Normal Speed (A Page Drawn Once A Second Stayed 2 - 15 s Behind For Ever)") {
        for (const uint32_t frame_ms : {150u, 250u, 500u, 1000u}) {
            Drive d;
            auto steady = [](uint32_t k) { return k * kTurnMs + 40; };
            for (uint32_t i = 0; i < 600; ++i) d.frame(17, steady);                       // 10 s of a steady link at 60 Hz
            d.freeze(10000, 1000, steady);                                                // frozen for 10 s: the first frame back runs a second's worth and leaves a backlog
            ASSERT_TRUE(d.runner.backlog_ms() >= 3000);
            // from now on the window is drawn once per `frame_ms`: every frame reads the turns that came meanwhile; the backlog must not stand for ever
            uint32_t frames_to_level = 0;
            for (uint32_t i = 0; i < 120 && frames_to_level == 0; ++i) {
                d.frame(frame_ms, steady);
                if (d.runner.queued() <= frame_ms / kTurnMs + 3) frames_to_level = i + 1;      // what one frame's time brings, and the buffer
            }
            if (frames_to_level == 0) std::cout << "\n    " << frame_ms << " ms frames: still " << d.runner.queued() << " turns queued after 120 frames\n";
            ASSERT_TRUE(frames_to_level > 0);
            ASSERT_TRUE(static_cast<uint64_t>(frames_to_level) * frame_ms <= 12000);      // a 10 s backlog at four times the speed: a few seconds at the most (the first frame back ran 1 s of it)
            // and level for good: every later frame leaves no more behind than it brings
            for (uint32_t i = 0; i < 60; ++i) {
                d.frame(frame_ms, steady);
                ASSERT_TRUE(d.runner.queued() <= frame_ms / kTurnMs + 3);
            }
            ASSERT_EQ(d.stall_episodes, 0u);                                              // (a hidden window is not "waiting": the frame ran every turn that came)
            ASSERT_TRUE(d.runner.next_turn_expected() - d.runner.next_turn_to_execute() <= frame_ms / kTurnMs + 3);
        }
    } TEST_END();

    TEST_CASE("J2.16 One Freeze Of The Link, Isolated: The Buffer Stays At One Turn (It Sat At Four For 10 s And Stepped Down Every 10 s: +150 / +100 / +50 ms For Half A Minute), The Backlog Is Run Down, And A Jittery Link That Freezes Keeps The Buffer It Needs") {
        for (const uint32_t freeze_ms : {1500u, 3000u, 5000u, 10000u}) {
            Drive d;
            const uint32_t start = 30000;
            auto link = [=](uint32_t k) {
                const uint32_t at = k * kTurnMs + 40;
                return (at >= start && at < start + freeze_ms) ? start + freeze_ms + 40 + (at - start) / 1000 : at;      // the turns held come in a bunch (a millisecond apart)
            };
            uint32_t max_target = 1;
            while (d.now < start + freeze_ms + 40000) {
                d.frame(17, link);
                max_target = std::max(max_target, d.runner.buffer_turns());
            }
            if (max_target != 1) std::cout << "\n    freeze " << freeze_ms << " ms: the buffer went to " << max_target << "\n";
            ASSERT_EQ(max_target, 1u);
            ASSERT_EQ(d.stall_episodes, 1u);                                               // the freeze itself is one wait
            ASSERT_TRUE(d.runner.queued() <= 3);                                           // and the backlog is gone
        }
        // jitter of 0 - 130 ms (the rule asks for three turns), a freeze of 3 s in the middle, jitter after it: the buffer stays what the jitter needs, before, during and after
        {
            Drive d;
            Lcg rng(11);
            auto table = std::make_shared<std::vector<uint32_t>>();
            uint32_t last = 0;
            for (uint32_t k = 0; k < 3000; ++k) {
                uint32_t at = k * kTurnMs + 30 + rng.below(130);
                if (at >= 40000 && at < 43000) at = 43000;
                at = std::max(at, last);
                last = at;
                table->push_back(at);
            }
            auto link = [table](uint32_t k) { return k < table->size() ? (*table)[k] : UINT32_MAX; };
            while (d.now < 39900) d.frame(17, link);
            ASSERT_EQ(d.runner.buffer_turns(), 3u);
            uint32_t min_after = 99;
            uint32_t max_after = 0;
            while (d.now < 70000) {
                d.frame(17, link);
                if (d.now > 43100) {
                    min_after = std::min(min_after, d.runner.buffer_turns());
                    max_after = std::max(max_after, d.runner.buffer_turns());
                }
            }
            ASSERT_EQ(min_after, 3u);                                                      // the jitter did not stop: the freeze neither shrank the buffer nor made it larger
            ASSERT_EQ(max_after, 3u);
        }
    } TEST_END();

    TEST_CASE("J2.12 A Stall That Begins In A Long Frame Is Counted From The Frames That Follow (The \"Waiting\" Message Needs It), Does Not Collect The Buffer Again, And The Next Late Turn Runs The Moment It Comes") {
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 1, 720000);
        LockstepRunner r(sim);
        for (uint32_t k = 0; k < 10; ++k) {
            TurnMsg t;
            t.turn = k;
            ASSERT_TRUE(r.on_turn(t));
        }
        r.update(16);                                                                      // starts (two turns are enough: ten are queued)
        r.update(600);                                                                     // ONE long frame: it runs every turn that is there and ends empty-handed (a tick is due)
        ASSERT_EQ(r.next_turn_to_execute(), 10u);
        ASSERT_EQ(r.stalled_ms(), 0u);                                                     // (a tick ran in it: its own time is not counted)
        ASSERT_FALSE(r.rebuilding());                                                      // a long frame ends empty-handed because it ran everything: not the link's fault, no buffer to collect
        for (uint32_t i = 0; i < 120; ++i) r.update(16);                                   // 1.9 s of frames, no turn
        ASSERT_EQ(r.stalled_ms(), 120u * 16u);                                             // counted: the overlay shows "Waiting for the other players..." from 1000
        ASSERT_TRUE(r.stalled_ms() >= 1000);
        ASSERT_FALSE(r.rebuilding());
        TurnMsg late;
        late.turn = 10;
        ASSERT_TRUE(r.on_turn(late));
        r.update(16);                                                                      // the late turn runs at once: it is not held for a buffer
        ASSERT_EQ(r.next_turn_to_execute(), 11u);
        ASSERT_EQ(r.stalled_ms(), 0u);
    } TEST_END();

    TEST_CASE("J2.13 A Stall That Ends In A Long Frame Is Not Told To The Rule: What The Rule Had Read Is Kept (A Frame's Own Length Says Nothing About The Link), And The Buffer Is Not Grown Or Forgotten By It") {
        Drive d;
        Lcg rng(7);
        // a link with 0 - 100 ms of jitter for 20 s: the rule learns it
        const uint32_t jitter_until = 20000;
        auto cumulative = std::make_shared<std::vector<uint32_t>>();
        {
            uint32_t last = 0;
            for (uint32_t k = 0; k < 2000; ++k) {
                uint32_t at = k * kTurnMs + 30 + rng.below(100);
                if (k * kTurnMs >= jitter_until + 10000) at = k * kTurnMs + 30;
                at = std::max(at, last);
                // a stall of 2 s: nothing is delivered in [22000, 24000), the turns that were held come together at 24000
                if (at >= 22000 && at < 24000) at = 24000;
                last = at;
                cumulative->push_back(at);
            }
        }
        const auto link = [cumulative](uint32_t k) { return k < cumulative->size() ? (*cumulative)[k] : UINT32_MAX; };
        while (d.now < 21900) d.frame(17, link);
        const uint32_t lateness_before = d.runner.jitter().lateness_ms();
        const uint32_t buffer_before = d.runner.buffer_turns();
        ASSERT_TRUE(lateness_before >= 40);                                                // it has learned the link
        ASSERT_TRUE(buffer_before >= 2);
        while (d.now < 23900) d.frame(17, link);                                           // the stall: 2 s without a turn, in ordinary frames
        ASSERT_TRUE(d.runner.stalled_ms() >= 1500);
        d.frame(150, link);                                                                // the frame that reads the bunch is a LONG one (a hitch)
        ASSERT_TRUE(d.runner.queued() > 10);                                               // (the bunch was read in it)
        ASSERT_EQ(d.runner.jitter().lateness_ms(), lateness_before);                       // not forgotten (a long stall told in an ordinary frame would forget it)
        ASSERT_EQ(d.runner.buffer_turns(), buffer_before);                                 // and not grown by a stall that no turn would have bridged
    } TEST_END();

    TEST_CASE("J2.14 Owed Game Time Is Bounded: A Frame That Cannot Run Everything It Is Owed (max_ticks_per_update) Keeps At Most That Much For The Next One, Not All Of It") {
        LockstepRunner::Config cfg;
        cfg.max_ticks_per_update = 2;
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 1, 720000);
        LockstepRunner r(sim, cfg);
        for (uint32_t k = 0; k < 300; ++k) {
            TurnMsg t;
            t.turn = k;
            ASSERT_TRUE(r.on_turn(t));
        }
        r.update(16);                                                                      // starts: one tick
        ASSERT_EQ(r.next_turn_to_execute(), 1u);
        r.update(100);                                                                     // 4x for 100 ms is eight ticks owed; two are allowed in a frame of this length
        ASSERT_EQ(r.next_turn_to_execute(), 3u);
        for (uint32_t i = 0; i < 6; ++i) r.update(0);                                      // no time passes: only what is still owed runs, and that is bounded to two ticks
        ASSERT_EQ(r.next_turn_to_execute(), 5u);                                           // (unbounded it would be 3 + 2 + 2 + 2 ... = 9 or more)
    } TEST_END();

    TEST_CASE("J2.15 backlog_ms Counts A Turn As 50 ms: The Turns That Are Here And Not Run, In Milliseconds Of Play (\"Catching up...\" Is Shown From 3000 Of Them: Sixty Turns)") {
        sim::SimulationEngine sim;
        sim.init_test_world(40, 40, 1, 720000);
        LockstepRunner r(sim);
        ASSERT_EQ(r.backlog_ms(), 0u);
        uint32_t next = 0;
        for (const uint32_t total : {1u, 2u, 7u, 59u, 60u, 61u, 400u}) {
            while (next < total) {
                TurnMsg t;
                t.turn = next++;
                ASSERT_TRUE(r.on_turn(t));
            }
            ASSERT_EQ(r.backlog_ms(), total * kTurnMs);
            ASSERT_EQ(r.queued(), size_t{total});
        }
        ASSERT_EQ(kTurnMs, 50u);
        ASSERT_EQ(r.backlog_ms(), 400u * 50u);
    } TEST_END();

    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
