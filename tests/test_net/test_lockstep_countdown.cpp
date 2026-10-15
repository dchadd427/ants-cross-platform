// Protocol 21, the countdown before the first turn (docs/MATCH_COUNTDOWN.md): the host side of the lock-step. A referee that holds the first turn 30 s (HostSession::Config::start_delay_ms,
// which a server's room sets for a match of two or more people) seals it exactly then and nothing before, lets chat through meanwhile, drops commands as protocol 12 does, ends the wait with a pause as
// it does for the 5 s dialog, and plays the match that follows exactly as a match with the 5 s dialog does (the simulation and its turns do not know there was a countdown).
#include "lockstep_fixtures.hpp"

namespace {

constexpr uint32_t kCountdownMs = kMatchCountdownSeconds * 1000u;

// The orders of the seats that a test sends at chosen times (every seat has ants from the shared world)
Command order_of(const Ids& ids, uint8_t seat, int16_t x) { return cmd(CommandType::GroupMove, seat, 255, x, 12, ids.ants[seat]); }

// The turns that a host sealed: the number of commands in each, and the seats that issued them
std::vector<uint8_t> issuers_of(HostSession& host) {
    std::vector<uint8_t> out;
    for (uint32_t t = 0; t < host.turns_sealed(); ++t) {
        const TurnMsg* turn = host.runner().logged_turn(t);
        if (turn == nullptr) continue;
        for (const Command& c : turn->commands) out.push_back(c.issuer);
    }
    return out;
}

struct Played {
    uint32_t turns{0};
    uint64_t state{0};
    std::vector<std::vector<uint8_t>> sealed;          // every sealed turn as the host sends it
    bool desync{false};
    bool equal{false};
};

// A referee match of three seats whose first turn is held `delay_ms`; the seats give orders at fixed times AFTER the first seal (so that what they say does not depend on how long the wait was)
Played play_match(uint32_t delay_ms) {
    HostSession::Config hc;
    hc.start_delay_ms = delay_ms;
    ServerMatch m(33, 3, {40, 0}, hc);                                          // no jitter: the same arrivals every time
    m.run(delay_ms + 6000, false, [&](uint32_t now) {
        if (now <= delay_ms || now - delay_ms > 4000 || (now - delay_ms) % 400 != 0) return;
        const int16_t x = static_cast<int16_t>(10 + (now - delay_ms) / 400);
        for (uint8_t seat = 0; seat < 3; ++seat) m.clients[seat]->submit(order_of(m.ids, seat, x));
    });
    m.settle();
    Played out;
    out.turns = m.host->turns_sealed();
    out.state = m.referee.state_hash().total;
    for (uint32_t t = 0; t < out.turns; ++t) {
        const TurnMsg* turn = m.host->runner().logged_turn(t);
        if (turn != nullptr) out.sealed.push_back(encode(*turn));
    }
    out.desync = !m.host->desyncs().empty();
    out.equal = m.all_equal();
    return out;
}

}  // namespace

