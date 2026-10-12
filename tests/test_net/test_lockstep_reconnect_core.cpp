// Tests of the lock-step network core: coming back (protocol 10), the turn log, the attendance rules, the runner's fast-forward
#include "lockstep_fixtures.hpp"

void run_reconnect_core_tests() {
    TEST_CASE("N2.41 Turn Log: 36,000 Real-Looking Turns (30 Minutes Of 50 ms) Are Kept Packed And Read Back In Batches Of Every Size, Identical To What Was Sealed; The Limit Stops It At The Right Turn And For Good") {
        const std::vector<TurnMsg> all = real_looking_turns(36000, 7);
        std::vector<size_t> sizes(all.size());                            // what each turn packs to, and the sum (the blob), worked out without the log
        size_t blob = 0;
        std::vector<uint8_t> everything;
        for (size_t t = 0; t < all.size(); ++t) {
            const std::vector<uint8_t> p = pack_turn(all[t]);
            sizes[t] = p.size();
            blob += p.size();
            everything.insert(everything.end(), p.begin(), p.end());
        }
        TurnLog log;
        ASSERT_TRUE(log.max_bytes() == TurnLog::kDefaultMaxBytes && TurnLog::kDefaultMaxBytes == 16u * 1024u * 1024u && log.usable() && log.turns() == 0 && log.bytes() == 0);
        for (const TurnMsg& t : all) ASSERT_TRUE(log.append(t));
        ASSERT_TRUE(log.usable());
        ASSERT_EQ(log.turns(), 36000u);
        ASSERT_EQ(log.bytes(), blob + 36000u * 4u);                       // the packed turns and a 4-byte offset for each
        ASSERT_TRUE(log.bytes() > 36000u * 6u && log.bytes() < 700u * 1024u);   // 30 minutes of busy play (4 - 5 orders a second, up to 24 ants): 651,372 bytes, 144,000 of them the index; nobody giving an order at all is 216,000. Far under the limit
        ASSERT_EQ(log.bytes_between(0, log.turns()), blob);
        // everything in one read is the packing by hand, byte for byte
        {
            std::vector<uint8_t> out;
            ASSERT_EQ(log.read(0, 36000, SIZE_MAX, out), 36000u);
            ASSERT_TRUE(out == everything);
        }
        // the stream as the host sends it (4096 turns, 48 KB): every batch decodes to the turns that were sealed, every message fits
        {
            uint32_t next = 0;
            size_t batches = 0;
            while (next < log.turns()) {
                std::vector<uint8_t> packed;
                const uint32_t n = log.read(next, static_cast<uint32_t>(kMaxBatchTurns), kBatchBytes, packed);
                ASSERT_TRUE(n >= 1 && n <= kMaxBatchTurns);
                const std::vector<uint8_t> message = encode_turn_batch_packed(next, n, packed.data(), packed.size());
                ASSERT_TRUE(!message.empty() && message.size() <= kMaxMessageBytes && packed.size() <= kBatchBytes);
                TurnBatchMsg b;
                ASSERT_TRUE(decode(message, b) && b.turns.size() == n && batch_is(all, next, b));
                TurnBatchMsg built;                                       // and the batch made from the decoded turns is the same message
                built.first_turn = next;
                built.turns = b.turns;
                ASSERT_TRUE(encode(built) == message);
                next += n;
                ++batches;
            }
            ASSERT_EQ(next, 36000u);
            ASSERT_TRUE(batches >= 9 && batches <= 60);                   // 36,000 turns / 4096 at the least; a few hundred KB / 48 KB
        }
        // batches of every size (the turn limit, no byte limit): the count is the size asked for until the end, and everything comes back
        for (const uint32_t size : {1u, 2u, 3u, 5u, 7u, 16u, 63u, 64u, 65u, 100u, 255u, 256u, 1000u, 4095u, 4096u}) {
            uint32_t next = 0;
            while (next < 36000u) {
                std::vector<uint8_t> packed;
                const uint32_t n = log.read(next, size, SIZE_MAX, packed);
                ASSERT_EQ(n, std::min(size, 36000u - next));
                ASSERT_EQ(packed.size(), log.bytes_between(next, next + n));
                if (next % 997 == 0 || next + n == 36000u) {              // (decoded for a sample of them: all would be 15 x 36000 turns of decoding)
                    TurnBatchMsg b;
                    ASSERT_TRUE(decode(encode_turn_batch_packed(next, n, packed.data(), packed.size()), b) && batch_is(all, next, b));
                }
                next += n;
            }
        }
        // a limit in bytes: at least one turn, then as many as fit; a turn that is alone over the limit is a batch of its own
        for (const size_t limit : {size_t{1}, size_t{10}, size_t{100}, size_t{1000}, size_t{4096}, kBatchBytes}) {
            uint32_t next = 0;
            while (next < 36000u) {
                std::vector<uint8_t> packed;
                const uint32_t n = log.read(next, 4096, limit, packed);
                ASSERT_TRUE(n >= 1 && packed.size() == log.bytes_between(next, next + n));
                ASSERT_TRUE(n == 1 || packed.size() <= limit);            // more than one turn only when they fit
                if (next + n < 36000u) {                                  // ... and the next turn would not have fitted (or 4096 are in)
                    ASSERT_TRUE(n == 4096 || packed.size() + sizes[next + n] > limit);
                }
                next += n;
            }
        }
        // random reads from anywhere against the sealed turns
        {
            Lcg rng(11);
            for (int i = 0; i < 400; ++i) {
                const uint32_t from = rng.below(36000);
                const uint32_t max_turns = 1 + rng.below(100);
                const size_t limit = 1 + rng.below(5000);
                std::vector<uint8_t> packed;
                const uint32_t n = log.read(from, max_turns, limit, packed);
                ASSERT_TRUE(n >= 1 && n <= max_turns && from + n <= 36000u);
                TurnBatchMsg b;
                ASSERT_TRUE(decode(encode_turn_batch_packed(from, n, packed.data(), packed.size()), b) && batch_is(all, from, b));
            }
        }
        // bytes_between is the sum of the turns' sizes, for any range; ranges that are empty, reversed or past the end count for nothing
        {
            Lcg rng(12);
            for (int i = 0; i < 400; ++i) {
                const uint32_t a = rng.below(36000);
                const uint32_t b = a + rng.below(36000 - a + 1);
                size_t sum = 0;
                for (uint32_t t = a; t < b; ++t) sum += sizes[t];
                ASSERT_EQ(log.bytes_between(a, b), sum);
            }
            ASSERT_EQ(log.bytes_between(5, 5), size_t{0});
            ASSERT_EQ(log.bytes_between(9, 5), size_t{0});
            ASSERT_EQ(log.bytes_between(36000, 99999), size_t{0});
            ASSERT_EQ(log.bytes_between(35999, 99999), sizes[35999]);     // (the end is cut at the turns that there are)
            ASSERT_EQ(log.bytes_between(0, 0xFFFFFFFFu), blob);
            std::vector<uint8_t> out = {1, 2, 3};
            ASSERT_EQ(log.read(36000, 10, 100, out), 0u);                 // nothing at or past the end, nothing for zero turns, and `out` is only ever appended to
            ASSERT_EQ(log.read(0xFFFFFFFFu, 10, 100, out), 0u);
            ASSERT_EQ(log.read(0, 0, 100, out), 0u);
            ASSERT_TRUE(out == (std::vector<uint8_t>{1, 2, 3}));
            ASSERT_EQ(log.read(0, 1, 100, out), 1u);
            ASSERT_TRUE(out.size() == 3 + sizes[0] && out[0] == 1 && out[3] == everything[0]);
        }
        // an empty log reads nothing and costs nothing
        {
            TurnLog empty;
            std::vector<uint8_t> out;
            ASSERT_TRUE(empty.read(0, 10, 100, out) == 0 && out.empty() && empty.bytes() == 0 && empty.bytes_between(0, 10) == 0 && empty.usable() && empty.turns() == 0);
        }
        // a turn that is alone bigger than the byte limit still goes (a batch holds at least one turn)
        {
            TurnLog big;
            ASSERT_TRUE(big.append(heavy_turn(0, 200, 32)));
            TurnMsg small;
            small.turn = 1;
            ASSERT_TRUE(big.append(small));
            ASSERT_TRUE(big.append(heavy_turn(2, 1, 1)));
            std::vector<uint8_t> out;
            ASSERT_EQ(big.read(0, 10, 100, out), 1u);
            ASSERT_EQ(out.size(), 2 + 200 * (sim::kCommandHeaderBytes + 4 * 32));
            out.clear();
            ASSERT_EQ(big.read(1, 10, 100, out), 2u);                     // the two small turns go together
            out.clear();
            ASSERT_EQ(big.read(0, 10, 2 + 200 * (sim::kCommandHeaderBytes + 4 * 32) + 1, out), 1u);    // one byte more than the big turn: the empty turn after it (2 bytes) does not
            out.clear();
            ASSERT_EQ(big.read(0, 10, 2 + 200 * (sim::kCommandHeaderBytes + 4 * 32) + 2, out), 2u);    // two bytes more: it does
        }
        // the limit: a hostile client's log (64 commands of 32 ants in every turn: 8706 bytes and 4 of index) at 1 MiB stops at the turn that would pass it, and stays stopped
        {
            const size_t limit = 1024 * 1024;
            TurnLog hostile(limit);
            ASSERT_EQ(hostile.max_bytes(), limit);
            const size_t each = 2 + 64 * (sim::kCommandHeaderBytes + 4 * 32) + 4;
            const uint32_t fit = static_cast<uint32_t>(limit / each);     // 120
            uint32_t made = 0;
            while (made < 5000 && hostile.append(heavy_turn(made, 64, 32))) {
                ++made;
                ASSERT_TRUE(hostile.bytes() <= limit);
            }
            ASSERT_EQ(made, fit);                                         // exactly the turns that fit
            ASSERT_FALSE(hostile.usable());                               // a log with a hole serves no replay ...
            ASSERT_TRUE(hostile.turns() == 0 && hostile.bytes() == 0 && hostile.capacity_bytes() == 0);   // ... and a log that is dead holds nothing: what it had is freed the moment it died
            ASSERT_FALSE(hostile.append(heavy_turn(fit, 64, 32)));
            TurnMsg tiny;
            tiny.turn = fit;
            ASSERT_FALSE(hostile.append(tiny));                           // not even a small turn: it stays dead, and nothing is stored or allocated
            ASSERT_TRUE(hostile.turns() == 0 && hostile.bytes() == 0 && hostile.capacity_bytes() == 0 && !hostile.usable());
            std::vector<uint8_t> out;                                     // nothing can be read from it, nothing streamed
            ASSERT_EQ(hostile.read(0, 4096, SIZE_MAX, out), 0u);
            ASSERT_TRUE(out.empty() && hostile.bytes_between(0, 100) == 0);
        }
        // the limit to the byte: three empty turns cost 3 x (2 + 4) = 18
        for (const size_t limit : {size_t{0}, size_t{5}, size_t{6}, size_t{11}, size_t{12}, size_t{17}, size_t{18}, size_t{19}, size_t{23}, size_t{24}}) {
            TurnLog tight(limit);
            uint32_t made = 0;
            TurnMsg empty;
            while (made < 100) {
                empty.turn = made;
                if (!tight.append(empty)) break;
                ++made;
            }
            ASSERT_EQ(made, static_cast<uint32_t>(limit / 6));
            ASSERT_FALSE(tight.usable());                                 // (the loop ends when an append is refused: the log is dead and holds nothing)
            ASSERT_TRUE(tight.bytes() == 0 && tight.turns() == 0);
            // (what it held while it lived: the same turns again, one fewer than refused)
            TurnLog again(limit);
            for (uint32_t t = 0; t < made; ++t) {
                empty.turn = t;
                ASSERT_TRUE(again.append(empty) && again.bytes() == size_t{t + 1} * 6 && again.bytes() <= limit);
            }
        }
        // whatever the limit, never more than 0xFFFFFFF0 bytes (the offsets are 32 bits)
        ASSERT_EQ(TurnLog(SIZE_MAX).max_bytes(), static_cast<size_t>(0xFFFFFFF0u));
        ASSERT_EQ(TurnLog(1000).max_bytes(), size_t{1000});
        ASSERT_TRUE(TurnLog::kHardMaxBytes == 0xFFFFFFF0u);
        // a hole or a repeat is the end of the log: the turn is not the next one
        {
            TurnLog holey;
            TurnMsg t5;
            t5.turn = 5;
            ASSERT_FALSE(holey.append(t5));
            ASSERT_FALSE(holey.usable());
            TurnMsg t0;
            ASSERT_FALSE(holey.append(t0));                               // stopped for good
            TurnLog twice;
            ASSERT_TRUE(twice.append(t0));
            ASSERT_FALSE(twice.append(t0));
            ASSERT_TRUE(!twice.usable() && twice.turns() == 0 && twice.bytes() == 0);      // (dead: it freed the turn that it had)
        }
        // a turn that no batch could carry: the largest turn that does is 65,526 bytes packed (7 bytes of batch header and 65,529 are what a message may hold, and a turn is 2 + a multiple of 4)
        {
            auto turn_of = [](uint32_t number, size_t last_ants) {         // 481 commands of 32 ants, and one of `last_ants`
                TurnMsg t = heavy_turn(number, 481, 32);
                std::vector<uint32_t> list;
                for (size_t i = 0; i < last_ants; ++i) list.push_back(static_cast<uint32_t>(i));
                t.commands.push_back(cmd(CommandType::GroupMove, 0, 255, 1, 1, list));
                return t;
            };
            TurnLog fits;
            ASSERT_TRUE(fits.append(turn_of(0, 25)));                      // 2 + 481 x 136 + 108 = 65,526
            ASSERT_EQ(fits.bytes(), size_t{65526 + 4});
            std::vector<uint8_t> packed;
            ASSERT_EQ(fits.read(0, 10, 1, packed), 1u);
            TurnBatchMsg b;
            ASSERT_TRUE(decode(encode_turn_batch_packed(0, 1, packed.data(), packed.size()), b) && b.turns.size() == 1 && b.turns[0].commands.size() == 482);
            TurnLog too_big;
            ASSERT_FALSE(too_big.append(turn_of(0, 26)));                  // 65,530: no message could hold it
            ASSERT_TRUE(!too_big.usable() && too_big.turns() == 0 && too_big.bytes() == 0);
        }
        // more commands than a turn may hold are cut, as encode(TurnMsg) cuts them
        {
            TurnMsg many;
            for (size_t i = 0; i < kMaxTurnCommands + 88; ++i) many.commands.push_back(cmd(CommandType::Hatch, static_cast<uint8_t>(i % 4)));
            TurnLog cut;
            ASSERT_TRUE(cut.append(many));
            std::vector<uint8_t> packed;
            ASSERT_EQ(cut.read(0, 1, 1, packed), 1u);
            TurnBatchMsg b;
            ASSERT_TRUE(decode(encode_turn_batch_packed(0, 1, packed.data(), packed.size()), b) && b.turns[0].commands.size() == kMaxTurnCommands);
            ASSERT_EQ(packed.size(), 2 + kMaxTurnCommands * sim::kCommandHeaderBytes);
        }
    } TEST_END();

    TEST_CASE("N2.42 Attendance: Every Transition Of The Table, The Impossible Ones Change Nothing, Away Time Adds Up Over Episodes (Each Loss Counts At Least 5 s), The Pause Counts Each Moment Once (And At Least 5 s For Every Pause, For The Cap)") {
        using S = Attendance::State;
        const uint32_t t0 = 100000;
        // a snapshot of everything that can be seen: a call that must be a no-op may not change any of it
        auto snapshot = [](const Attendance& a, uint32_t now) {
            std::vector<uint32_t> v;
            for (uint8_t s = 0; s < 4; ++s) {
                v.push_back(static_cast<uint32_t>(a.state(s)));
                v.push_back(a.away_ms(s, now));
                v.push_back(a.percent(s));
                v.push_back(a.votes_for_continue(s));
            }
            v.insert(v.end(), {a.paused() ? 1u : 0u, a.pause_ms(now), a.connected_humans(), a.vote_subject(now), a.drops_by_vote(), a.drops_by_cap(), a.rejoins(), a.cap_s(now)});
            return v;
        };
        // ---- the table: which event is possible from which state, and where it leads ----
        struct Row {
            S from;
            bool lost, returning, progress, caught_up, failed, dropped;
        };
        const Row table[] = {
            {S::Present, true, true, false, false, false, true},
            {S::Absent, false, true, false, false, false, true},
            {S::CatchingUp, false, true, true, true, true, true},
            {S::Dropped, false, false, false, false, false, false},
            {S::Empty, false, false, false, false, false, false},
        };
        for (const Row& row : table) {
            for (int event = 0; event < 6; ++event) {
                Attendance a;
                a.seat_humans(0b0001 | (row.from == S::Empty ? 0 : 0b0010) | 0b1000, t0);        // seats 0, 3 and (unless the row is Empty) 1 are people; seat 2 is no person
                if (row.from == S::Absent) ASSERT_TRUE(a.lost(1, t0 + 1000));
                if (row.from == S::CatchingUp) ASSERT_TRUE(a.lost(1, t0 + 1000) && a.returning(1, t0 + 2000));
                if (row.from == S::Dropped) ASSERT_TRUE(a.dropped(1, t0 + 1000));
                ASSERT_EQ(a.state(1), row.from);
                const uint32_t now = t0 + 3000;
                const std::vector<uint32_t> before = snapshot(a, now);
                bool result = false;
                bool expect = false;
                S after = row.from;
                switch (event) {
                    case 0: result = a.lost(1, now); expect = row.lost; after = S::Absent; break;
                    case 1: result = a.returning(1, now); expect = row.returning; after = S::CatchingUp; break;
                    case 2: result = a.progress(1, 50, now); expect = row.progress; after = S::CatchingUp; break;
                    case 3: result = a.caught_up(1, now); expect = row.caught_up; after = S::Present; break;
                    case 4: result = a.catch_up_failed(1, now); expect = row.failed; after = S::Absent; break;
                    default: result = a.dropped(1, now); expect = row.dropped; after = S::Dropped; break;
                }
                ASSERT_EQ(result, expect);
                ASSERT_EQ(a.state(1), expect ? after : row.from);
                if (!expect) ASSERT_TRUE(snapshot(a, now) == before);                              // an event that does not fit changes nothing at all
            }
        }
        {   // seats that are no seats, and calls with seats out of range
            Attendance a;
            a.seat_humans(0b1011, t0);
            const std::vector<uint32_t> before = snapshot(a, t0 + 10);
            ASSERT_FALSE(a.lost(2, t0) || a.lost(4, t0) || a.lost(255, t0) || a.returning(2, t0) || a.returning(255, t0) || a.progress(2, 5, t0) || a.progress(7, 5, t0) ||
                         a.caught_up(2, t0) || a.caught_up(9, t0) || a.catch_up_failed(2, t0) || a.dropped(2, t0) || a.dropped(4, t0) || a.vote(0, 1, true, t0) || a.vote(9, 1, true, t0) || a.vote(0, 9, true, t0));
            ASSERT_TRUE(snapshot(a, t0 + 10) == before);
            ASSERT_TRUE(a.state(2) == S::Empty && a.state(4) == S::Empty && a.state(255) == S::Empty);
            ASSERT_TRUE(a.away_ms(2, t0 + 1000) == 0 && a.away_ms(200, t0 + 1000) == 0 && a.percent(200) == 0 && a.votes_for_continue(200) == 0);
            ASSERT_EQ(a.connected_humans(), 3);
            ASSERT_TRUE(a.state(0) == S::Present && a.state(1) == S::Present && a.state(3) == S::Present);
            ASSERT_EQ(a.config().vote_after_ms, kVoteAfterMs);
            ASSERT_TRUE(kVoteAfterMs == 30000 && kCatchUpStallMs == 20000 && kMinAbsenceMs == 5000 && kMaxPauseMs == 1800000);
        }
        // ---- one seat's episodes: loss, return, progress, catching up, three times; the total, the minimum, the pause ----
        {
            Attendance a;
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(!a.paused() && a.pause_ms(t0 + 9999) == 0 && a.away_ms(1, t0 + 9999) == 0 && a.cap_s(t0) == 1800);
            // episode 1: lost at +1 s, back at +6 s, caught up at +8 s: 7 s away
            ASSERT_TRUE(a.lost(1, t0 + 1000));
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.away_ms(1, t0 + 1000), 0u);
            ASSERT_EQ(a.away_ms(1, t0 + 4000), 3000u);
            ASSERT_EQ(a.pause_ms(t0 + 4000), 5000u);                                 // (a pause counts at least 5 s toward the cap, from the moment it begins: 3 s were real)
            ASSERT_EQ(a.held_ms(t0 + 4000), 3000u);                                  // (the time that the match was held counts what it really was)
            ASSERT_FALSE(a.lost(1, t0 + 4500));                                      // already lost: the absence does not begin again
            ASSERT_EQ(a.away_ms(1, t0 + 5000), 4000u);
            ASSERT_EQ(a.connected_humans(), 2);
            ASSERT_TRUE(a.returning(1, t0 + 6000));
            ASSERT_EQ(a.state(1), S::CatchingUp);
            ASSERT_EQ(a.away_ms(1, t0 + 7000), 6000u);                               // the away time goes on from the loss
            ASSERT_TRUE(a.paused());
            ASSERT_FALSE(a.progress(1, 0, t0 + 7000));                               // 0 is not more than 0
            ASSERT_TRUE(a.progress(1, 10, t0 + 7100));
            ASSERT_EQ(a.percent(1), 10);
            ASSERT_FALSE(a.progress(1, 10, t0 + 7200));                              // the same again, or less, is no progress
            ASSERT_FALSE(a.progress(1, 9, t0 + 7300));
            ASSERT_EQ(a.percent(1), 10);
            ASSERT_TRUE(a.progress(1, 250, t0 + 7400));                              // above 100 is 100
            ASSERT_EQ(a.percent(1), 100);
            ASSERT_EQ(a.percent(0), 0);
            ASSERT_TRUE(a.caught_up(1, t0 + 8000));
            ASSERT_EQ(a.state(1), S::Present);
            ASSERT_EQ(a.percent(1), 0);
            ASSERT_FALSE(a.paused());
            ASSERT_EQ(a.rejoins(), 1u);
            ASSERT_EQ(a.away_ms(1, t0 + 8000), 7000u);
            ASSERT_EQ(a.away_ms(1, t0 + 99999), 7000u);                              // (an absence that is over does not grow)
            ASSERT_EQ(a.pause_ms(t0 + 8000), 7000u);
            ASSERT_EQ(a.pause_ms(t0 + 99999), 7000u);                                // nor does the pause
            ASSERT_EQ(a.connected_humans(), 3);
            // episode 2: a blip of 300 ms: it counts 5 s toward the seat's total, and 5 s toward the pause (so that a connection that flaps reaches the cap too)
            ASSERT_TRUE(a.lost(1, t0 + 20000));
            ASSERT_TRUE(a.returning(1, t0 + 20100));
            ASSERT_EQ(a.away_ms(1, t0 + 20300), 7300u);                              // (while it goes on, the real time: 7 s and 300 ms)
            ASSERT_TRUE(a.caught_up(1, t0 + 20300));
            ASSERT_EQ(a.away_ms(1, t0 + 20300), 12000u);                             // 7 s + the minimum of 5 s
            ASSERT_EQ(a.pause_ms(t0 + 20300), 7000u + 5000u);                        // the pause was 300 ms: the minimum counts for the cap, as for the vote
            ASSERT_EQ(a.held_ms(t0 + 20300), 7300u);                                 // (and what the match really waited: 7 s and 300 ms)
            // episode 3: 9 s, over the minimum
            ASSERT_TRUE(a.lost(1, t0 + 30000));
            ASSERT_EQ(a.away_ms(1, t0 + 30100), 12100u);
            ASSERT_TRUE(a.returning(1, t0 + 38000));
            ASSERT_TRUE(a.caught_up(1, t0 + 39000));
            ASSERT_EQ(a.away_ms(1, t0 + 39000), 21000u);                             // 12 s + 9 s
            ASSERT_EQ(a.pause_ms(t0 + 39000), 12000u + 9000u);
            ASSERT_EQ(a.held_ms(t0 + 39000), 7300u + 9000u);
            ASSERT_EQ(a.rejoins(), 3u);
            ASSERT_EQ(a.away_ms(0, t0 + 39000), 0u);                                 // the others have not been away
            ASSERT_EQ(a.cap_s(t0 + 39000), static_cast<uint16_t>((1800000u - 21000u + 999u) / 1000u));
        }
        {   // a catch-up that fails: the seat is Absent again and its time kept running from the loss; the pause goes on
            Attendance a;
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0 + 1000) && a.returning(1, t0 + 11000));
            ASSERT_TRUE(a.progress(1, 30, t0 + 12000));
            ASSERT_TRUE(a.catch_up_failed(1, t0 + 16000));
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.percent(1), 0);
            ASSERT_EQ(a.away_ms(1, t0 + 17000), 16000u);
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.pause_ms(t0 + 17000), 16000u);
            ASSERT_TRUE(a.returning(1, t0 + 18000));                                 // a second try (after the first failed): the percent starts again
            ASSERT_TRUE(a.progress(1, 5, t0 + 18100));                               // (5 is more than the 0 of the new try, less than the 30 of the old)
            ASSERT_TRUE(a.returning(1, t0 + 19000));                                 // a third Hello while the second try goes on: the try goes on as it was (REWRITTEN: it used to start again from 0 with a new stall clock, which let a key holder keep the pause alive for ever)
            ASSERT_EQ(a.percent(1), 5);
            ASSERT_EQ(a.away_ms(1, t0 + 20000), 19000u);
            ASSERT_TRUE(a.caught_up(1, t0 + 21000));
            ASSERT_EQ(a.away_ms(1, t0 + 21000), 20000u);
        }
        {   // a seat whose old link was not known to be dead: the new link takes it over, the seat is away from that moment on
            Attendance a;
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.returning(1, t0 + 5000));
            ASSERT_EQ(a.state(1), S::CatchingUp);
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.away_ms(1, t0 + 5000), 0u);
            ASSERT_EQ(a.away_ms(1, t0 + 7000), 2000u);                               // not since the start of the match
            ASSERT_EQ(a.pause_ms(t0 + 7000), 5000u);                                 // (2 s were real: a pause counts at least 5 s toward the cap)
            ASSERT_EQ(a.held_ms(t0 + 7000), 2000u);
            ASSERT_TRUE(a.caught_up(1, t0 + 7000));
            ASSERT_EQ(a.away_ms(1, t0 + 7000), 5000u);                               // a loss counts at least 5 s
            ASSERT_EQ(a.pause_ms(t0 + 7000), 5000u);
            ASSERT_EQ(a.held_ms(t0 + 7000), 2000u);
        }
        // ---- dropped is final, from every state, and ends the absence ----
        for (const int from : {0, 1, 2}) {
            Attendance a;
            a.seat_humans(0b0111, t0);
            if (from >= 1) ASSERT_TRUE(a.lost(1, t0 + 1000));
            if (from >= 2) ASSERT_TRUE(a.returning(1, t0 + 2000));
            ASSERT_TRUE(a.dropped(1, t0 + 10000));
            ASSERT_EQ(a.state(1), S::Dropped);
            ASSERT_FALSE(a.paused());                                                // nobody else is away
            ASSERT_EQ(a.connected_humans(), 2);
            const uint32_t away = a.away_ms(1, t0 + 10000);
            ASSERT_EQ(away, from == 0 ? 0u : 9000u);
            ASSERT_EQ(a.away_ms(1, t0 + 500000), away);                              // frozen
            ASSERT_EQ(a.pause_ms(t0 + 500000), from == 0 ? 0u : 9000u);
            ASSERT_FALSE(a.lost(1, t0 + 20000) || a.returning(1, t0 + 20000) || a.progress(1, 99, t0 + 20000) || a.caught_up(1, t0 + 20000) || a.catch_up_failed(1, t0 + 20000) || a.dropped(1, t0 + 20000));
            ASSERT_FALSE(a.vote(1, 2, true, t0 + 20000));                            // a dropped seat does not vote
            ASSERT_EQ(a.state(1), S::Dropped);
            ASSERT_EQ(a.drops_by_vote(), 0u);                                        // (a seat that left is no drop by vote or by the cap)
            ASSERT_EQ(a.drops_by_cap(), 0u);
            ASSERT_TRUE(a.update(t0 + 60000).empty());
        }
        // ---- the pause counts every moment once, however many seats are away ----
        {
            Attendance a;
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0));                                              // seat 1 from 0 to 10 s, seat 2 from 4 s to 12 s: the pause is 0 to 12 s
            ASSERT_TRUE(a.lost(2, t0 + 4000));
            ASSERT_EQ(a.pause_ms(t0 + 6000), 6000u);
            ASSERT_TRUE(a.returning(1, t0 + 8000));
            ASSERT_TRUE(a.caught_up(1, t0 + 10000));
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.pause_ms(t0 + 11000), 11000u);                               // (seat 2 is still away)
            ASSERT_TRUE(a.returning(2, t0 + 11500));
            ASSERT_EQ(a.pause_ms(t0 + 11800), 11800u);                               // catching up is pause too
            ASSERT_TRUE(a.caught_up(2, t0 + 12000));
            ASSERT_FALSE(a.paused());
            ASSERT_EQ(a.pause_ms(t0 + 12000), 12000u);
            ASSERT_EQ(a.pause_ms(t0 + 80000), 12000u);
            ASSERT_EQ(a.away_ms(1, t0 + 80000), 10000u);
            ASSERT_EQ(a.away_ms(2, t0 + 80000), 8000u);
            ASSERT_EQ(a.rejoins(), 2u);
            // a blip of a second: 5 s for the seat, and 5 s for the cap (what the match really waited was 1 s)
            ASSERT_TRUE(a.lost(2, t0 + 100000) && a.returning(2, t0 + 100500) && a.caught_up(2, t0 + 101000));
            ASSERT_EQ(a.away_ms(2, t0 + 101000), 13000u);
            ASSERT_EQ(a.pause_ms(t0 + 101000), 12000u + 5000u);
            ASSERT_EQ(a.held_ms(t0 + 101000), 12000u + 1000u);
        }
        {   // the match begins again: everything that was before is forgotten
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(1, t0 + 100) && a.returning(1, t0 + 200) && a.caught_up(1, t0 + 300) && a.lost(2, t0 + 400) && a.dropped(3, t0 + 500));
            a.seat_humans(0b0110, t0 + 1000);
            ASSERT_TRUE(a.state(0) == S::Empty && a.state(1) == S::Present && a.state(2) == S::Present && a.state(3) == S::Empty);
            ASSERT_TRUE(!a.paused() && a.pause_ms(t0 + 1000) == 0 && a.away_ms(1, t0 + 1000) == 0 && a.rejoins() == 0 && a.connected_humans() == 2);
        }
    } TEST_END();

    TEST_CASE("N2.43 Attendance: The Vote Opens At 30 s In All, One Seat At A Time (The One Away Longest, Never One That Is Catching Up); More Than Half Of The Connected Players Win It (1, 2 And 3 Connected, Every Number Of Votes); A Lost Voter Leaves The Count") {
        using S = Attendance::State;
        const uint32_t t0 = 5000000;
        // ---- the vote opens when the seat's total reaches 30 s: not a millisecond before, also over several absences ----
        {
            Attendance a;
            a.seat_humans(0b1011, t0);
            ASSERT_TRUE(a.lost(3, t0));
            ASSERT_EQ(a.vote_subject(t0 + 29999), 255);
            ASSERT_FALSE(a.vote(0, 3, true, t0 + 29999));                            // no vote is open: the choice is ignored
            ASSERT_EQ(a.presence_for(0, t0 + 29999).vote_seat, 255);
            ASSERT_EQ(a.vote_subject(t0 + 30000), 3);
            ASSERT_EQ(a.presence_for(0, t0 + 30000).vote_seat, 3);
            ASSERT_TRUE(a.update(t0 + 29999).empty() && a.update(t0 + 30000).empty());        // (no vote was cast)
            Attendance b;                                                            // 7 s, a return, then 23 s more: the total is 30 s at the same instant
            b.seat_humans(0b1011, t0);
            ASSERT_TRUE(b.lost(3, t0) && b.returning(3, t0 + 6000) && b.caught_up(3, t0 + 7000) && b.lost(3, t0 + 20000));
            ASSERT_EQ(b.away_ms(3, t0 + 42999), 29999u);
            ASSERT_EQ(b.vote_subject(t0 + 42999), 255);
            ASSERT_EQ(b.vote_subject(t0 + 43000), 3);
            Attendance::Config many;                                                 // (a seat is taken back twelve times a minute by default: this one is cut every 3 s, so the rule is raised to its most)
            many.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            Attendance c(many);                                                      // six losses of a second each count 5 s each: the vote opens at 30 s of loss in all ...
            c.seat_humans(0b1011, t0);
            for (int i = 0; i < 6; ++i) {
                const uint32_t at = t0 + static_cast<uint32_t>(i) * 3000u;
                ASSERT_TRUE(c.lost(3, at) && c.returning(3, at + 500) && c.caught_up(3, at + 1000));
            }
            ASSERT_EQ(c.away_ms(3, t0 + 20000), 30000u);                             // six blips of a second are 30 s
            ASSERT_TRUE(c.lost(3, t0 + 20000));
            ASSERT_EQ(c.vote_subject(t0 + 20000), 3);                                // ... so the next loss is put to the vote at once
        }
        // ---- the rule: 2 * votes > connected; 1, 2 and 3 connected players (a match has four seats: one that is missing leaves at most three to vote), every subset that chooses Continue ----
        for (uint8_t connected = 1; connected <= 3; ++connected) {
            uint8_t mask = 0b1000;                                                   // seat 3 is the one that is missing; seats 0 .. connected - 1 are connected
            for (uint8_t s = 0; s < connected; ++s) mask = static_cast<uint8_t>(mask | (1u << s));
            for (unsigned chose = 0; chose < (1u << connected); ++chose) {           // the voters that choose Continue (bit v); the others choose Keep waiting
                for (const bool others_abstain : {false, true}) {
                    Attendance a;
                    a.seat_humans(mask, t0);
                    ASSERT_TRUE(a.lost(3, t0));
                    const uint32_t now = t0 + 31000;
                    unsigned votes = 0;
                    for (uint8_t v = 0; v < connected; ++v) {
                        const bool cont = (chose & (1u << v)) != 0;
                        if (!cont && others_abstain) continue;
                        ASSERT_TRUE(a.vote(v, 3, cont, now));
                        votes += cont ? 1u : 0u;
                    }
                    ASSERT_EQ(a.votes_for_continue(3), votes);
                    const PresenceMsg p = a.presence_for(0, now);
                    ASSERT_TRUE(p.vote_seat == 3 && p.votes_continue == votes && p.voters == connected);
                    const bool wins = 2 * votes > connected;                         // 1 of 1; 2 of 2; 2 of 3 (not 1 of 2: that is half)
                    const std::vector<uint8_t> dropped = a.update(now);
                    ASSERT_EQ(dropped.size(), wins ? size_t{1} : size_t{0});
                    ASSERT_EQ(a.state(3), wins ? S::Dropped : S::Absent);
                    ASSERT_EQ(a.drops_by_vote(), wins ? 1u : 0u);
                    ASSERT_EQ(a.drops_by_cap(), 0u);
                    if (wins) ASSERT_TRUE(dropped[0] == 3 && !a.paused());
                    else ASSERT_TRUE(a.paused() && a.vote_subject(now) == 3);
                }
            }
        }
        // the rule itself, for every number of connected players that the arithmetic knows (a match has four seats, so a missing one leaves three at most; 4 shows the rule is general)
        for (uint8_t connected = 0; connected <= 4; ++connected) {
            for (uint8_t votes = 0; votes <= connected; ++votes) {
                ASSERT_EQ(Attendance::vote_won(votes, connected), connected > 0 && votes * 2 > connected);
            }
        }
        ASSERT_TRUE(Attendance::vote_won(1, 1) && Attendance::vote_won(2, 2) && Attendance::vote_won(2, 3) && Attendance::vote_won(3, 4));        // 1 of 1, 2 of 2, 2 of 3, 3 of 4
        ASSERT_FALSE(Attendance::vote_won(0, 1) || Attendance::vote_won(1, 2) || Attendance::vote_won(1, 3) || Attendance::vote_won(2, 4) || Attendance::vote_won(0, 0));    // half is not more than half
        ASSERT_FALSE(Attendance::vote_won(1, 0) || Attendance::vote_won(4, 0));                                                                                       // nobody connected: never
        {   // nobody connected can never win (and one seat of one is all of them)
            Attendance a;
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.lost(0, t0 + 1000));                      // both away: nobody is there to vote
            ASSERT_EQ(a.connected_humans(), 0);
            ASSERT_TRUE(a.update(t0 + 600000).empty());                              // (the cap is 30 minutes)
            ASSERT_EQ(a.vote_subject(t0 + 600000), 1);
            ASSERT_FALSE(a.vote(0, 1, true, t0 + 600000));                           // an absent seat does not vote
        }
        // ---- a choice can be changed; a seat that does not vote is as one that wants to keep waiting ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(3, t0));
            const uint32_t now = t0 + 30000;
            ASSERT_TRUE(a.vote(0, 3, true, now) && a.vote(1, 3, true, now));        // two of three: a majority ...
            ASSERT_TRUE(a.vote(1, 3, false, now));                                   // ... until the second thinks again
            ASSERT_EQ(a.votes_for_continue(3), 1u);
            ASSERT_TRUE(a.presence_for(1, now).your_vote == 1 && a.presence_for(0, now).your_vote == 2 && a.presence_for(2, now).your_vote == 0);
            ASSERT_TRUE(a.update(now).empty());
            ASSERT_TRUE(a.vote(1, 3, true, now));
            ASSERT_TRUE(a.vote(2, 3, false, now + 1));
            ASSERT_EQ(a.presence_for(0, now + 1).votes_continue, 2);
            const std::vector<uint8_t> dropped = a.update(now + 1);                  // 2 of 3 chose Continue; the third chose to keep waiting
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 3);
        }
        // ---- a voter that is lost leaves the count at once, and its choice does not come back with it ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(3, t0));
            const uint32_t now = t0 + 30000;
            ASSERT_TRUE(a.vote(0, 3, true, now) && a.vote(1, 3, true, now));        // 2 of 3: it would win at the next update
            ASSERT_TRUE(a.lost(1, now + 10));                                        // ... but one of the two voters is lost first: 1 of 2 is only half
            ASSERT_EQ(a.connected_humans(), 2);
            ASSERT_EQ(a.votes_for_continue(3), 1u);
            ASSERT_EQ(a.presence_for(0, now + 10).voters, 2);
            ASSERT_TRUE(a.update(now + 10).empty());
            ASSERT_TRUE(a.returning(1, now + 20000) && a.caught_up(1, now + 21000));
            ASSERT_EQ(a.connected_humans(), 3);
            ASSERT_EQ(a.votes_for_continue(3), 1u);                                  // its old choice is gone: it must choose again
            ASSERT_EQ(a.presence_for(1, now + 21000).your_vote, 0);
            ASSERT_TRUE(a.update(now + 21000).empty());
            ASSERT_TRUE(a.vote(1, 3, true, now + 21000));
            ASSERT_EQ(a.update(now + 21000).size(), 1u);
            // the same with a voter that is dropped (it left): its choice leaves the count
            Attendance d;
            d.seat_humans(0b1111, t0);
            ASSERT_TRUE(d.lost(3, t0) && d.vote(0, 3, true, now) && d.vote(1, 3, true, now) && d.dropped(1, now + 5));
            ASSERT_EQ(d.votes_for_continue(3), 1u);
            ASSERT_TRUE(d.update(now + 5).empty());                                  // 1 of the 2 that are left
        }
        // ---- votes that do not count: wrong subject, a voter that is not connected, a seat that is no seat; and no violation (only false) ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(2, t0) && a.lost(3, t0 + 10000));
            const uint32_t now = t0 + 31000;                                         // seat 2 has been away 31 s, seat 3 21 s: the vote is about 2
            ASSERT_EQ(a.vote_subject(now), 2);
            ASSERT_FALSE(a.vote(0, 3, true, now));                                   // about seat 3: it is not the subject
            ASSERT_FALSE(a.vote(0, 1, true, now));                                   // about a seat that is present
            ASSERT_FALSE(a.vote(0, 0, true, now));
            ASSERT_FALSE(a.vote(2, 2, true, now));                                   // from the seat that is missing
            ASSERT_FALSE(a.vote(3, 2, true, now));                                   // from another seat that is missing
            ASSERT_FALSE(a.vote(4, 2, true, now));                                   // from a seat that does not exist
            ASSERT_FALSE(a.vote(0, 4, true, now));
            ASSERT_FALSE(a.vote(255, 255, true, now));
            ASSERT_EQ(a.votes_for_continue(2), 0u);
            ASSERT_EQ(a.votes_for_continue(3), 0u);
            ASSERT_TRUE(a.returning(3, now) && a.vote_subject(now) == 2);            // a seat that is catching up cannot vote either
            ASSERT_FALSE(a.vote(3, 2, true, now));
            ASSERT_TRUE(a.vote(0, 2, true, now));
        }
        // ---- one vote at a time: the seat that has been away longest; of a tie the lowest seat; a seat that is catching up is never the subject; votes about a seat are gone when it changes state ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(2, t0) && a.lost(3, t0));                             // lost together: a tie
            ASSERT_EQ(a.vote_subject(t0 + 40000), 2);
            const PresenceMsg tie = a.presence_for(0, t0 + 40000);
            ASSERT_EQ(tie.missing.size(), 2u);
            ASSERT_TRUE(tie.missing[0].seat == 2 && tie.missing[1].seat == 3 && tie.missing[0].waited_s == 40 && tie.missing[1].waited_s == 40);       // (a tie: the lowest seat first)
            Attendance b;
            b.seat_humans(0b1111, t0);
            ASSERT_TRUE(b.lost(3, t0) && b.lost(1, t0 + 5000));                      // 3 has been away longer than 1: 3, whatever the numbers of the seats
            ASSERT_EQ(b.vote_subject(t0 + 36000), 3);
            ASSERT_EQ(b.vote_subject(t0 + 29999), 255);
            ASSERT_EQ(b.presence_for(0, t0 + 36000).missing.size(), 2u);
            ASSERT_EQ(b.presence_for(0, t0 + 36000).missing[0].seat, 3);              // longest away first
            // seat 3 (away 60 s) comes back and is catching up: not the subject any more, though it has been away longest; seat 1 (55 s) is
            Attendance c;
            c.seat_humans(0b1111, t0);
            ASSERT_TRUE(c.lost(3, t0) && c.lost(1, t0 + 5000));
            const uint32_t now = t0 + 60000;
            ASSERT_EQ(c.vote_subject(now), 3);
            ASSERT_TRUE(c.vote(0, 3, true, now));
            ASSERT_TRUE(c.returning(3, now));
            ASSERT_EQ(c.vote_subject(now), 1);
            ASSERT_EQ(c.presence_for(0, now).vote_seat, 1);
            ASSERT_EQ(c.presence_for(0, now).missing[0].seat, 3);                    // (still first in the list: away longest) and not the vote's seat
            ASSERT_EQ(c.votes_for_continue(3), 0u);                                  // the votes about it are gone: it is back
            ASSERT_TRUE(c.vote(0, 1, true, now));
            ASSERT_EQ(c.votes_for_continue(1), 1u);
            ASSERT_TRUE(c.catch_up_failed(3, now + 1000));                           // it fails: absent again, with the longest total: the subject again, with no votes
            ASSERT_EQ(c.vote_subject(now + 1000), 3);
            ASSERT_EQ(c.votes_for_continue(3), 0u);
            ASSERT_FALSE(c.vote(0, 1, true, now + 1000));                            // the vote is about 3 now: a choice about 1 is ignored
            ASSERT_EQ(c.votes_for_continue(1), 1u);                                  // (what was chosen about 1 stays with 1 until it changes state)
            // the votes of one subject are gone when it returns, whether it catches up or not
            ASSERT_TRUE(c.vote(0, 3, true, now + 1000));
            ASSERT_TRUE(c.returning(3, now + 2000) && c.catch_up_failed(3, now + 3000));
            ASSERT_EQ(c.votes_for_continue(3), 0u);
        }
        // ---- one vote at a time, through update(): the first seat is dropped by its vote, the second starts with no votes ----
        {
            Attendance a;
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.lost(2, t0 + 2000));
            const uint32_t now = t0 + 40000;
            ASSERT_EQ(a.vote_subject(now), 1);
            ASSERT_TRUE(a.vote(0, 1, true, now) && a.vote(3, 1, true, now));        // 2 of the 2 that are connected
            std::vector<uint8_t> dropped = a.update(now);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1);                     // seat 1 only: seat 2 was not asked yet
            ASSERT_EQ(a.state(2), S::Absent);
            ASSERT_EQ(a.vote_subject(now), 2);                                       // now it is
            ASSERT_EQ(a.votes_for_continue(2), 0u);
            ASSERT_TRUE(a.paused());                                                 // (seat 2 is still away: the match still waits)
            ASSERT_TRUE(a.update(now + 100).empty());
            ASSERT_TRUE(a.vote(0, 2, true, now + 200) && a.vote(3, 2, true, now + 200));
            dropped = a.update(now + 200);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 2);
            ASSERT_FALSE(a.paused());
            ASSERT_EQ(a.drops_by_vote(), 2u);
        }
    } TEST_END();

    TEST_CASE("N2.44 Attendance: A Catch-Up That Shows No Progress For 20 s Is Let Go; No Seat Is Dropped For Its Own Time, The Match's Total Pause Is Capped (Every Seat That Is Not Present Is Dropped At The Cap, Absent Or Catching Up, Whatever Progress It Shows) And cap_s Counts Down; The Same Sequence Across The Wrap Of The Clock; Presence Is Always Something The Decoder Takes") {
        using S = Attendance::State;
        // ---- the stall: 20 s without progress (a higher percent) and the seat is absent again ----
        {
            const uint32_t t0 = 1000000;
            Attendance a;
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 5000));
            ASSERT_TRUE(a.update(t0 + 24999).empty() && a.state(1) == S::CatchingUp);        // 19,999 ms since the return
            ASSERT_TRUE(a.progress(1, 10, t0 + 15000));                                       // a sign of life at 10 s into it: the clock starts again
            ASSERT_TRUE(a.update(t0 + 34999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_FALSE(a.progress(1, 10, t0 + 30000));                                      // the same percent again is no progress: the clock goes on from 15 s
            ASSERT_TRUE(a.update(t0 + 34999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_TRUE(a.update(t0 + 35000).empty());                                        // 20,000 ms: let go (not dropped: it is Absent again, with its time)
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_TRUE(a.paused());
            ASSERT_EQ(a.away_ms(1, t0 + 35000), 35000u);
            ASSERT_EQ(a.percent(1), 0);
            ASSERT_TRUE(a.returning(1, t0 + 36000) && a.progress(1, 1, t0 + 40000));         // it may try again, and each try has its own 20 s
            ASSERT_TRUE(a.update(t0 + 59999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_TRUE(a.update(t0 + 60000).empty() && a.state(1) == S::Absent);
            // a seat that is only Absent has no stall clock
            ASSERT_TRUE(a.update(t0 + 900000).empty() && a.state(1) == S::Absent);
        }
        // ---- no seat is dropped for its own time ----
        {
            const uint32_t t0 = 1000000;
            Attendance::Config cfg;
            cfg.max_pause_ms = 24u * 3600u * 1000u;                                           // (the longest that a room may be set to)
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0));
            ASSERT_EQ(a.cap_s(t0), static_cast<uint16_t>(0xFFFF));                            // (a day is more than a u16 of seconds: it is told as "more")
            for (uint32_t hours = 1; hours <= 20; hours += 3) {
                ASSERT_TRUE(a.update(t0 + hours * 3600u * 1000u).empty());                    // ten hours, nineteen: still held
                ASSERT_EQ(a.state(1), S::Absent);
            }
            ASSERT_TRUE(a.update(t0 + 24u * 3600u * 1000u - 1).empty());
            const std::vector<uint8_t> at_cap = a.update(t0 + 24u * 3600u * 1000u);          // only the cap ends it
            ASSERT_TRUE(at_cap.size() == 1 && at_cap[0] == 1);
            ASSERT_EQ(a.cap_s(t0 + 24u * 3600u * 1000u), 0);
        }
        // ---- the cap: the total of the match's pauses; every Absent seat goes with it, whatever its own time ----
        {
            const uint32_t t0 = 3000000;
            Attendance::Config cfg;
            cfg.max_pause_ms = 60000;
            {   // one seat: dropped exactly when the pauses reach 60 s
                Attendance a(cfg);
                a.seat_humans(0b0111, t0);
                ASSERT_TRUE(a.lost(1, t0 + 5000));
                ASSERT_TRUE(a.update(t0 + 64999).empty() && a.state(1) == S::Absent);
                ASSERT_EQ(a.cap_left_ms(t0 + 64999), 1u);
                const std::vector<uint8_t> dropped = a.update(t0 + 65000);
                ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1 && a.state(1) == S::Dropped);
                ASSERT_TRUE(a.cap_reached(t0 + 65000) && a.cap_left_ms(t0 + 65000) == 0 && a.cap_s(t0 + 65000) == 0);
                ASSERT_EQ(a.drops_by_cap(), 1u);
                ASSERT_EQ(a.drops_by_vote(), 0u);
                ASSERT_FALSE(a.paused());
                ASSERT_EQ(a.pause_ms(t0 + 70000), 60000u);
                // the budget is gone: the next loss is dropped by the next update, however brief
                ASSERT_TRUE(a.lost(2, t0 + 100000));
                const std::vector<uint8_t> at_once = a.update(t0 + 100000);
                ASSERT_TRUE(at_once.size() == 1 && at_once[0] == 2 && a.state(2) == S::Dropped);
                ASSERT_EQ(a.drops_by_cap(), 2u);
            }
            {   // two seats: both go at the cap, the one that was away 10 s as the one that was away 60 s (no per-seat time)
                Attendance a(cfg);
                a.seat_humans(0b0111, t0);
                ASSERT_TRUE(a.lost(1, t0) && a.lost(2, t0 + 50000));
                const std::vector<uint8_t> dropped = a.update(t0 + 60000);
                ASSERT_TRUE(dropped.size() == 2 && dropped[0] == 1 && dropped[1] == 2);
                ASSERT_EQ(a.away_ms(2, t0 + 60000), 10000u);
                ASSERT_EQ(a.drops_by_cap(), 2u);
            }
            {   // the pauses add up over the match: 20 s + 30 s, and the third pause is dropped when it reaches the 10 s that are left
                Attendance a(cfg);
                a.seat_humans(0b0111, t0);
                ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 19000) && a.caught_up(1, t0 + 20000));
                ASSERT_EQ(a.cap_left_ms(t0 + 50000), 40000u);
                ASSERT_TRUE(a.lost(2, t0 + 100000) && a.returning(2, t0 + 129000) && a.caught_up(2, t0 + 130000));
                ASSERT_EQ(a.pause_ms(t0 + 200000), 50000u);
                ASSERT_EQ(a.cap_left_ms(t0 + 200000), 10000u);
                ASSERT_TRUE(a.lost(1, t0 + 300000));
                ASSERT_TRUE(a.update(t0 + 309999).empty() && a.state(1) == S::Absent);
                const std::vector<uint8_t> dropped = a.update(t0 + 310000);
                ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1);
            }
            {   // the cap is ABSOLUTE: a seat that is catching up and makes progress is dropped at the cap like every seat that is not present (REWRITTEN: it used to keep its chance for as long as it
                // made progress, and a key holder that said Hello again and again, each time with a little progress, held a room paused for ever)
                Attendance a(cfg);
                a.seat_humans(0b0111, t0);
                ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 30000));
                ASSERT_TRUE(a.progress(1, 5, t0 + 40000) && a.progress(1, 6, t0 + 55000));
                ASSERT_TRUE(a.update(t0 + 59999).empty() && a.state(1) == S::CatchingUp);     // 1 ms before the cap: it is getting on
                ASSERT_FALSE(a.cap_reached(t0 + 59999));
                const std::vector<uint8_t> dropped = a.update(t0 + 60000);                   // the cap: dropped, whatever progress it shows
                ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1 && a.state(1) == S::Dropped);
                ASSERT_TRUE(a.cap_reached(t0 + 60000) && a.cap_s(t0 + 60000) == 0);
                ASSERT_EQ(a.drops_by_cap(), 1u);
                ASSERT_FALSE(a.paused());
                // a seat that catches up in time is back; a later loss, which counts 5 s at least, finds the cap reached and is dropped at once
                Attendance b(cfg);
                b.seat_humans(0b0111, t0);
                ASSERT_TRUE(b.lost(1, t0) && b.returning(1, t0 + 30000) && b.progress(1, 50, t0 + 50000));
                ASSERT_TRUE(b.update(t0 + 55000).empty() && b.progress(1, 99, t0 + 55000) && b.caught_up(1, t0 + 56000));
                ASSERT_TRUE(b.state(1) == S::Present && !b.paused());
                ASSERT_EQ(b.pause_ms(t0 + 56000), 56000u);
                ASSERT_TRUE(b.lost(2, t0 + 70000));                                           // (56 s + at least 5 s: the budget of 60 s is gone)
                const std::vector<uint8_t> next = b.update(t0 + 70000);
                ASSERT_TRUE(next.size() == 1 && next[0] == 2);
                // a catch-up that was not getting on at all is let go before the cap can wait for it: 20 s without progress
                Attendance c(cfg);
                c.seat_humans(0b0111, t0);
                ASSERT_TRUE(c.lost(1, t0) && c.returning(1, t0 + 30000));
                ASSERT_TRUE(c.update(t0 + 49999).empty() && c.state(1) == S::CatchingUp);
                const std::vector<uint8_t> stalled = c.update(t0 + 50000);                    // 20 s of no progress: Absent again; the pause is 50 s, the cap 60 s: held
                ASSERT_TRUE(stalled.empty() && c.state(1) == S::Absent);
            }
            {   // the cap and a vote at the same moment: the vote's seat is the vote's, the rest are the cap's
                Attendance a(cfg);
                a.seat_humans(0b1111, t0);
                ASSERT_TRUE(a.lost(1, t0) && a.lost(2, t0 + 1000));
                ASSERT_TRUE(a.vote(0, 1, true, t0 + 59000) && a.vote(3, 1, true, t0 + 59000));          // the two that are connected
                ASSERT_FALSE(a.vote(2, 1, true, t0 + 59000));                                            // (seat 2 is away: it cannot vote)
                const std::vector<uint8_t> dropped = a.update(t0 + 60000);
                ASSERT_TRUE(dropped.size() == 2 && dropped[0] == 1 && dropped[1] == 2);
                ASSERT_TRUE(a.drops_by_vote() == 1 && a.drops_by_cap() == 1);
            }
        }
        // ---- cap_s: seconds that are left, rounded up, so 0 means the cap is reached; 0xFFFF for more than a u16 holds ----
        {
            const uint32_t t0 = 777;
            Attendance::Config cfg;
            cfg.max_pause_ms = 60000;
            Attendance a(cfg);
            a.seat_humans(0b0011, t0);
            ASSERT_EQ(a.cap_s(t0), 60);                                                       // not paused: the whole budget
            ASSERT_EQ(a.cap_s(t0 + 99999), 60);                                               // (and it does not run down while nobody is away)
            ASSERT_TRUE(a.lost(1, t0 + 5000));
            ASSERT_EQ(a.cap_s(t0 + 5000), 55);                                                // (a pause counts at least 5 s, from its first moment)
            ASSERT_EQ(a.cap_s(t0 + 5001), 55);
            ASSERT_EQ(a.cap_s(t0 + 10000), 55);                                               // 5 s really: the minimum and the pause agree
            ASSERT_EQ(a.cap_s(t0 + 10001), 55);                                               // 54,999 ms: still "55" (rounded up)
            ASSERT_EQ(a.cap_s(t0 + 11000), 54);
            ASSERT_EQ(a.cap_s(t0 + 11001), 54);
            ASSERT_EQ(a.cap_s(t0 + 34000), 31);
            ASSERT_EQ(a.cap_s(t0 + 64000), 1);
            ASSERT_EQ(a.cap_s(t0 + 64001), 1);
            ASSERT_EQ(a.cap_s(t0 + 64999), 1);
            ASSERT_EQ(a.cap_s(t0 + 65000), 0);
            ASSERT_EQ(a.cap_s(t0 + 99999), 0);
            Attendance::Config long_cfg;
            long_cfg.max_pause_ms = 86400u * 1000u;
            Attendance b(long_cfg);
            b.seat_humans(0b0011, t0);
            ASSERT_EQ(b.cap_s(t0), 0xFFFF);
            ASSERT_TRUE(b.lost(1, t0));
            ASSERT_EQ(b.cap_s(t0 + 20000000), 0xFFFF);                                        // 86,400 - 20,000 s = 66,400 s: more than a u16
            ASSERT_EQ(b.cap_s(t0 + 21000000), static_cast<uint16_t>(65400));                  // 65,400 s
            ASSERT_EQ(b.cap_s(t0 + 20865000), static_cast<uint16_t>(65535));                  // 65,535 s left: exactly the largest
            ASSERT_EQ(b.cap_s(t0 + 20864999), 0xFFFF);                                        // 65,535.001 s
            ASSERT_EQ(b.cap_s(t0 + 20865001), static_cast<uint16_t>(65535));                  // 65,534.999 s rounds up to 65,535
            ASSERT_EQ(b.cap_s(t0 + 20866000), static_cast<uint16_t>(65534));
            // the wait that Presence tells saturates at 0xFFFF seconds too (a pause of a day: the cap of such a room is 24 hours)
            ASSERT_EQ(b.presence_for(0, t0 + 65534999).missing[0].waited_s, 65534);
            ASSERT_EQ(b.presence_for(0, t0 + 65535000).missing[0].waited_s, 0xFFFF);
            ASSERT_EQ(b.presence_for(0, t0 + 70000000).missing[0].waited_s, 0xFFFF);
            ASSERT_EQ(b.presence_for(0, t0 + 70000000).cap_s, 16400);                       // (86,400 s of cap less the 70,000 s that have gone)
            Attendance::Config zero_cfg;                                                      // a room with no pause budget at all: the first update drops any seat that is away
            zero_cfg.max_pause_ms = 0;
            Attendance z(zero_cfg);
            z.seat_humans(0b0011, t0);
            ASSERT_EQ(z.cap_s(t0), 0);
            ASSERT_TRUE(z.lost(1, t0 + 10));
            ASSERT_EQ(z.update(t0 + 10).size(), 1u);
        }
        // ---- the same sequence, started at different points of the 32-bit clock (8 s before the wrap, at the wrap, over the signed half), makes the same decisions at the same offsets ----
        {
            auto scenario = [](uint32_t t0) {
                std::vector<int64_t> trace;
                auto at = [&](uint32_t ms) { return t0 + ms; };
                Attendance::Config cfg;
                cfg.max_pause_ms = 120000;
                Attendance a(cfg);
                a.seat_humans(0b1111, at(0));
                auto note = [&](uint32_t ms) {                                                // what can be seen at an offset
                    trace.push_back(ms);
                    for (uint8_t s = 0; s < 4; ++s) {
                        trace.push_back(static_cast<int64_t>(a.state(s)));
                        trace.push_back(a.away_ms(s, at(ms)));
                    }
                    trace.push_back(a.pause_ms(at(ms)));
                    trace.push_back(a.cap_s(at(ms)));
                    trace.push_back(a.vote_subject(at(ms)));
                    trace.push_back(a.connected_humans());
                    const PresenceMsg p = a.presence_for(0, at(ms));
                    trace.push_back(p.vote_seat);
                    trace.push_back(p.votes_continue);
                    trace.push_back(static_cast<int64_t>(p.missing.size()));
                    for (const auto& e : p.missing) {
                        trace.push_back(e.seat);
                        trace.push_back(e.waited_s);
                        trace.push_back(e.progress);
                    }
                };
                note(0);
                trace.push_back(a.lost(3, at(2000)));
                note(2000);
                trace.push_back(a.lost(2, at(9000)));
                trace.push_back(a.returning(2, at(11000)));
                trace.push_back(a.progress(2, 40, at(12000)));
                note(15000);
                trace.push_back(a.caught_up(2, at(17000)));
                note(17000);
                note(31999);                                                                  // seat 3 has been away 29,999 ms
                note(32000);                                                                  // 30,000: the vote opens
                trace.push_back(a.vote(0, 3, true, at(33000)));
                trace.push_back(a.vote(1, 3, false, at(33000)));
                note(33000);
                for (uint32_t ms = 34000; ms <= 70000; ms += 6000) {
                    for (uint8_t seat : a.update(at(ms))) trace.push_back(1000 + seat);
                    note(ms);
                }
                trace.push_back(a.vote(2, 3, true, at(70500)));
                for (uint8_t seat : a.update(at(70600))) trace.push_back(2000 + seat);       // 2 of 3 voted Continue: seat 3 is dropped
                note(70600);
                trace.push_back(a.lost(1, at(80000)));
                trace.push_back(a.returning(1, at(81000)));
                for (uint32_t ms = 82000; ms <= 135000; ms += 1000) {                         // seat 1 never makes progress: let go at 20 s, then the cap (120 s of pause in all) drops it
                    for (uint8_t seat : a.update(at(ms))) trace.push_back(3000 + seat);
                }
                note(135000);
                trace.push_back(a.drops_by_vote());
                trace.push_back(a.drops_by_cap());
                trace.push_back(a.rejoins());
                return trace;
            };
            const std::vector<int64_t> reference = scenario(1000000);
            ASSERT_TRUE(reference.size() > 100);
            for (const uint32_t start : {0u, 0xFFFFE000u, 0xFFFFFFFFu, 0xFFFFFFFFu - 40000u, 0x7FFFF000u, 0x80000000u, 0x80000000u - 1u, 0xFFFFFFFFu - 100000u}) {
                ASSERT_TRUE(scenario(start) == reference);
            }
            bool saw_drop_by_vote = false;
            bool saw_drop_by_cap = false;
            for (const int64_t v : reference) {
                saw_drop_by_vote = saw_drop_by_vote || v == 2003;
                saw_drop_by_cap = saw_drop_by_cap || v == 3001;
            }
            ASSERT_TRUE(saw_drop_by_vote && saw_drop_by_cap);                                 // (the scenario does run through a vote and through the cap)
        }
        // ---- Presence is whatever the state is, and always a message that the decoder takes: a random walk over every event ----
        {
            Lcg rng(44);
            for (const uint32_t start : {500000u, 0xFFFFF000u}) {
                Attendance::Config cfg;
                cfg.max_pause_ms = 300000;
                Attendance a(cfg);
                a.seat_humans(0b1111, start);
                uint32_t now = start;
                std::vector<bool> was_dropped(4, false);
                uint32_t last_pause = 0;
                size_t valid_votes = 0;
                size_t with_vote = 0;
                size_t with_missing = 0;
                size_t vote_drops = 0;
                size_t cap_drops = 0;
                for (int step = 0; step < 8000; ++step) {
                    now += rng.below(10) == 0 ? 40000u : rng.below(3000);
                    const uint8_t seat = static_cast<uint8_t>(rng.below(4));
                    switch (rng.below(10)) {
                        case 0: a.lost(seat, now); break;
                        case 1: a.returning(seat, now); break;
                        case 2: a.progress(seat, static_cast<uint8_t>(rng.below(120)), now); break;
                        case 3: a.caught_up(seat, now); break;
                        case 4: a.catch_up_failed(seat, now); break;
                        case 5: if (rng.below(8) == 0) a.dropped(seat, now); break;
                        case 6:
                        case 7: valid_votes += a.vote(seat, rng.below(2) == 0 ? a.vote_subject(now) : static_cast<uint8_t>(rng.below(4)), rng.below(3) != 0, now) ? 1u : 0u; break;
                        default: a.update(now); break;
                    }
                    for (uint8_t s = 0; s < 4; ++s) {
                        ASSERT_FALSE(was_dropped[s] && a.state(s) != S::Dropped);                                       // a seat that is dropped stays so ...
                        was_dropped[s] = a.state(s) == S::Dropped;
                    }
                    ASSERT_TRUE(a.pause_ms(now) >= last_pause);                                                          // ... and the pause never runs back
                    last_pause = a.pause_ms(now);
                    bool everybody_gone = true;
                    for (uint8_t s = 0; s < 4; ++s) everybody_gone = everybody_gone && a.state(s) == S::Dropped;
                    if (everybody_gone) {                                                                                // (a new match begins, with the same attendance)
                        vote_drops += a.drops_by_vote();
                        cap_drops += a.drops_by_cap();
                        a.seat_humans(0b1111, now);
                        was_dropped.assign(4, false);
                        last_pause = 0;
                    }
                    for (const uint8_t viewer : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{255}}) {
                        const PresenceMsg m = a.presence_for(viewer, now);
                        const std::vector<uint8_t> bytes = encode(m);
                        PresenceMsg back;
                        ASSERT_TRUE(decode(bytes, back));                                                                // a Presence of the real state is always one that the decoder takes ...
                        ASSERT_TRUE(encode(back) == bytes);
                        ASSERT_EQ(back.missing.empty(), !a.paused());                                                    // ... and it says what the state says
                        ASSERT_EQ(back.voters, a.connected_humans());
                        ASSERT_EQ(back.cap_s, a.cap_s(now));
                        ASSERT_EQ(back.vote_seat, a.vote_subject(now));
                        if (back.vote_seat != 255) {
                            ASSERT_EQ(back.votes_continue, a.votes_for_continue(back.vote_seat));
                            ASSERT_TRUE((a.state(back.vote_seat) == S::Absent && a.away_ms(back.vote_seat, now) >= cfg.vote_after_ms) || a.flapping(back.vote_seat, now));      // (away long enough, or a seat that flaps)
                            ASSERT_TRUE(viewer == 255 ? back.your_vote == 0 : (a.state(viewer) == S::Present || back.your_vote == 0));
                        }
                        for (const auto& e : back.missing) {
                            ASSERT_TRUE(a.state(e.seat) == S::Absent || a.state(e.seat) == S::CatchingUp);
                            ASSERT_EQ(e.waited_s, std::min<uint32_t>(a.away_ms(e.seat, now) / 1000u, 0xFFFFu));
                            ASSERT_EQ(e.progress, a.percent(e.seat));
                        }
                    }
                    const PresenceMsg m = a.presence_for(0, now);
                    with_vote += m.vote_seat != 255 ? 1u : 0u;
                    with_missing += m.missing.empty() ? 0u : 1u;
                }
                vote_drops += a.drops_by_vote();
                cap_drops += a.drops_by_cap();
                ASSERT_TRUE(valid_votes > 40 && with_vote > 1500 && with_missing > 3000 && vote_drops > 3 && cap_drops > 20);      // (the walk does reach votes, pauses and drops of both kinds)
            }
        }
    } TEST_END();

    TEST_CASE("N2.45 Lock-Step Runner: fast_forward Runs Queued Turns At Once Without Any Hook, In The Order Of update(): The Same State After Every Turn As The Paced Runner Over 600 Turns Of The Four-Player Match, In Slices Of Every Kind; Nothing Queued Is Nothing Done; Paced Play Goes On From There") {
        const uint32_t seed = 1;
        sim::SimulationEngine proto;
        const Ids ids = build_world(proto, seed);
        // 600 turns of the four-player match: the scripted orders of every player, and three turns that only the right ORDER can survive: an invitation and its acceptance in one turn
        // (the acceptance needs the invitation: canonical order is by issuer), a Hatch of every player, and the drop-out of seat 3
        std::vector<TurnMsg> turns(600);
        for (uint32_t k = 0; k < 600; ++k) {
            turns[k].turn = k;
            for (uint8_t p = 0; p < 4; ++p) {
                Command c;
                if (script(ids, seed, k * kTurnMs, p, c)) turns[k].commands.push_back(c);
            }
        }
        turns[100].commands.push_back(cmd(CommandType::AllianceAccept, 2, 1));
        turns[100].commands.push_back(cmd(CommandType::AllianceInvite, 1, 2));
        for (uint8_t p = 0; p < 4; ++p) turns[150].commands.push_back(cmd(CommandType::Hatch, p));
        turns[400].commands.push_back(cmd(CommandType::Drop, 3));
        for (TurnMsg& t : turns) sim::canonical_order(t.commands);
        size_t commands_in_all = 0;
        for (const TurnMsg& t : turns) commands_in_all += t.commands.size();
        ASSERT_TRUE(commands_in_all > 300);
        // the paced runner's state after every turn (one turn runs per update, a turn behind the one that was fed)
        std::vector<sim::StateHash> after(600);
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            size_t done = 0;
            for (uint32_t k = 0; k < 640; ++k) {
                if (k < 600) ASSERT_TRUE(runner.on_turn(turns[k]));
                for (const LockstepRunner::Executed& e : runner.update(kTurnMs)) {
                    ASSERT_EQ(e.turn, done);
                    after[done++] = sim.state_hash();
                }
            }
            ASSERT_EQ(done, size_t{600});
        }
        for (size_t k = 1; k < 600; ++k) ASSERT_TRUE(after[k] != after[k - 1]);       // (the match changes every turn: a tick that was skipped shows)
        {   // the scenario is order sensitive: turn 100 applied backwards gives another state (so the equality below proves the order, and a runner that applied it so would be caught)
            sim::SimulationEngine good;
            sim::SimulationEngine bad;
            build_world(good, seed);
            build_world(bad, seed);
            for (uint32_t k = 0; k < 100; ++k) {
                for (const Command& c : turns[k].commands) { good.apply_command(c); bad.apply_command(c); }
                good.tick();
                bad.tick();
            }
            ASSERT_TRUE(good.state_hash() == bad.state_hash());
            for (const Command& c : turns[100].commands) good.apply_command(c);
            for (auto it = turns[100].commands.rbegin(); it != turns[100].commands.rend(); ++it) bad.apply_command(*it);
            ASSERT_TRUE(good.state_hash() != bad.state_hash());
        }
        // ---- one turn at a time: the state after each is the paced runner's ----
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            size_t hooks = 0;
            runner.set_on_tick([&]() { ++hooks; });
            runner.set_on_command([&](const Command&, const sim::CommandResult&) { ++hooks; });
            runner.set_on_applied([&](const Command&) { ++hooks; });
            for (uint32_t k = 0; k < 600; ++k) {
                ASSERT_TRUE(runner.on_catch_up_turn(turns[k]));
                ASSERT_EQ(runner.queued(), size_t{1});
                ASSERT_EQ(runner.fast_forward(1), 1u);
                ASSERT_TRUE(sim.state_hash() == after[k]);                              // the same state after EVERY turn
                ASSERT_EQ(runner.next_turn_to_execute(), k + 1);
                ASSERT_EQ(runner.next_turn_expected(), k + 1);
                ASSERT_EQ(sim.current_tick(), uint64_t{k} + 1);
                ASSERT_TRUE(runner.at_boundary());
            }
            ASSERT_EQ(hooks, size_t{0});                                                // ... and no hook was called: nothing was drawn, no sound, no news
            ASSERT_EQ(runner.queued(), size_t{0});
        }
        // ---- slices of every kind: the state at each boundary of a slice is the paced runner's after that many turns ----
        for (const uint32_t pattern : {0u, 1u, 2u, 3u, 4u}) {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            size_t hooks = 0;
            runner.set_on_tick([&]() { ++hooks; });
            runner.set_on_command([&](const Command&, const sim::CommandResult&) { ++hooks; });
            runner.set_on_applied([&](const Command&) { ++hooks; });
            Lcg rng(100 + pattern);
            uint32_t fed = 0;
            uint32_t executed = 0;
            size_t calls = 0;
            while (executed < 600) {
                // turns come in bunches of the size of a batch (or of one), while the slices are of whatever size the frame had time for
                const uint32_t bunch = pattern == 0 ? 1u : (pattern == 1 ? 600u : 1u + rng.below(300));
                for (uint32_t i = 0; i < bunch && fed < 600; ++i) ASSERT_TRUE(runner.on_catch_up_turn(turns[fed++]));
                while (runner.queued() > 0) {
                    const uint32_t slice = pattern == 3 ? 7u : (pattern == 4 ? 1u + rng.below(100) : (pattern == 0 ? 1u : 64u));
                    const uint32_t ran = runner.fast_forward(slice);
                    ASSERT_TRUE(ran >= 1 && ran <= slice);
                    executed += ran;
                    ++calls;
                    ASSERT_EQ(runner.next_turn_to_execute(), executed);
                    ASSERT_TRUE(sim.state_hash() == after[executed - 1]);
                }
            }
            ASSERT_EQ(executed, 600u);
            ASSERT_EQ(hooks, size_t{0});
            ASSERT_TRUE(calls >= 10);
            ASSERT_EQ(sim.current_tick(), uint64_t{600});
        }
        // ---- max_ticks is respected, 0 does nothing, an empty queue is nothing done (and the runner is left as it was) ----
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            ASSERT_EQ(runner.fast_forward(100), 0u);                                     // nothing queued
            ASSERT_EQ(runner.next_turn_to_execute(), 0u);
            ASSERT_EQ(sim.current_tick(), uint64_t{0});
            for (uint32_t k = 0; k < 10; ++k) ASSERT_TRUE(runner.on_catch_up_turn(turns[k]));
            ASSERT_EQ(runner.fast_forward(0), 0u);                                       // asked for no ticks
            ASSERT_TRUE(runner.queued() == 10 && sim.current_tick() == 0);
            ASSERT_EQ(runner.fast_forward(5), 5u);
            ASSERT_TRUE(runner.queued() == 5 && sim.current_tick() == 5 && runner.next_turn_to_execute() == 5);
            ASSERT_TRUE(sim.state_hash() == after[4]);
            ASSERT_EQ(runner.fast_forward(1000), 5u);                                    // what is there, no more
            ASSERT_TRUE(runner.queued() == 0 && sim.current_tick() == 10 && sim.state_hash() == after[9]);
            ASSERT_EQ(runner.fast_forward(1000), 0u);
            ASSERT_EQ(sim.current_tick(), uint64_t{10});
            // a runner that is in the middle of paced play and has nothing queued is not touched by a fast-forward of nothing: it is still started, with the buffer that it had
            sim::SimulationEngine live;
            build_world(live, seed);
            LockstepRunner paced(live);
            for (uint32_t k = 0; k < 30; ++k) {
                ASSERT_TRUE(paced.on_turn(turns[k]));
                paced.update(kTurnMs);
            }
            const uint32_t buffer = paced.buffer_turns();
            const uint32_t expected = paced.next_turn_expected();
            const uint32_t executed_before = paced.next_turn_to_execute();
            ASSERT_TRUE(paced.queued() >= 1);
            const size_t queued = paced.queued();
            ASSERT_EQ(paced.fast_forward(0), 0u);
            ASSERT_TRUE(paced.queued() == queued && paced.buffer_turns() == buffer && paced.next_turn_expected() == expected && paced.next_turn_to_execute() == executed_before);
            paced.update(kTurnMs);                                                       // the last turn that was fed runs: the queue is empty, the runner is in paced play
            ASSERT_EQ(paced.queued(), size_t{0});
            const bool rebuilding = paced.rebuilding();
            const uint32_t stalled_for = paced.stalled_ms();
            const uint32_t at_turn = paced.next_turn_to_execute();
            ASSERT_EQ(paced.fast_forward(10), 0u);                                       // nothing queued is nothing done: not even the pacing is touched
            ASSERT_TRUE(paced.buffer_turns() == buffer && paced.rebuilding() == rebuilding && paced.stalled_ms() == stalled_for && paced.next_turn_to_execute() == at_turn);
            ASSERT_EQ(live.current_tick(), uint64_t{at_turn});
        }
        // ---- the numbering and the order of arrival: catch-up turns and live turns are one sequence, with the same refusals ----
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            ASSERT_FALSE(runner.on_catch_up_turn(turns[1]));                              // must start at 0
            ASSERT_TRUE(runner.on_catch_up_turn(turns[0]));
            ASSERT_FALSE(runner.on_catch_up_turn(turns[0]));                              // no repeat
            ASSERT_FALSE(runner.on_catch_up_turn(turns[2]));                              // no gap
            ASSERT_TRUE(runner.on_turn(turns[1]));                                        // a live turn follows a catch-up turn ...
            ASSERT_TRUE(runner.on_catch_up_turn(turns[2]));                               // ... and the other way round
            ASSERT_FALSE(runner.on_turn(turns[2]));
            TurnMsg many;
            many.turn = 3;
            for (size_t i = 0; i < kMaxTurnCommands + 1; ++i) many.commands.push_back(cmd(CommandType::Hatch, 0));
            ASSERT_FALSE(runner.on_catch_up_turn(many));                                  // more commands than a turn may hold
            ASSERT_FALSE(runner.on_turn(many));
            ASSERT_EQ(runner.next_turn_expected(), 3u);
            ASSERT_TRUE(runner.logged_turn(0) != nullptr && runner.logged_turn(2) != nullptr && runner.logged_turn(2)->commands == turns[2].commands);      // (a machine that caught up can serve what it was given)
            ASSERT_EQ(runner.fast_forward(100), 3u);
            ASSERT_TRUE(sim.state_hash() == after[2]);
        }
        // ---- paced play goes on from a fast-forward: the next live turns are collected (target + 1 of them), then run in order, with every hook, to the paced runner's final state ----
        {
            sim::SimulationEngine sim;
            build_world(sim, seed);
            LockstepRunner runner(sim);
            size_t ticks = 0;
            size_t applied = 0;
            size_t verdicts = 0;
            bool boundary_inside_a_hook = false;
            runner.set_on_tick([&]() { ++ticks; boundary_inside_a_hook = boundary_inside_a_hook || runner.at_boundary(); });
            runner.set_on_command([&](const Command&, const sim::CommandResult&) { ++verdicts; boundary_inside_a_hook = boundary_inside_a_hook || runner.at_boundary(); });
            runner.set_on_applied([&](const Command&) { ++applied; boundary_inside_a_hook = boundary_inside_a_hook || runner.at_boundary(); });
            for (uint32_t k = 0; k < 300; ++k) ASSERT_TRUE(runner.on_catch_up_turn(turns[k]));
            ASSERT_EQ(runner.fast_forward(300), 300u);
            ASSERT_TRUE(ticks == 0 && applied == 0 && verdicts == 0);
            ASSERT_TRUE(sim.state_hash() == after[299]);
            ASSERT_FALSE(runner.stalled());                                               // (no "waiting for the other players": the runner is not waiting, it has not started)
            ASSERT_FALSE(runner.rebuilding());
            ASSERT_EQ(runner.buffer_turns(), 1u);
            ASSERT_TRUE(runner.update(50).empty());                                       // nothing is queued: nothing runs, and nothing is a stall
            ASSERT_FALSE(runner.stalled());
            ASSERT_TRUE(runner.on_turn(turns[300]));
            ASSERT_TRUE(runner.update(50).empty());                                       // one live turn is not enough: the buffer (target 1 + the turn that runs) is collected first
            ASSERT_TRUE(runner.on_turn(turns[301]));
            std::vector<LockstepRunner::Executed> ran = runner.update(0);
            ASSERT_TRUE(ran.size() == 1 && ran[0].turn == 300);                           // ... and the first live turn runs at once, numbered as it is
            ASSERT_TRUE(sim.state_hash() == after[300]);
            size_t commands_applied = 0;
            for (uint32_t k = 301; k < 600; ++k) {
                if (k + 1 < 600) ASSERT_TRUE(runner.on_turn(turns[k + 1]));
                ran = runner.update(kTurnMs);
                ASSERT_TRUE(ran.size() == 1 && ran[0].turn == k);
                ASSERT_TRUE(sim.state_hash() == after[k]);
            }
            ran = runner.update(kTurnMs);
            ASSERT_TRUE(ran.empty() && runner.next_turn_to_execute() == 600);
            for (uint32_t k = 300; k < 600; ++k) commands_applied += turns[k].commands.size();
            ASSERT_EQ(ticks, size_t{300});                                                // the hooks are called for the live turns, one tick each ...
            ASSERT_TRUE(applied == commands_applied && verdicts == commands_applied);     // ... and for every command of them
            ASSERT_FALSE(boundary_inside_a_hook);                                         // (inside a hook a turn is half done: not at a boundary)
            ASSERT_TRUE(runner.at_boundary());
            ASSERT_TRUE(sim.state_hash() == after[599]);
        }
    } TEST_END();

    TEST_CASE("N2.46 Lock-Step Runner: The Catch-Up Tells The Jitter Buffer Nothing, And Leaves The Runner As At The Start Of A Match (The Buffer At Its Steady Value, No Turn Read, The Next Live Turns Collected First): A Link That Was Rough Before The Pause Does Not Make The Buffer Grow After It") {
        // a turn's commands are not what this is about: empty turns, and a world that nothing happens in
        auto empty_turn = [](uint32_t n) {
            TurnMsg t;
            t.turn = n;
            return t;
        };
        // drives live turns into a runner: turn k (k0 .. k0 + count - 1) is sealed at base + (k - k0) * 50 ms and arrives `delay(k)` later (never before an earlier turn: the link is ordered); every
        // 10 ms the runner has its frame. `buffer_max` is the largest buffer that was asked for.
        struct Drive {
            uint32_t buffer_max{0};
            uint32_t buffer_at_end{0};
            size_t executed{0};
        };
        auto drive = [&](LockstepRunner& runner, uint32_t k0, uint32_t count, const std::function<uint32_t(uint32_t)>& delay) {
            Drive out;
            std::vector<uint32_t> arrival(count);
            uint32_t last = 0;
            for (uint32_t i = 0; i < count; ++i) {
                last = std::max(last, i * kTurnMs + delay(i));
                arrival[i] = last;
            }
            uint32_t next = 0;
            for (uint32_t now = 10; now <= (count + 30) * kTurnMs; now += 10) {
                while (next < count && arrival[next] <= now) {
                    runner.on_turn(empty_turn(k0 + next));
                    ++next;
                }
                out.executed += runner.update(10).size();
                out.buffer_max = std::max(out.buffer_max, runner.buffer_turns());
            }
            out.buffer_at_end = runner.buffer_turns();
            return out;
        };
        // ---- the catch-up's turns are not arrivals: even when the runner is driven in between, the jitter buffer reads nothing from them ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            ASSERT_TRUE(runner.update(10).empty());                                       // (the first update of a runner reads what waited for it and counts none of it: so one frame goes by first)
            for (uint32_t k = 0; k < 1000; ++k) ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(k)));
            runner.update(50);                                                            // a frame that finds a thousand turns that came all at once: for live turns that is a bunch of 1000 and wants the largest buffer
            ASSERT_EQ(runner.jitter().lateness_ms(), 0u);
            ASSERT_EQ(runner.buffer_turns(), 1u);
            ASSERT_EQ(runner.jitter().wanted(), 1u);
            // the same turns as live ones: the buffer reads them (the control: this is what the catch-up must not do)
            sim::SimulationEngine other;
            build_world(other, 1);
            LockstepRunner live(other);
            ASSERT_TRUE(live.update(10).empty());
            for (uint32_t k = 0; k < 1000; ++k) ASSERT_TRUE(live.on_turn(empty_turn(k)));
            live.update(50);
            ASSERT_TRUE(live.jitter().lateness_ms() > 1000 && live.buffer_turns() == 4);
        }
        // ---- a long catch-up (36,000 turns, in the batches that the server sends) and then the live match: nothing was learned, nothing is wrong ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1, 2 * 3600 * 1000);                                         // (a match of two hours: the engine ends a match of 12 minutes, which has 14,400 ticks)
            LockstepRunner runner(sim);
            uint32_t next = 0;
            while (next < 36000) {
                for (uint32_t i = 0; i < 4096 && next < 36000; ++i) ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(next++)));
                while (runner.queued() > 0) runner.fast_forward(500);
            }
            ASSERT_EQ(runner.next_turn_to_execute(), 36000u);
            ASSERT_EQ(sim.current_tick(), uint64_t{36000});
            ASSERT_EQ(runner.jitter().lateness_ms(), 0u);
            ASSERT_EQ(runner.buffer_turns(), 1u);
            ASSERT_FALSE(runner.stalled());
            const Drive live = drive(runner, 36000, 400, [](uint32_t) { return 0u; });      // the live match goes on over a steady link
            ASSERT_EQ(live.buffer_max, 1u);                                               // the buffer never asked for more than one turn
            ASSERT_EQ(live.buffer_at_end, 1u);
            ASSERT_EQ(live.executed, size_t{400});                                        // every turn ran (the last one when its tick was due)
            ASSERT_EQ(runner.next_turn_to_execute(), 36000u + 400u);
        }
        // ---- a link that was rough before the pause: the buffer had grown; the pause, the catch-up and a steady link afterwards leave it at its steady value ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            Lcg rng(46);
            const Drive rough = drive(runner, 0, 600, [&](uint32_t) { return rng.below(130); });   // 0 - 130 ms of jitter: a buffer of three or four turns
            ASSERT_TRUE(rough.buffer_max >= 3);
            const uint32_t learned = runner.buffer_turns();
            ASSERT_TRUE(learned >= 2);
            const uint32_t executed_before = runner.next_turn_to_execute();
            for (int i = 0; i < 100; ++i) runner.update(50);                              // the pause: 5 s with no turn at all (the connection was lost and the match waits)
            ASSERT_TRUE(runner.stalled());
            const uint32_t next = runner.next_turn_expected();
            ASSERT_TRUE(runner.on_turn(empty_turn(next)));                                // (a live turn that came in the frame that the catch-up began in: nobody has read it yet)
            for (uint32_t k = next + 1; k < next + 300; ++k) ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(k)));      // the turns that this machine missed come as a catch-up
            ASSERT_EQ(runner.fast_forward(1000), 300u);
            ASSERT_EQ(runner.next_turn_to_execute(), next + 300);
            ASSERT_TRUE(runner.next_turn_to_execute() > executed_before);
            ASSERT_EQ(runner.buffer_turns(), 1u);                                         // the buffer starts again from its steady value ...
            ASSERT_EQ(runner.jitter().lateness_ms(), 0u);                                 // ... with nothing read: what was read describes a link and a clock that are gone
            ASSERT_FALSE(runner.stalled());
            ASSERT_FALSE(runner.rebuilding());
            const Drive steady = drive(runner, next + 300, 400, [](uint32_t) { return 0u; });      // the match goes on, over a link that is steady now
            ASSERT_EQ(steady.buffer_max, 1u);                                             // the stale measurements did not come back, the clock that jumped did not look like a late link
            ASSERT_EQ(steady.buffer_at_end, 1u);
            ASSERT_EQ(steady.executed, size_t{400});
            // and a link that IS rough after the catch-up is learned afresh: it grows as it would at the start of a match
            const Drive rough_again = drive(runner, next + 300 + 400, 600, [&](uint32_t) { return rng.below(130); });
            ASSERT_TRUE(rough_again.buffer_max >= 3);
        }
        // ---- live turns that waited behind the catch-up (a window that was not drawn for a second after it) come in a bunch: that is not the link's lateness either ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            for (uint32_t k = 0; k < 300; ++k) ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(k)));
            ASSERT_EQ(runner.fast_forward(1000), 300u);
            for (uint32_t k = 300; k < 340; ++k) ASSERT_TRUE(runner.on_turn(empty_turn(k)));      // two seconds of the live match, all there when the first frame comes
            runner.update(10);
            ASSERT_EQ(runner.jitter().lateness_ms(), 0u);                                 // read at once, but they were not late: they waited for the runner
            ASSERT_EQ(runner.buffer_turns(), 1u);
            for (int i = 0; i < 400 && runner.queued() > 0; ++i) runner.update(10);        // and the queue is run down at up to four times normal speed, as for any bunch
            ASSERT_EQ(runner.queued(), size_t{0});
            ASSERT_EQ(runner.next_turn_to_execute(), 340u);
            ASSERT_EQ(runner.buffer_turns(), 1u);
        }
        // ---- a runner that had waited a moment for a turn (80 ms: a stall that one more turn of buffer would have bridged) when a small catch-up came: the wait is over, so the live turns that come
        // after it do not grow the buffer as a late turn would ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            for (uint32_t k = 0; k < 60; ++k) {
                ASSERT_TRUE(runner.on_turn(empty_turn(k)));
                runner.update(kTurnMs);
            }
            runner.update(kTurnMs);                                                       // the last turn that was queued runs
            ASSERT_EQ(runner.queued(), size_t{0});
            for (int i = 0; i < 8; ++i) runner.update(10);                                // 80 ms with no turn although one is due
            ASSERT_TRUE(runner.stalled() && runner.stalled_ms() <= 100);
            const uint32_t next = runner.next_turn_expected();
            ASSERT_TRUE(runner.on_catch_up_turn(empty_turn(next)) && runner.on_catch_up_turn(empty_turn(next + 1)));
            ASSERT_EQ(runner.fast_forward(10), 2u);
            ASSERT_FALSE(runner.stalled());                                               // not waiting for anything now
            const Drive live = drive(runner, next + 2, 200, [](uint32_t) { return 0u; });
            ASSERT_EQ(live.buffer_max, 1u);                                               // (the end of the stall was never counted as a late turn)
            ASSERT_EQ(live.executed, size_t{200});
        }
        // ---- a fast-forward that runs nothing does not reset what the runner has learned ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            Lcg rng(47);
            drive(runner, 0, 600, [&](uint32_t) { return rng.below(130); });
            while (runner.queued() > 0) runner.update(50);                               // (the turns of the run that wait in the buffer run: nothing is queued now)
            const uint32_t learned = runner.buffer_turns();
            const uint32_t lateness = runner.jitter().lateness_ms();
            ASSERT_TRUE(learned >= 2 && lateness > 0);
            ASSERT_EQ(runner.fast_forward(100), 0u);                                      // nothing queued: nothing done, and what the runner has learned stays learned
            ASSERT_EQ(runner.fast_forward(100), 0u);
            ASSERT_TRUE(runner.buffer_turns() == learned && runner.jitter().lateness_ms() == lateness);
        }
    } TEST_END();
    // ---- the fixes of the review of release A: the budgets of a key holder, flapping, the resume countdown (N2.75 - N2.79) -------------------------------------------------------

    TEST_CASE("N2.75 Attendance: The Catch-Up Budget Of An Absence: A Hello Of An Attempt That Is In Progress Does Not Start Its Stall Clock, Percent Or Time Again; The Time Of All The Attempts Of One Absence Is Added Up; Used Up, The Catch-Up Fails At That Moment (The Seat Is Absent: The Vote And The Cap Apply) And A Hello Is Refused; A New Absence Has It All Again") {
        using S = Attendance::State;
        const uint32_t t0 = 400000;
        Attendance::Config cfg;
        cfg.max_catch_up_ms = 60000;
        cfg.max_pause_ms = 600000;
        cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;               // (this test is about time, not about the number of Hellos: N2.76)
        ASSERT_TRUE(Attendance::Config{}.max_catch_up_ms == 300000 && kMaxCatchUpMs == 300000);          // the default: five minutes
        {   // a Hello of the attempt that goes on changes nothing of it
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000));
            ASSERT_TRUE(a.progress(1, 10, t0 + 5000));
            ASSERT_TRUE(a.returning(1, t0 + 12000));                         // 12 s into the attempt it says Hello again
            ASSERT_EQ(a.state(1), S::CatchingUp);
            ASSERT_EQ(a.percent(1), 10);                                      // (it used to start at 0 again)
            ASSERT_EQ(a.catching_up_ms(1, t0 + 12000), 11000u);               // the attempt began at 1 s: its time goes on
            ASSERT_TRUE(a.returning(1, t0 + 24000));                          // and again: the stall clock is that of the progress at 5 s, whatever Hellos come
            ASSERT_TRUE(a.update(t0 + 24999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_TRUE(a.update(t0 + 25000).empty());
            ASSERT_EQ(a.state(1), S::Absent);                                 // 20 s after the last progress
            ASSERT_EQ(a.catching_up_ms(1, t0 + 25000), 24000u);               // the attempt took 24 s of the 60
        }
        {   // the attempts of one absence add up, and a seat that makes progress is let go at the budget all the same
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 100000), 0u);                  // an absent seat is not catching up
            ASSERT_TRUE(a.returning(1, t0 + 10000));                          // attempt 1: it never progresses, the stall ends it after 20 s
            ASSERT_TRUE(a.update(t0 + 30000).empty() && a.state(1) == S::Absent);
            ASSERT_EQ(a.catching_up_ms(1, t0 + 31000), 20000u);
            ASSERT_TRUE(a.returning(1, t0 + 40000));                          // attempt 2: it progresses every 10 s, so the stall never ends it
            for (uint32_t k = 1; k <= 3; ++k) ASSERT_TRUE(a.progress(1, static_cast<uint8_t>(k * 10), t0 + 40000 + k * 10000));     // 50, 60 and 70 s
            ASSERT_EQ(a.catching_up_ms(1, t0 + 79999), 20000u + 39999u);
            ASSERT_TRUE(a.update(t0 + 79999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_EQ(a.rejoin_check(1, t0 + 79999), Attendance::Rejoin::Allowed);          // 1 ms is left
            ASSERT_EQ(a.rejoin_check(1, t0 + 80000), Attendance::Rejoin::CatchUpSpent);     // 60 s: none
            ASSERT_TRUE(a.update(t0 + 80000).empty());                        // let go at the budget, 10 s after its last progress
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.catch_up_expired(), 1u);
            ASSERT_EQ(a.percent(1), 0);
            ASSERT_EQ(a.rejoins_refused(), 0u);
            ASSERT_FALSE(a.returning(1, t0 + 81000));                         // a Hello is refused, nothing changes, it is counted
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.rejoins_refused(), 1u);
            ASSERT_EQ(a.catching_up_ms(1, t0 + 500000), 60000u);              // (it stays what it was: an absent seat does not catch up)
            ASSERT_FALSE(a.returning(1, t0 + 500000));
            ASSERT_EQ(a.catch_up_expired(), 1u);                              // (and a refusal is not an expiry)
            ASSERT_EQ(a.vote_subject(t0 + 81000), 1);                         // the vote opens: it has been away 81 s, and nothing keeps it catching up
            ASSERT_TRUE(a.vote(0, 1, true, t0 + 81000) && a.vote(2, 1, true, t0 + 81000));
            ASSERT_EQ(a.update(t0 + 81000).size(), 1u);
            ASSERT_EQ(a.drops_by_vote(), 1u);
        }
        {   // the cap applies to a seat whose budget is spent (no vote is cast: the cap, 120 s, takes it)
            Attendance::Config cap = cfg;
            cap.max_pause_ms = 120000;
            Attendance a(cap);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000));
            for (uint32_t k = 1; k <= 5; ++k) ASSERT_TRUE(a.progress(1, static_cast<uint8_t>(k), t0 + 1000 + k * 10000));
            ASSERT_TRUE(a.update(t0 + 60999).empty() && a.state(1) == S::CatchingUp);
            ASSERT_TRUE(a.update(t0 + 61000).empty() && a.state(1) == S::Absent);
            ASSERT_FALSE(a.returning(1, t0 + 62000));
            ASSERT_TRUE(a.update(t0 + 119999).empty());
            const std::vector<uint8_t> dropped = a.update(t0 + 120000);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1 && a.drops_by_cap() == 1u);
        }
        {   // the budget is of one absence: catching up ends it (the next absence has all of it), and so does a seat that was Present (a second window)
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000));
            ASSERT_TRUE(a.progress(1, 50, t0 + 20000) && a.progress(1, 60, t0 + 40000));
            ASSERT_TRUE(a.update(t0 + 50000).empty() && a.caught_up(1, t0 + 50000));      // 49 s of the 60 were spent
            ASSERT_EQ(a.catching_up_ms(1, t0 + 50000), 0u);
            ASSERT_TRUE(a.lost(1, t0 + 100000) && a.returning(1, t0 + 101000));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 130000), 29000u);
            ASSERT_TRUE(a.progress(1, 5, t0 + 110000) && a.progress(1, 6, t0 + 120000) && a.progress(1, 7, t0 + 130000));
            ASSERT_TRUE(a.update(t0 + 130000).empty() && a.state(1) == S::CatchingUp);    // (49 s would have been 20 s over)
            ASSERT_TRUE(a.caught_up(1, t0 + 131000));
            // a window that takes over a seat whose old link was alive starts an absence with all of the budget
            ASSERT_TRUE(a.returning(1, t0 + 200000));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 210000), 10000u);
        }
        {   // an attempt that failed and one that got through: the time of the first is given back with the seat (caught up), and what the next absence may spend is all of it again
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000));
            ASSERT_TRUE(a.update(t0 + 21000).empty() && a.state(1) == S::Absent);        // 20 s without progress
            ASSERT_EQ(a.catching_up_ms(1, t0 + 25000), 20000u);
            ASSERT_TRUE(a.returning(1, t0 + 30000) && a.progress(1, 90, t0 + 35000) && a.caught_up(1, t0 + 36000));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 36000), 0u);                               // back: nothing of the absence is held against the seat
            ASSERT_TRUE(a.lost(1, t0 + 100000) && a.returning(1, t0 + 101000));
            ASSERT_EQ(a.catching_up_ms(1, t0 + 130000), 29000u);                          // (the next absence counts from 0, not from the 26 s of the last)
            ASSERT_EQ(a.rejoin_check(1, t0 + 130000), Attendance::Rejoin::Allowed);
        }
    } TEST_END();

    TEST_CASE("N2.76 Attendance: The Rejoin Budget Of A Seat: At Most Twelve Hellos Are Accepted In A Minute (The Window Slides To The Millisecond), And At Most Three Times The Log's Size Is Streamed In Ten Minutes, At Least 1 MiB; A Refused Hello Changes Nothing, Counts Nothing Against The Seat And Is Counted; The Streams Are Charged As They Are Sent") {
        using S = Attendance::State;
        const uint32_t t0 = 900000;
        {   // twelve Hellos a minute
            Attendance a;
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(Attendance::Config{}.rejoin_attempts == 12 && Attendance::Config{}.rejoin_window_ms == 60000 && kRejoinAttempts == 12 && kRejoinWindowMs == 60000);
            ASSERT_TRUE(a.lost(1, t0));
            for (uint32_t k = 0; k < 12; ++k) ASSERT_TRUE(a.returning(1, t0 + 1000 + k * 4000));        // 1 .. 12, the last at 45 s
            ASSERT_EQ(a.rejoin_check(1, t0 + 49000), Attendance::Rejoin::TooManyAttempts);
            const uint8_t percent = a.percent(1);
            ASSERT_TRUE(a.progress(1, 7, t0 + 49000));
            ASSERT_FALSE(a.returning(1, t0 + 49000));                                     // the thirteenth is refused: nothing changes
            ASSERT_TRUE(a.state(1) == S::CatchingUp && a.percent(1) == 7 && percent == 0);
            ASSERT_EQ(a.rejoins_refused(), 1u);
            ASSERT_FALSE(a.returning(1, t0 + 55000));                                     // (and a refused Hello does not count: the window is that of the twelve that were accepted)
            ASSERT_FALSE(a.returning(1, t0 + 60999));                                     // the first one is 59,999 ms old: it still counts
            ASSERT_EQ(a.rejoins_refused(), 3u);
            ASSERT_TRUE(a.returning(1, t0 + 61000));                                      // 60,000 ms: it is out of the minute
            ASSERT_EQ(a.rejoin_check(1, t0 + 61000), Attendance::Rejoin::TooManyAttempts);
            ASSERT_EQ(a.rejoin_check(1, t0 + 65000), Attendance::Rejoin::Allowed);        // (the second leaves at 65 s)
            ASSERT_EQ(a.rejoins_refused(), 3u);
            // a seat that is dropped, or no seat, is not "refused": nothing is held for it
            ASSERT_TRUE(a.dropped(1, t0 + 62000));
            ASSERT_FALSE(a.returning(1, t0 + 300000));
            ASSERT_EQ(a.rejoin_check(1, t0 + 300000), Attendance::Rejoin::NotHeld);
            ASSERT_EQ(a.rejoin_check(3, t0), Attendance::Rejoin::NotHeld);               // (seat 3 is no seat of this match)
            ASSERT_EQ(a.rejoins_refused(), 3u);
            // each seat has its own budget
            ASSERT_TRUE(a.lost(0, t0 + 65000) && a.returning(0, t0 + 65100));
        }
        {   // the configured number: one a minute, sixteen a minute (the most), and a request for more or for none is clamped
            Attendance::Config one;
            one.rejoin_attempts = 1;
            Attendance a(one);
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 100));
            ASSERT_FALSE(a.returning(1, t0 + 200));
            ASSERT_TRUE(a.returning(1, t0 + 60100));
            Attendance::Config lots;
            lots.rejoin_attempts = 1000;
            ASSERT_EQ(Attendance(lots).config().rejoin_attempts, Attendance::kMaxRejoinAttempts);
            lots.rejoin_attempts = 0;
            ASSERT_EQ(Attendance(lots).config().rejoin_attempts, 1u);
            Attendance::Config flap;
            flap.flap_losses = 1;
            ASSERT_EQ(Attendance(flap).config().flap_losses, 2u);
            flap.flap_losses = 99;
            ASSERT_EQ(Attendance(flap).config().flap_losses, Attendance::kMaxFlapLosses);
        }
        {   // bytes: three times the log's size in ten minutes (here with no floor)
            Attendance::Config cfg;
            cfg.stream_floor_bytes = 0;
            cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            cfg.max_pause_ms = 3600000;
            cfg.max_catch_up_ms = 3600000;
            Attendance a(cfg);
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0));
            ASSERT_EQ(a.streamed_bytes(), uint64_t{0});
            const size_t log = 1000;
            const size_t planned = 900;                                                    // (the packed turns are less than the log: the index is not streamed)
            ASSERT_TRUE(a.returning(1, t0 + 1000, planned, log));
            a.charge_stream(1, t0 + 1100, 500);                                            // the stream is charged as it is sent
            a.charge_stream(1, t0 + 1200, 400);
            ASSERT_EQ(a.streamed_bytes(), uint64_t{900});
            ASSERT_TRUE(a.catch_up_failed(1, t0 + 2000) && a.returning(1, t0 + 100000, planned, log));        // the second: 900 + 900
            a.charge_stream(1, t0 + 100100, 900);
            ASSERT_TRUE(a.catch_up_failed(1, t0 + 101000) && a.returning(1, t0 + 200000, planned, log));      // the third: 1800 + 900 = 2700 of 3000
            a.charge_stream(1, t0 + 200100, 900);
            ASSERT_TRUE(a.catch_up_failed(1, t0 + 201000));
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, planned, log), Attendance::Rejoin::TooMuchStreamed);     // the fourth: 2700 + 900 = 3600 of 3000
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, 300, log), Attendance::Rejoin::Allowed);                 // a smaller one still fits: 3000 of 3000 ...
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, 301, log), Attendance::Rejoin::TooMuchStreamed);         // ... and not a byte more
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000), Attendance::Rejoin::Allowed);                           // (no size given: the bytes are not asked)
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, planned, 0), Attendance::Rejoin::Allowed);
            ASSERT_EQ(a.rejoin_check(1, t0 + 250000, 0, log), Attendance::Rejoin::Allowed);
            ASSERT_FALSE(a.returning(1, t0 + 250000, planned, log));
            ASSERT_EQ(a.rejoins_refused(), 1u);
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.streamed_bytes(), uint64_t{2700});                                                    // (a refusal streams nothing)
            // the window of ten minutes slides: the first stream (at 1.1 - 1.2 s) is out of it at 601.1 s
            ASSERT_EQ(a.rejoin_check(1, t0 + 601099, planned, log), Attendance::Rejoin::TooMuchStreamed);
            ASSERT_EQ(a.rejoin_check(1, t0 + 601100, planned, log), Attendance::Rejoin::Allowed);             // 1800 + 900
            ASSERT_EQ(a.rejoin_check(1, t0 + 700099, planned, log), Attendance::Rejoin::Allowed);
            ASSERT_EQ(a.rejoin_check(1, t0 + 700100, planned, log), Attendance::Rejoin::Allowed);
            ASSERT_TRUE(a.returning(1, t0 + 700100, planned, log));
            ASSERT_EQ(a.streamed_bytes(), uint64_t{2700});
        }
        {   // the floor: a log of a few KB is no reason to refuse a fifth reload; the budget is the larger of three logs and 1 MiB
            Attendance::Config cfg;
            cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;
            cfg.max_catch_up_ms = 3600000;
            cfg.max_pause_ms = 3600000;
            ASSERT_TRUE(cfg.stream_factor == 3 && cfg.stream_floor_bytes == (size_t{1} << 20) && cfg.stream_window_ms == 600000 && kStreamFactor == 3 && kStreamWindowMs == 600000);
            Attendance a(cfg);
            a.seat_humans(0b0011, t0);
            ASSERT_TRUE(a.lost(1, t0));
            uint32_t at = t0 + 1000;
            for (int i = 0; i < 10; ++i) {                                                 // ten streams of 100 KB of a log of 100 KB: 1 MB of the 1 MiB
                ASSERT_TRUE(a.returning(1, at, 100000, 100000));
                a.charge_stream(1, at + 10, 100000);
                ASSERT_TRUE(a.catch_up_failed(1, at + 20));
                at += 8000;                                                                // (sixteen Hellos a minute are the most a Config may allow: this spacing is within it)
            }
            ASSERT_EQ(a.rejoin_check(1, at, 100000, 100000), Attendance::Rejoin::TooMuchStreamed);   // the eleventh: 1.1 MB of 1,048,576
            ASSERT_EQ(a.rejoin_check(1, at, 48576, 100000), Attendance::Rejoin::Allowed);
            ASSERT_EQ(a.rejoin_check(1, at, 48577, 100000), Attendance::Rejoin::TooMuchStreamed);
            // a log of 2 MB: three times that
            Attendance b(cfg);
            b.seat_humans(0b0011, t0);
            ASSERT_TRUE(b.lost(1, t0));
            const size_t big = 2u << 20;
            for (int i = 0; i < 3; ++i) {
                ASSERT_TRUE(b.returning(1, t0 + 1000 + 20000u * static_cast<uint32_t>(i), big - 100, big));
                b.charge_stream(1, t0 + 1100 + 20000u * static_cast<uint32_t>(i), big - 100);
                ASSERT_TRUE(b.catch_up_failed(1, t0 + 1200 + 20000u * static_cast<uint32_t>(i)));
            }
            ASSERT_EQ(b.rejoin_check(1, t0 + 61000, big - 100, big), Attendance::Rejoin::TooMuchStreamed);
        }
        {   // the same sequence from other points of the 32-bit clock gives the same answers (the windows are differences)
            for (const uint32_t start : {0u, 0xFFFFFF00u, 0xFFFF0000u, 0x80000000u - 500u}) {
                Attendance::Config cfg;
                cfg.stream_floor_bytes = 0;
                cfg.max_catch_up_ms = 3600000;                                               // (the attempt is left open for ten minutes: not the subject here)
                cfg.max_pause_ms = 3600000;
                Attendance a(cfg);
                a.seat_humans(0b0011, start);
                ASSERT_TRUE(a.lost(1, start));
                ASSERT_TRUE(a.returning(1, start + 1000, 900, 1000));
                a.charge_stream(1, start + 1100, 2800);
                for (uint32_t k = 2; k <= 12; ++k) ASSERT_TRUE(a.returning(1, start + k * 1000));          // the twelve of the minute
                ASSERT_EQ(a.rejoin_check(1, start + 13000), Attendance::Rejoin::TooManyAttempts);
                ASSERT_EQ(a.rejoin_check(1, start + 61000 - 1), Attendance::Rejoin::TooManyAttempts);
                ASSERT_EQ(a.rejoin_check(1, start + 61000), Attendance::Rejoin::Allowed);
                ASSERT_EQ(a.rejoin_check(1, start + 61000, 900, 1000), Attendance::Rejoin::TooMuchStreamed);     // 2800 + 900 > 3000
                ASSERT_EQ(a.rejoin_check(1, start + 601100 - 1, 900, 1000), Attendance::Rejoin::TooMuchStreamed);
                ASSERT_EQ(a.rejoin_check(1, start + 601100, 900, 1000), Attendance::Rejoin::Allowed);
            }
        }
    } TEST_END();

    TEST_CASE("N2.77 Attendance: A Seat That Flaps: Lost Three Times Within A Minute (A Second Window That Takes The Seat Over Counts As A Loss) It Is The Subject Of The Vote At Once, In Any State (Absent, Catching Up, Or Back); Its Votes Stay Across Its Returns And Win Or Close; It Does Not Vote On Itself; Every Pause Counts At Least 5 s Toward The Cap, So That Flapping Reaches It") {
        using S = Attendance::State;
        const uint32_t t0 = 3000000;
        Attendance::Config cfg;
        cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;
        cfg.max_pause_ms = 3600000;
        ASSERT_TRUE(Attendance::Config{}.flap_losses == 3 && Attendance::Config{}.flap_window_ms == 60000 && kFlapLosses == 3 && kFlapWindowMs == 60000);
        // ---- three losses in a minute; two are none; three in more than a minute are none ----
        {
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 500) && a.caught_up(1, t0 + 1000));
            ASSERT_TRUE(a.lost(1, t0 + 20000));
            ASSERT_FALSE(a.flapping(1, t0 + 20000));                                      // two
            ASSERT_EQ(a.vote_subject(t0 + 20000), 255);
            ASSERT_TRUE(a.returning(1, t0 + 20500) && a.caught_up(1, t0 + 21000));
            ASSERT_TRUE(a.lost(1, t0 + 40000));                                           // the third, 40 s after the first
            ASSERT_TRUE(a.flapping(1, t0 + 40000));
            ASSERT_TRUE(a.away_ms(1, t0 + 40000) < 30000u);                               // (it is nowhere near 30 s away in all: the flap alone puts it to the vote)
            ASSERT_EQ(a.vote_subject(t0 + 40000), 1);
            ASSERT_EQ(a.presence_for(0, t0 + 40000).vote_seat, 1);
            Attendance b(cfg);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(1, t0) && b.returning(1, t0 + 500) && b.caught_up(1, t0 + 1000) && b.lost(1, t0 + 30000) && b.returning(1, t0 + 30500) && b.caught_up(1, t0 + 31000));
            ASSERT_TRUE(b.lost(1, t0 + 60001));                                           // the third is 60,001 ms after the first
            ASSERT_FALSE(b.flapping(1, t0 + 60001));
            ASSERT_EQ(b.vote_subject(t0 + 60001), 255);
            Attendance c(cfg);
            c.seat_humans(0b0111, t0);
            ASSERT_TRUE(c.lost(1, t0) && c.returning(1, t0 + 500) && c.caught_up(1, t0 + 1000) && c.lost(1, t0 + 30000) && c.returning(1, t0 + 30500) && c.caught_up(1, t0 + 31000));
            ASSERT_TRUE(c.lost(1, t0 + 60000));                                           // ... and 60,000 ms: within the minute
            ASSERT_TRUE(c.flapping(1, t0 + 60000));
        }
        // ---- the vote stays open across the returns of the seat, in every state, with its votes ----
        {
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            for (uint32_t i = 0; i < 3; ++i) {
                const uint32_t at = t0 + i * 10000;
                ASSERT_TRUE(a.lost(1, at));
                if (i < 2) ASSERT_TRUE(a.returning(1, at + 500) && a.caught_up(1, at + 1000));
            }
            const uint32_t now = t0 + 20000;                                              // the third loss: Absent
            ASSERT_EQ(a.state(1), S::Absent);
            ASSERT_EQ(a.vote_subject(now), 1);
            ASSERT_TRUE(a.vote(0, 1, true, now));
            ASSERT_EQ(a.votes_for_continue(1), 1u);
            ASSERT_TRUE(a.returning(1, now + 500));                                       // catching up: still the subject, and its votes stay
            ASSERT_EQ(a.state(1), S::CatchingUp);
            ASSERT_EQ(a.vote_subject(now + 500), 1);
            ASSERT_EQ(a.votes_for_continue(1), 1u);
            ASSERT_EQ(a.presence_for(2, now + 500).vote_seat, 1);
            ASSERT_EQ(a.presence_for(2, now + 500).votes_continue, 1);
            ASSERT_TRUE(a.caught_up(1, now + 1000));                                      // back and Present: still the subject, with the vote that was cast
            ASSERT_EQ(a.state(1), S::Present);
            ASSERT_EQ(a.vote_subject(now + 1000), 1);
            ASSERT_EQ(a.votes_for_continue(1), 1u);
            const PresenceMsg p = a.presence_for(2, now + 1000);
            ASSERT_TRUE(p.missing.empty() && p.vote_seat == 1 && p.votes_continue == 1 && p.voters == 2 && p.your_vote == 0);   // (the voters are the others: seat 1 does not vote on itself)
            ASSERT_EQ(a.presence_for(0, now + 1000).your_vote, 2);                        // seat 0's own choice is still its own
            ASSERT_FALSE(a.vote(1, 1, true, now + 1000));                                 // a seat does not vote on its own case
            ASSERT_EQ(a.connected_humans(), 3);
            ASSERT_EQ(a.connected_humans(1), 2);
            std::vector<uint8_t> dropped = a.update(now + 1000);                          // 1 of 2: not more than half
            ASSERT_TRUE(dropped.empty() && a.state(1) == S::Present);
            ASSERT_TRUE(a.vote(2, 1, true, now + 1500));                                  // 2 of 2: the seat is dropped though it is connected
            dropped = a.update(now + 1500);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1);
            ASSERT_EQ(a.state(1), S::Dropped);
            ASSERT_EQ(a.drops_by_vote(), 1u);
            ASSERT_FALSE(a.paused());
            ASSERT_EQ(a.vote_subject(now + 1500), 255);
            ASSERT_EQ(a.votes_for_continue(1), 0u);
        }
        // ---- the vote closes by itself after a minute without a loss, with the votes that were cast; a vote of the other kind (away long enough) is not closed by it ----
        {
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            for (uint32_t i = 0; i < 3; ++i) {
                const uint32_t at = t0 + i * 10000;
                ASSERT_TRUE(a.lost(1, at) && a.returning(1, at + 500) && a.caught_up(1, at + 1000));
            }
            ASSERT_TRUE(a.flapping(1, t0 + 20000 + 59999));
            ASSERT_TRUE(a.vote(0, 1, true, t0 + 30000));
            ASSERT_EQ(a.vote_subject(t0 + 20000 + 59999), 1);
            ASSERT_TRUE(a.update(t0 + 20000 + 59999).empty());
            ASSERT_FALSE(a.flapping(1, t0 + 20000 + 60000));                              // 60 s after the last loss
            ASSERT_EQ(a.vote_subject(t0 + 20000 + 60000), 255);
            ASSERT_TRUE(a.update(t0 + 20000 + 60000).empty());
            ASSERT_EQ(a.votes_for_continue(1), 0u);                                       // the vote that was cast is gone with the vote
            ASSERT_EQ(a.presence_for(0, t0 + 100000).vote_seat, 255);
            // a seat that flapped and is away for long is a subject for that too: the votes stay when the flap runs out
            Attendance b(cfg);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(1, t0) && b.returning(1, t0 + 500) && b.caught_up(1, t0 + 1000) && b.lost(1, t0 + 5000) && b.returning(1, t0 + 5500) && b.caught_up(1, t0 + 6000) && b.lost(1, t0 + 10000));
            ASSERT_TRUE(b.vote(0, 1, true, t0 + 11000));
            ASSERT_TRUE(b.update(t0 + 80000).empty());                                    // the flap is over (70 s), the seat has been away 70 s: the vote goes on
            ASSERT_FALSE(b.flapping(1, t0 + 80000));
            ASSERT_EQ(b.vote_subject(t0 + 80000), 1);
            ASSERT_EQ(b.votes_for_continue(1), 1u);
        }
        // ---- a second window that takes over a seat that is still Present is a loss, like a cut link ----
        {
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 500) && a.caught_up(1, t0 + 1000));
            ASSERT_TRUE(a.returning(1, t0 + 10000));                                      // a Hello for a seat whose link the host thinks is alive
            ASSERT_FALSE(a.flapping(1, t0 + 10000));
            ASSERT_TRUE(a.caught_up(1, t0 + 10500) && a.returning(1, t0 + 20000));        // ... a third time within the minute
            ASSERT_TRUE(a.flapping(1, t0 + 20000));
            ASSERT_EQ(a.vote_subject(t0 + 20000), 1);
        }
        // ---- the cap: every pause counts at least 5 s, so a connection that flaps reaches it long before it has cost that much time ----
        {
            Attendance::Config cap = cfg;
            cap.max_pause_ms = 40000;
            Attendance a(cap);
            a.seat_humans(0b0111, t0);
            for (uint32_t i = 0; i < 8; ++i) {
                const uint32_t at = t0 + i * 1500;
                ASSERT_FALSE(a.cap_reached(at));
                ASSERT_TRUE(a.update(at).empty());
                ASSERT_TRUE(a.lost(1, at) && a.returning(1, at + 400) && a.caught_up(1, at + 800));
            }
            ASSERT_EQ(a.held_ms(t0 + 12000), 8u * 800u);                                  // the match really waited 6.4 s ...
            ASSERT_EQ(a.pause_ms(t0 + 12000), 8u * 5000u);                                // ... and the cap counted 40 s
            ASSERT_TRUE(a.cap_reached(t0 + 12000) && a.cap_s(t0 + 12000) == 0);
            ASSERT_TRUE(a.lost(1, t0 + 12000));                                           // the next loss finds no budget: dropped at once
            const std::vector<uint8_t> dropped = a.update(t0 + 12000);
            ASSERT_TRUE(dropped.size() == 1 && dropped[0] == 1 && a.drops_by_cap() == 1u);
        }
        // ---- the same from other points of the clock ----
        for (const uint32_t start : {0u, 0xFFFFFC00u, 0xFFFF0000u, 0x7FFFF000u}) {
            Attendance a(cfg);
            a.seat_humans(0b0111, start);
            for (uint32_t i = 0; i < 3; ++i) ASSERT_TRUE(a.lost(1, start + i * 20000) && (i == 2 || (a.returning(1, start + i * 20000 + 500) && a.caught_up(1, start + i * 20000 + 1000))));
            ASSERT_TRUE(a.flapping(1, start + 40000) && a.vote_subject(start + 40000) == 1);
            ASSERT_TRUE(a.flapping(1, start + 99999) && !a.flapping(1, start + 100000));
        }
    } TEST_END();

    TEST_CASE("N2.78 Attendance: The Resume Countdown: After A Pause Of 3 s Or More (A Seat Back And Verified, Or Dropped By A Vote Or The Cap) The Match Is Held For The Countdown, Which Is No Pause (Not The Cap's, Not Any Catch-Up's, But The Match's Own Held Time); A Shorter Pause Resumes At Once; A Loss Cancels It And One Follows That Pause; 0 Is Off") {
        using S = Attendance::State;
        const uint32_t t0 = 5000000;
        Attendance::Config cfg;
        cfg.resume_countdown_ms = 10000;
        cfg.rejoin_attempts = Attendance::kMaxRejoinAttempts;
        ASSERT_TRUE(Attendance::Config{}.resume_countdown_ms == 0 && kResumeCountdownMs == 10000 && kResumeMinPauseMs == 3000 && Attendance::Config{}.resume_min_pause_ms == 3000);   // (the library's default is none, a room's is ten seconds)
        {   // a pause of 20 s ends with a seat that is back: ten seconds of countdown
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_FALSE(a.counting_down(t0) || a.paused());
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 15000));
            ASSERT_FALSE(a.counting_down(t0 + 16000));                                    // (it is a pause: no countdown yet)
            ASSERT_TRUE(a.caught_up(1, t0 + 20000));
            ASSERT_FALSE(a.paused());
            ASSERT_TRUE(a.counting_down(t0 + 20000));
            ASSERT_EQ(a.resume_s(t0 + 20000), 10);
            ASSERT_EQ(a.resume_s(t0 + 20001), 10);                                        // 9,999 ms: rounded up
            ASSERT_EQ(a.resume_s(t0 + 20999), 10);                                        // 9,001 ms: still ten
            ASSERT_EQ(a.resume_s(t0 + 21000), 9);                                         // 9,000 ms: nine
            ASSERT_EQ(a.resume_s(t0 + 21001), 9);
            ASSERT_EQ(a.resume_s(t0 + 29001), 1);
            ASSERT_EQ(a.resume_s(t0 + 29999), 1);
            ASSERT_TRUE(a.counting_down(t0 + 29999));
            ASSERT_FALSE(a.counting_down(t0 + 30000));                                    // ten seconds to the millisecond
            ASSERT_EQ(a.resume_s(t0 + 30000), 0);
            ASSERT_EQ(a.resume_s(t0 + 99999), 0);
            // it is no pause: the cap and the seat's away time do not count it, the match's held time does
            ASSERT_EQ(a.pause_ms(t0 + 20000), 20000u);
            ASSERT_EQ(a.pause_ms(t0 + 25000), 20000u);
            ASSERT_EQ(a.pause_ms(t0 + 99999), 20000u);
            ASSERT_EQ(a.cap_left_ms(t0 + 25000), cfg.max_pause_ms - 20000u);
            ASSERT_EQ(a.away_ms(1, t0 + 99999), 20000u);
            ASSERT_EQ(a.held_ms(t0 + 20000), 20000u);
            ASSERT_EQ(a.held_ms(t0 + 25000), 25000u);
            ASSERT_EQ(a.held_ms(t0 + 30000), 30000u);
            ASSERT_EQ(a.held_ms(t0 + 99999), 30000u);                                     // (the time that the match did not run: 20 s paused, 10 s counted down)
            // the presence says it, to everybody, with nobody missing
            const PresenceMsg p = a.presence_for(0, t0 + 23500);
            ASSERT_TRUE(p.missing.empty() && p.resume_s == 7 && p.cap_s == a.cap_s(t0 + 23500));
            PresenceMsg back;
            ASSERT_TRUE(decode(encode(p), back) && back.resume_s == 7);
            ASSERT_EQ(a.presence_for(255, t0 + 31000).resume_s, 0);
        }
        {   // a pause shorter than 3 s is a blip: no countdown (2,999 ms none, 3,000 ms one)
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 1000) && a.caught_up(1, t0 + 2999));
            ASSERT_FALSE(a.counting_down(t0 + 2999) || a.counting_down(t0 + 3000) || a.paused());
            ASSERT_EQ(a.held_ms(t0 + 3000), 2999u);
            ASSERT_TRUE(a.lost(1, t0 + 10000) && a.returning(1, t0 + 10500) && a.caught_up(1, t0 + 13000));
            ASSERT_TRUE(a.counting_down(t0 + 13000));
            ASSERT_EQ(a.resume_s(t0 + 13000), 10);
            // the pause of two seats is one pause: 5 s here, though neither seat was away 3 s on its own after the other came back
            Attendance b(cfg);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(1, t0) && b.lost(2, t0 + 2000) && b.returning(1, t0 + 2500) && b.caught_up(1, t0 + 3000));
            ASSERT_FALSE(b.counting_down(t0 + 3000));                                     // seat 2 is away: the pause goes on
            ASSERT_TRUE(b.returning(2, t0 + 4000) && b.caught_up(2, t0 + 5000));
            ASSERT_TRUE(b.counting_down(t0 + 5000));
        }
        {   // a loss during the countdown cancels it (the pause rules apply); a countdown follows that pause, however short it is
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 4000) && a.caught_up(1, t0 + 5000));
            ASSERT_TRUE(a.counting_down(t0 + 8000));
            ASSERT_TRUE(a.lost(2, t0 + 8000));                                            // 3 s into the countdown
            ASSERT_TRUE(a.paused());
            ASSERT_FALSE(a.counting_down(t0 + 8000) || a.counting_down(t0 + 9000));
            ASSERT_EQ(a.resume_s(t0 + 9000), 0);
            ASSERT_EQ(a.held_ms(t0 + 9000), 5000u + 3000u + 1000u);                       // 5 s paused, 3 s counted, 1 s paused
            ASSERT_TRUE(a.returning(2, t0 + 8200) && a.caught_up(2, t0 + 8400));          // the new pause was 400 ms: shorter than 3 s ...
            ASSERT_TRUE(a.counting_down(t0 + 8400));                                      // ... and the countdown comes all the same: 10 s from now
            ASSERT_EQ(a.resume_s(t0 + 8400), 10);
            ASSERT_FALSE(a.counting_down(t0 + 18400));
            ASSERT_EQ(a.pause_ms(t0 + 18400), 5000u + 5000u);                             // (400 ms counted 5 s: the pauses count at least 5 s for the cap)
            ASSERT_EQ(a.held_ms(t0 + 18400), 5000u + 3000u + 400u + 10000u);
            // and the countdown after the second pause does not owe a third
            ASSERT_FALSE(a.counting_down(t0 + 40000));
            ASSERT_TRUE(a.lost(2, t0 + 40000) && a.returning(2, t0 + 40100) && a.caught_up(2, t0 + 40200));
            ASSERT_FALSE(a.counting_down(t0 + 40200));                                    // a plain blip again
        }
        {   // a seat that returns and is lost again within the countdown: a loss in the very ms of the countdown's end is the same as one after it
            Attendance a(cfg);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 4000) && a.caught_up(1, t0 + 5000));
            ASSERT_TRUE(a.lost(2, t0 + 15000));                                           // the countdown is over (10 s): this is an ordinary loss
            ASSERT_TRUE(a.returning(2, t0 + 15100) && a.caught_up(2, t0 + 15200));
            ASSERT_FALSE(a.counting_down(t0 + 15200));
        }
        {   // the countdown after a vote that was won, and after the cap
            Attendance a(cfg);
            a.seat_humans(0b1111, t0);
            ASSERT_TRUE(a.lost(3, t0));
            ASSERT_TRUE(a.vote(0, 3, true, t0 + 31000) && a.vote(1, 3, true, t0 + 31000));
            ASSERT_EQ(a.update(t0 + 31000).size(), 1u);
            ASSERT_FALSE(a.paused());
            ASSERT_TRUE(a.counting_down(t0 + 31000));                                     // the match goes on after ten seconds, without seat 3
            ASSERT_EQ(a.resume_s(t0 + 31000), 10);
            ASSERT_EQ(a.pause_ms(t0 + 41000), 31000u);
            ASSERT_FALSE(a.counting_down(t0 + 41000));
            Attendance::Config capped = cfg;
            capped.max_pause_ms = 60000;
            Attendance b(capped);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(2, t0));
            ASSERT_EQ(b.update(t0 + 60000).size(), 1u);
            ASSERT_FALSE(b.paused());
            ASSERT_TRUE(b.counting_down(t0 + 60000));
            ASSERT_EQ(b.pause_ms(t0 + 65000), 60000u);                                    // (the cap is spent: the countdown is not on it)
            ASSERT_EQ(b.held_ms(t0 + 65000), 65000u);
            // a pause that ends with a seat that LEFT (a Leave while the other one is away) is a pause that ended too
            Attendance c(cfg);
            c.seat_humans(0b0111, t0);
            ASSERT_TRUE(c.lost(1, t0) && c.dropped(1, t0 + 8000));
            ASSERT_TRUE(c.counting_down(t0 + 8000));
        }
        {   // 0 is off; a short or a long setting is what it says; the match begins again: nothing is left
            Attendance::Config off = cfg;
            off.resume_countdown_ms = 0;
            Attendance a(off);
            a.seat_humans(0b0111, t0);
            ASSERT_TRUE(a.lost(1, t0) && a.returning(1, t0 + 5000) && a.caught_up(1, t0 + 20000));
            ASSERT_FALSE(a.counting_down(t0 + 20000) || a.paused());
            ASSERT_EQ(a.resume_s(t0 + 20000), 0);
            ASSERT_EQ(a.held_ms(t0 + 20000), 20000u);
            Attendance::Config sixty = cfg;
            sixty.resume_countdown_ms = 60000;
            Attendance b(sixty);
            b.seat_humans(0b0111, t0);
            ASSERT_TRUE(b.lost(1, t0) && b.returning(1, t0 + 5000) && b.caught_up(1, t0 + 20000));
            ASSERT_EQ(b.resume_s(t0 + 20000), 60);
            ASSERT_EQ(b.resume_s(t0 + 79001), 1);
            ASSERT_FALSE(b.counting_down(t0 + 80000));
            Attendance c(cfg);
            c.seat_humans(0b0111, t0);
            ASSERT_TRUE(c.lost(1, t0) && c.returning(1, t0 + 5000) && c.caught_up(1, t0 + 20000));
            c.seat_humans(0b0111, t0 + 22000);
            ASSERT_FALSE(c.counting_down(t0 + 22000) || c.paused());
            ASSERT_EQ(c.held_ms(t0 + 22500), 0u);
            ASSERT_TRUE(c.state(1) == S::Present);
        }
        {   // the same from other points of the clock
            for (const uint32_t start : {0u, 0xFFFFF000u, 0xFFFFFFFFu, 0x80000000u - 1u}) {
                Attendance a(cfg);
                a.seat_humans(0b0111, start);
                ASSERT_TRUE(a.lost(1, start) && a.returning(1, start + 4000) && a.caught_up(1, start + 5000));
                ASSERT_TRUE(a.counting_down(start + 14999) && a.resume_s(start + 14999) == 1 && !a.counting_down(start + 15000));
                ASSERT_EQ(a.held_ms(start + 20000), 15000u);
            }
        }
    } TEST_END();
    TEST_CASE("N2.79 Lock-Step Runner: A Match That The Server Holds (Presence: A Pause, The Countdown After It) Is No Stall Of The Link: The Turns In Hand Run, Then The Runner Waits Without A Stall (Nothing Is Told To The Jitter Buffer, No \"Waiting For The Other Players\", Pauses Of 40 ms To 30 s) And Collects The Buffer Again; What Was Read Before The Pause Is Forgotten When It Ends, The Buffer Itself Is Not Reset") {
        auto empty_turn = [](uint32_t n) {
            TurnMsg t;
            t.turn = n;
            return t;
        };
        // One continuous stream on a runner of its own: turn k is sealed at k * 50 ms (and arrives `delay(k)` later, never before an earlier turn), every 10 ms the runner has its frame; no turn
        // is sealed for `pause_ms` after turn `pause_after_turn`; when `held` the runner is told (set_held) when the pause begins and when it ends. The start of the match and the end of the
        // stream (nothing follows the last turn: that is a stall, and no business of this) are not looked at.
        struct Run {
            uint32_t buffer_max{0};
            uint32_t buffer_at_end{0};
            uint32_t stalled_most_ms{0};
            bool stalled_ever{false};
            bool stalled_in_pause{false};                                             // the runner was stalled at a moment inside the pause (after the turns in hand had run)
            bool rebuilding_at_release{false};
            size_t executed{0};
            uint32_t jitter_ms_after{0};
        };
        const auto stream = [&](LockstepRunner& runner, uint32_t turns, uint32_t pause_after_turn, uint32_t pause_ms, bool held, const std::function<uint32_t(uint32_t)>& delay) {
            Run out;
            std::vector<std::pair<uint32_t, uint32_t>> arrivals;                      // (arrival, turn)
            uint32_t last = 0;
            uint32_t seal = 0;
            for (uint32_t i = 0; i < turns; ++i) {
                if (i == pause_after_turn) seal += pause_ms;                          // nothing is sealed for the pause
                last = std::max(last, seal + delay(i));
                arrivals.push_back({last, i});
                seal += kTurnMs;
            }
            const uint32_t pause_begin = pause_after_turn * kTurnMs;
            const uint32_t pause_end = pause_begin + pause_ms;
            size_t next = 0;
            bool announced = false;
            bool released = false;
            for (uint32_t now = 10; now <= seal + 400; now += 10) {
                if (held && !announced && now >= pause_begin) {                       // the Presence reaches the machine right behind the last turn that was sealed
                    runner.set_held(true);
                    announced = true;
                }
                if (held && announced && !released && now >= pause_end) {              // ... and the one that ends the pause right before the first turn after it
                    out.rebuilding_at_release = runner.rebuilding();
                    runner.set_held(false);
                    released = true;
                }
                while (next < arrivals.size() && arrivals[next].first <= now) {
                    runner.on_turn(empty_turn(arrivals[next].second));
                    ++next;
                }
                out.executed += runner.update(10).size();
                out.buffer_max = std::max(out.buffer_max, runner.buffer_turns());
                if (now > pause_begin + 150 && now < pause_end && runner.stalled()) out.stalled_in_pause = true;
                if (now < 500 || now > seal - kTurnMs) continue;
                out.stalled_most_ms = std::max(out.stalled_most_ms, runner.stalled_ms());
                out.stalled_ever = out.stalled_ever || runner.stalled();
            }
            out.buffer_at_end = runner.buffer_turns();
            out.jitter_ms_after = runner.jitter().lateness_ms();
            return out;
        };
        const auto none = [](uint32_t) { return 0u; };
        // ---- the control: a pause that the runner does not know of is a stall, and a short one grows the buffer (this is what the others' windows did) ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1, 2 * 3600 * 1000);
            LockstepRunner steady(sim);
            const Run quiet = stream(steady, 800, 100000, 0, false, none);             // no pause: a steady link keeps one turn
            ASSERT_TRUE(quiet.buffer_max == 1 && !quiet.stalled_ever);
            sim::SimulationEngine other;
            build_world(other, 1, 2 * 3600 * 1000);
            LockstepRunner runner(other);
            const Run cut = stream(runner, 800, 600, 130, false, none);                // 30 s of a steady link and a pause of 130 ms
            ASSERT_TRUE(cut.stalled_ever);
            ASSERT_TRUE(cut.stalled_in_pause || cut.buffer_max >= 2);
            ASSERT_TRUE(cut.buffer_max >= 2);                                          // the buffer grew for a pause that was nobody's link
        }
        // ---- held: the same pauses, the runner told: no stall, no growth, whatever the length of the pause ----
        for (const uint32_t pause : {40u, 130u, 210u, 600u, 1200u, 5000u, 30000u}) {
            sim::SimulationEngine sim;
            build_world(sim, 1, 2 * 3600 * 1000);
            LockstepRunner runner(sim);
            const Run cut = stream(runner, 800, 600, pause, true, none);
            ASSERT_FALSE(cut.stalled_ever || cut.stalled_in_pause);                   // no "Waiting for the other players..." ever, not even in a pause of half a minute
            ASSERT_EQ(cut.stalled_most_ms, 0u);
            ASSERT_EQ(cut.buffer_max, 1u);                                            // the jitter buffer was told nothing: it is where it was
            ASSERT_EQ(cut.buffer_at_end, 1u);
            ASSERT_TRUE(cut.rebuilding_at_release || pause <= 130);                   // (a pause that the buffer's turns outlast never ran dry: nothing to collect)
            ASSERT_EQ(cut.executed, size_t{800});                                     // every turn ran
            ASSERT_EQ(runner.next_turn_to_execute(), 800u);
            ASSERT_FALSE(runner.held());
            ASSERT_EQ(cut.jitter_ms_after, 0u);                                       // a pause is not lateness: the later turns were measured against each other
        }
        // ---- a rough link keeps what it learned across a pause (the pause forgets the samples, not the buffer) ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1, 2 * 3600 * 1000);
            LockstepRunner runner(sim);
            Lcg rng(79);
            const Run cut = stream(runner, 900, 600, 5000, true, [&](uint32_t) { return rng.below(130); });
            ASSERT_FALSE(cut.stalled_in_pause);                                       // (the link is rough: it stalls now and then, as it should; the pause is no stall)
            ASSERT_TRUE(cut.buffer_max >= 3);                                         // a link of 0 - 130 ms of jitter needs three or four turns ...
            ASSERT_TRUE(cut.buffer_at_end >= 3 && cut.buffer_at_end <= 4);            // ... and still does after the pause: it was not reset to one by it
            ASSERT_EQ(cut.executed, size_t{900});
        }
        // ---- the details: turns that were queued before the Presence run; a turn that arrives while the match is held runs; held twice or released twice changes nothing ----
        {
            sim::SimulationEngine sim;
            build_world(sim, 1);
            LockstepRunner runner(sim);
            ASSERT_FALSE(runner.held());
            runner.set_held(false);                                                    // nothing to release
            ASSERT_FALSE(runner.held());
            for (uint32_t k = 0; k < 5; ++k) ASSERT_TRUE(runner.on_turn(empty_turn(k)));
            ASSERT_EQ(runner.update(0).size(), 1u);                                    // it starts
            runner.set_held(true);
            runner.set_held(true);
            ASSERT_TRUE(runner.held());
            size_t ran = 1;
            for (int i = 0; i < 30; ++i) ran += runner.update(10).size();              // 300 ms: the four turns that were queued run (the server sealed them)
            ASSERT_EQ(ran, size_t{5});
            for (int i = 0; i < 400; ++i) runner.update(50);                           // 20 s of nothing: not a stall, not a message
            ASSERT_FALSE(runner.stalled());
            ASSERT_EQ(runner.stalled_ms(), 0u);
            ASSERT_EQ(runner.buffer_turns(), 1u);
            ASSERT_TRUE(runner.rebuilding());                                          // collecting the buffer for the turns that follow
            ASSERT_TRUE(runner.on_turn(empty_turn(5)));                                // (a turn that was in flight: it is not run before the buffer is full again, as after a start)
            ASSERT_TRUE(runner.update(50).empty());
            ASSERT_TRUE(runner.on_turn(empty_turn(6)));
            ASSERT_EQ(runner.update(50).size(), 1u);
            runner.set_held(false);
            runner.set_held(false);
            ASSERT_FALSE(runner.held());
            ASSERT_FALSE(runner.stalled());
            ASSERT_EQ(runner.buffer_turns(), 1u);
        }
    } TEST_END();
}