void run_protocol21_tests() {
    TEST_CASE("S2.17 Protocol 21, The Countdown Of A Dedicated Server's Room: The Referee Seals The First Turn Exactly 30 s After start() And Nothing Before It, Chat Is Relayed During The Wait, A Command In It Is Dropped Without A Violation, The Match Then Runs In Step") {
        ASSERT_EQ(kMatchCountdownSeconds, 30u);                                     // the owner's 30 s
        ASSERT_EQ(kMaxCountdownSeconds, 60u);
        ASSERT_EQ(kCountdownMinPeople, 2u);                                         // a room of one person keeps the 5 s dialog
        ASSERT_TRUE(kMatchCountdownSeconds * 1000u > kMatchStartDelayMs);
        HostSession::Config hc;
        hc.start_delay_ms = kCountdownMs;
        ServerMatch m(31, 3, {40, 0}, hc);
        std::vector<std::pair<uint32_t, ChatMsg>> heard[3];
        for (uint8_t p = 0; p < 3; ++p) m.clients[p]->set_on_chat([&heard, &m, p](const ChatMsg& c) { heard[p].emplace_back(m.now, c); });
        bool said = false;
        bool early_order = false;
        bool late_order = false;
        uint32_t first_seal = 0;
        uint32_t sealed_when_quiet = 99;
        bool quiet = true;                                                          // the seconds before the first turn: nothing sealed, run, waited for or reported
        m.run(kCountdownMs + 4000, false, [&](uint32_t now) {
            if (first_seal == 0 && m.host->turns_sealed() >= 1) first_seal = now - 10;       // (the pass that sealed it ran in the step before this one)
            if (first_seal == 0) {
                sealed_when_quiet = m.host->turns_sealed();
                quiet = quiet && !m.host->paused() && !m.host->waiting() && m.host->lagging_mask() == 0 && m.host->desyncs().empty() && m.referee.current_tick() == 0;
                for (auto& c : m.clients) quiet = quiet && c->runner().queued() == 0 && c->runner().next_turn_to_execute() == 0 && !c->runner().stalled() && c->lagging_seat() == 255;
            }
            if (now == 12000) said = m.clients[0]->chat("hi all", false);
            if (now == 20000) early_order = m.clients[1]->submit(order_of(m.ids, 1, 12));
            if (now == kCountdownMs + 1000) late_order = m.clients[1]->submit(order_of(m.ids, 1, 14));
        });
        ASSERT_EQ(first_seal, kCountdownMs);                                        // sealed on the very pass that is 30 s after start()
        ASSERT_EQ(sealed_when_quiet, 0u);
        ASSERT_TRUE(quiet);
        ASSERT_TRUE(said);
        for (uint8_t p : {uint8_t{1}, uint8_t{2}}) {                                // the others hear it at once (a link of 40 ms there and 40 ms back), thirteen seconds before the match
            ASSERT_EQ(heard[p].size(), size_t{1});
            ASSERT_EQ(heard[p][0].second.sender, 0);                                 // the connection speaks, not the payload
            ASSERT_EQ(heard[p][0].second.text, std::string("hi all"));
            ASSERT_TRUE(heard[p][0].first >= 12000 && heard[p][0].first <= 12200);
        }
        ASSERT_TRUE(early_order && late_order);                                     // both were sent; the one in the wait was dropped, the one after the first seal was sealed
        ASSERT_TRUE(issuers_of(*m.host) == std::vector<uint8_t>{1});                // one command in the whole match: the second one
        ASSERT_EQ(m.host->violations(1), 0u);                                        // a dropped command is no offence (protocol 12's rule, unchanged)
        for (uint8_t seat = 0; seat < 3; ++seat) ASSERT_TRUE(m.host->client_present(seat));
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        for (auto& c : m.clients) ASSERT_FALSE(c->desynced());
        ASSERT_TRUE(m.all_equal());
        ASSERT_TRUE(m.referee.current_tick() > 60);                                  // the match runs
    } TEST_END();

    TEST_CASE("S2.18 Protocol 21, The Countdown Is Not Part Of The Simulation: A Match With A 30 s Wait And One With The 5 s Dialog, Given The Same Orders At The Same Moments After Their First Turn, Seal The Same Turns, Byte For Byte, And End In The Same State") {
        const Played five = play_match(kMatchStartDelayMs);
        const Played thirty = play_match(kCountdownMs);
        ASSERT_TRUE(five.turns > 100 && thirty.turns > 100);
        ASSERT_EQ(five.turns, thirty.turns);
        ASSERT_TRUE(five.sealed == thirty.sealed);                                  // every turn: the commands in the canonical order, the same bytes (a recording is the turns, so the replay is the same too)
        ASSERT_EQ(five.state, thirty.state);                                        // and the state hash that every machine compares
        ASSERT_FALSE(five.desync || thirty.desync);
        ASSERT_TRUE(five.equal && thirty.equal);
        bool any_command = false;
        for (const std::vector<uint8_t>& turn : five.sealed) any_command = any_command || turn.size() > 8;       // (a turn with no command is a few bytes: this test did give orders)
        ASSERT_TRUE(any_command);
    } TEST_END();

    TEST_CASE("S2.20 Protocol 21, Soak: Matches Of 2 To 4 Seats With A Wait Of 2 To 60 s On Links With Jitter And Spikes, Their Seats Giving Orders And Chatting From The First Second, Seal The First Turn At The Wait's End On The Dot And Not Before, Count No Violation, Never Desync And End In Equal States") {
        const uint32_t waits_s[] = {2, 5, 9, 13, 21, 30, 38, 47, 60};
        uint32_t matches = 0;
        for (uint32_t i = 0; i < 18; ++i) {
            const uint32_t wait_ms = waits_s[i % 9] * 1000u;
            HostSession::Config hc;
            hc.start_delay_ms = wait_ms;
            const LoopbackNetwork::Link link{15 + 10 * (i % 5), 4 * (i % 6), i % 3 == 0 ? 90u : 0u, i % 3 == 0 ? 25u : 0u};          // (every third link has spikes)
            const uint8_t seats = static_cast<uint8_t>(2 + i % 3);
            ServerMatch m(200 + i, seats, link, hc);
            uint32_t first_seal = 0;
            uint32_t said = 0;
            m.run(wait_ms + 7000, true, [&](uint32_t now) {
                if (first_seal == 0 && m.host->turns_sealed() >= 1) first_seal = now - 10;       // (the pass that sealed it ran in the step before this one)
                if (first_seal == 0) ASSERT_EQ(m.host->turns_sealed(), 0u);
                if (now % 650 == 0 && m.clients[(now / 650) % seats]->chat("soak " + std::to_string(now), (now / 650) % 4 == 3)) ++said;
            });
            ASSERT_EQ(first_seal, wait_ms);                                              // on the very pass that is `wait_ms` after start(), whatever the link did
            ASSERT_TRUE(said > 5);
            m.settle();
            for (uint8_t seat = 0; seat < seats; ++seat) {
                ASSERT_EQ(m.host->violations(seat), 0u);                                  // the scripted orders of the wait were dropped, not held against anybody
                ASSERT_TRUE(m.host->client_present(seat));
                ASSERT_FALSE(m.clients[seat]->desynced());
            }
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_TRUE(m.all_equal());
            ASSERT_TRUE(m.referee.current_tick() > 20);
            ++matches;
        }
        ASSERT_EQ(matches, 18u);
    } TEST_END();

    TEST_CASE("S2.19 Protocol 21, A Seat Lost In The Countdown Pauses The Match For Everybody As It Did In The 5 s Dialog: Nothing Is Sealed Past The 30 s While The Seat Is Away, The Resume Countdown Follows Its Return, And The Match Then Runs In Step") {
        HoldOptions o;
        o.link = {30, 0};
        o.seed = 35;
        o.host.start_delay_ms = kCountdownMs;
        o.host.attendance.resume_countdown_ms = kResumeCountdownMs;               // (a server's room has it: the library's default is none)
        HoldMatch m(3, o);
        m.auto_reconnect[1] = false;                                                // seat 1's machine does not come back by itself (until the test says)
        m.run(10000, false);
        ASSERT_EQ(m.sealed(), 0u);
        ASSERT_FALSE(m.host->paused());
        m.cut(1);
        m.run(1000, false);
        ASSERT_TRUE(m.host->paused());                                              // a seat is missing: the match is held, as in any other pause
        m.run(25000, false);                                                        // 36 s from the start: the countdown would be over, the match stays held
        ASSERT_EQ(m.sealed(), 0u);
        ASSERT_TRUE(m.host->paused());
        m.auto_reconnect[1] = true;                                                 // the machine comes back with its key
        const uint32_t back = m.now;
        ASSERT_TRUE(m.until([&]() { return m.sealed() >= 1; }, 60000));
        ASSERT_TRUE(m.now - back >= kResumeCountdownMs - 1000);                      // the resume countdown (10 s) that follows a pause ran before the first turn
        m.run(4000, false);
        ASSERT_TRUE(m.sealed() > 40);
        m.settle();
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_TRUE(m.all_equal());
        {   // a pause that ends well before the 30 s: the schedule slid with it, so the first turn comes at the end of the resume countdown, not at 30 s (the rest of the count is not kept: documented)
            HoldOptions q;
            q.link = {30, 0};
            q.seed = 36;
            q.host.start_delay_ms = kCountdownMs;
            q.host.attendance.resume_countdown_ms = kResumeCountdownMs;
            HoldMatch n(3, q);
            n.auto_reconnect[1] = false;
            n.run(5000, false);
            n.cut(1);
            n.run(4000, false);
            ASSERT_TRUE(n.host->paused());
            n.auto_reconnect[1] = true;
            const uint32_t returned = n.now;
            ASSERT_TRUE(n.until([&]() { return n.sealed() >= 1; }, 60000));
            ASSERT_TRUE(n.now < kCountdownMs);                                      // about 9 s + the way back + 10 s: before the 30 s that were left
            ASSERT_TRUE(n.now - returned >= kResumeCountdownMs - 1000);
            n.run(3000, false);
            n.settle();
            ASSERT_TRUE(n.host->desyncs().empty() && n.all_equal());
        }
    } TEST_END();
}
