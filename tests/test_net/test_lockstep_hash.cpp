// Tests of the lock-step network core: a dedicated server's referee and the seats whose reported state differs from its own (docs/NETWORK_PORT.md, "Sessions" and "Room"; session.hpp, "A seat whose state
// differs from the referee's"; Sequencer::judge_hashes). A seat that differs is dropped as one that quits is, it alone is told, and the others go on; the match is stopped, as it always was, only when no seat
// at the table agrees with the referee.
//   S2.40 - S2.43  the rule and a whole match: a machine that holds another state; the verdict on the reports of a turn, whichever came first (a table of cases), the reports that are not kept (a turn that is not
//                  hashed, one that was never sealed, a repeat, a seat that is gone); a machine whose state is sound and whose Hash messages are not; a report that comes ahead of the machine's own
//   S2.44 - S2.47  the match is stopped, as before, when nobody agrees with the referee (a referee that is wrong, one seat alone); the verdict waits for a seat that has not executed the turn yet, and a seat that
//                  leaves does not take the verdict with it
//   S2.48 - S2.50  the verdict is the same on links of any speed, reports that mean nothing change nothing, several seats at once and a seat that is alone with the referee
//   S2.51 - S2.53  a seat that held its place (the key of a dropped seat is told so, a machine that comes back and sends hashes, a link that is no seat's), the seat of a computer player
// The Quit of the dropped seat in the turn stream and what it makes of the results, the history and the replay are tested in front of real rooms (test_server S3.250 - S3.257). The mutants of all of it:
// tests/data/state_hash_mutants.json (tools/mutate.py), every one caught.
#include "lockstep_fixtures.hpp"

#include "../common/hash_skew.hpp"

namespace {

using Kind = HashVerdict::Kind;

// A dedicated server's sequencer with seats 0 .. 2 at the table and `turns` turns sealed; the three states the cases use (0 is the referee's), and a seat's report of one of them
struct JudgeRig {
    Sequencer seq;
    sim::StateHash state[3];
    explicit JudgeRig(uint32_t turns = 100) {
        seq.set_host_player(kNoSeat);
        for (uint8_t p = 0; p < 3; ++p) seq.set_active(p, true);
        for (uint32_t i = 0; i < turns; ++i) seq.seal();
        for (uint64_t k = 0; k < 3; ++k) {
            state[k].total = 0x1000 * (k + 1);
            state[k].ants = 7 + k;
        }
    }
    void report(uint8_t seat, uint32_t turn, int which) { seq.on_hash(seat, turn, state[which]); }
    void executed(uint8_t seat, uint32_t turn) { seq.on_ack(seat, turn + 3); }       // the seat has run the turn (and a few after it): a client sends its hash of a turn before its acknowledgement
};

uint8_t seats_of(const HashVerdict& v) {
    uint8_t mask = 0;
    for (const DesyncMsg& d : v.seats) mask = static_cast<uint8_t>(mask | (1u << d.player));
    return mask;
}

// The first tick at which a machine had dropped `seat`, noted while the match runs
struct DropWatch {
    uint64_t tick{0};
    void look(const sim::SimulationEngine& s, uint8_t seat) {
        if (tick == 0 && s.is_player_dropped(seat)) tick = s.current_tick();
    }
};

}  // namespace

void run_hash_tests() {
    TEST_CASE("S2.40 Dedicated Server: A Seat Whose State Differs From The Referee's Is Dropped And The Others Go On: It Alone Is Told, The Match Is Not Stopped, Its Ants Die On Every Machine, And The Rest Stay Equal To The Referee") {
        ServerMatch m(1, 4, {40, 10});
        m.run(3000);
        ASSERT_TRUE(m.host->desyncs().empty());
        m.sims[3]->get_unit(m.ids.ants[3][1]).hp = 3;                                // a hit point that no command explains
        const uint32_t before = m.now;
        m.run(2000, false);
        ASSERT_TRUE(m.now - before <= 2000);                                         // named within a second of play, with the subsystem
        ASSERT_FALSE(m.host->frozen());                                              // the match goes on
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_EQ(m.host->diverged_drops(), 1u);
        ASSERT_FALSE(m.host->client_present(3));                                     // the seat is out ...
        for (uint8_t p = 0; p < 3; ++p) ASSERT_TRUE(m.host->client_present(p) && !m.clients[p]->desynced());     // ... the others are not told and stay
        ASSERT_TRUE(m.clients[3]->desynced());                                       // it alone is told
        const DesyncMsg d = m.clients[3]->desync();
        ASSERT_EQ(d.player, 3);
        ASSERT_TRUE(d.peer.ants != d.host.ants && d.peer.grid == d.host.grid && d.peer.players == d.host.players);       // only the ants differ
        const uint32_t sealed = m.host->turns_sealed();
        m.run(8000);                                                                 // the three who are left play on for eight more seconds
        ASSERT_TRUE(m.host->turns_sealed() >= sealed + 150);
        ASSERT_FALSE(m.host->frozen());
        m.settle();
        ASSERT_TRUE(m.referee.is_player_dropped(3));
        for (uint8_t p = 0; p < 3; ++p) {
            ASSERT_FALSE(m.referee.is_player_dropped(p));
            ASSERT_TRUE(m.sims[p]->is_player_dropped(3) && !m.sims[p]->is_player_dropped(p));      // the drop travelled in the turn stream: every machine dropped the team
            ASSERT_TRUE(m.sims[p]->state_hash() == m.referee.state_hash());
            size_t ants_of_3 = 0;
            for (const auto& a : m.sims[p]->get_world_state().ants) ants_of_3 += a.player_id == 3;
            ASSERT_EQ(ants_of_3, 0u);                                                // the team's ants died
        }
    } TEST_END();

    TEST_CASE("S2.41 Dedicated Server: The Referee's Verdict On The Reports Of A Turn (Sequencer::judge_hashes): A Seat That Differs Is Dropped When A Seat At The Table Agrees With The Referee, The Verdict Waits While Nobody Does And A Seat May Still Say, And Fails Only When Nobody Does And Nobody Can; The Same Whichever Came First") {
        struct Case {
            const char* what;
            int report[3];              // by seat: -1 no report, 0 the referee's state, 1 and 2 other states
            uint8_t executed;           // the seats that have run the turn
            uint8_t seated;             // the seats at the table
            Kind kind;
            uint8_t seats;              // Drop, Fail: the seats named
        };
        const Case cases[] = {
            {"one differs, two agree", {0, 0, 1}, 7, 7, Kind::Drop, 4},
            {"the only report differs, the others may still say", {1, -1, -1}, 1, 7, Kind::Nothing, 0},
            {"one differs, one agrees, the third has not run the turn", {1, 0, -1}, 3, 7, Kind::Drop, 1},
            {"everybody reports another state", {1, 1, 1}, 7, 7, Kind::Fail, 7},
            {"one differs, the others ran the turn and said nothing", {1, -1, -1}, 7, 7, Kind::Fail, 1},
            {"two differ, the third has not run the turn", {1, 1, -1}, 3, 7, Kind::Nothing, 0},
            {"two differ, the third agrees", {1, 1, 0}, 7, 7, Kind::Drop, 3},
            {"the seat that differs is not at the table", {1, 0, 0}, 7, 6, Kind::Nothing, 0},
            {"a seat alone at the table", {1, -1, -1}, 1, 1, Kind::Fail, 1},
            {"two other states, one agrees", {2, 1, 0}, 7, 7, Kind::Drop, 3},
            {"all agree", {0, 0, 0}, 7, 7, Kind::Nothing, 0},
            {"the seat that agrees is not at the table, the third has not run the turn", {1, 0, -1}, 3, 5, Kind::Nothing, 0},
            {"the seat that agrees is not at the table, the third ran the turn and said nothing", {1, 0, -1}, 7, 5, Kind::Fail, 1},
            {"nobody has reported", {-1, -1, -1}, 7, 7, Kind::Nothing, 0},
        };
        for (const Case& c : cases) {
            for (int referee_first = 0; referee_first < 2; ++referee_first) {
                JudgeRig r;
                if (referee_first != 0) r.seq.on_referee_hash(19, r.state[0]);
                for (uint8_t p = 0; p < 3; ++p) {
                    if (c.report[p] >= 0) ASSERT_TRUE(r.seq.on_hash(p, 19, r.state[c.report[p]]).empty());       // (a dedicated server's sequencer returns nothing: judge_hashes answers)
                }
                if (referee_first == 0) r.seq.on_referee_hash(19, r.state[0]);
                for (uint8_t p = 0; p < 3; ++p) {
                    if ((c.executed & (1u << p)) != 0) r.executed(p, 19);
                }
                const HashVerdict v = r.seq.judge_hashes(c.seated);
                const bool ok = v.kind == c.kind && seats_of(v) == c.seats;
                if (!ok) std::cout << "\n    case: " << c.what << ", " << (referee_first != 0 ? "the referee first" : "the seats first") << ": kind " << static_cast<int>(v.kind) << ", seats " << static_cast<int>(seats_of(v)) << "\n";
                ASSERT_TRUE(ok);
                for (const DesyncMsg& d : v.seats) {
                    ASSERT_TRUE(d.turn == 19 && d.host == r.state[0] && d.peer == r.state[c.report[d.player]] && d.peer.ants == r.state[c.report[d.player]].ants);      // each names the turn and both states
                }
                ASSERT_TRUE(r.seq.judge_hashes(c.seated).kind == Kind::Nothing);       // a turn is answered once (and a verdict that waits has no other answer meanwhile)
            }
        }
    } TEST_END();

    TEST_CASE("S2.42 Dedicated Server: A Machine Whose State Is Sound But Whose Hash Messages Say Another State Is Dropped Just Like One Whose State Differs: Alone, And The Others Go On") {
        ServerMatch m(1, 4, {40, 10});
        ants_test::HashSkew skew(m.client_ends[3]);
        m.clients[3]->set_connection(&skew);                                         // seat 3 plays and acknowledges as any other, and reports another state
        m.run(4000);
        ASSERT_TRUE(skew.skewed >= 1);
        ASSERT_FALSE(m.host->frozen());
        ASSERT_TRUE(m.host->desyncs().empty());
        ASSERT_EQ(m.host->diverged_drops(), 1u);
        ASSERT_FALSE(m.host->client_present(3));
        for (uint8_t p = 0; p < 3; ++p) ASSERT_TRUE(m.host->client_present(p) && !m.clients[p]->desynced());
        ASSERT_TRUE(m.clients[3]->desynced() && m.clients[3]->desync().player == 3);
        ASSERT_TRUE(m.clients[3]->desync().peer.total == (m.clients[3]->desync().host.total ^ 0x5AA55AA5ull));       // (what it reported, and what the referee holds)
        m.run(4000);
        m.settle();
        ASSERT_TRUE(m.referee.is_player_dropped(3));
        for (uint8_t p = 0; p < 3; ++p) ASSERT_TRUE(!m.referee.is_player_dropped(p) && m.sims[p]->state_hash() == m.referee.state_hash());
        ASSERT_TRUE(m.sims[3]->state_hash() == m.referee.state_hash() || m.sims[3]->current_tick() < m.referee.current_tick());     // (its own machine was sound: it only ran fewer turns)
    } TEST_END();

    TEST_CASE("S2.43 Dedicated Server: A Report That Comes Ahead Of The Machine's Own (A Hash Message For A Turn That The Seat Has Not Run Yet) Is The Seat's Report Of That Turn: The First Report Of A Turn Is Final, So The Seat Is Dropped On It, And The Honest Report That Follows Cannot Take It Back") {
        for (int variant = 0; variant < 2; ++variant) {
            ServerMatch m(3 + static_cast<uint32_t>(variant), 4, {40, 10});
            bool sent = false;
            m.run(4000, true, [&](uint32_t) {
                if (!sent && m.host->turns_sealed() >= 20) {                         // turn 19 is sealed: a machine runs it a few turns later, after the link has brought it
                    sent = true;
                    HashMsg h;
                    h.turn = 19;
                    h.hash.total = uint64_t{0xBADBAD} + static_cast<uint64_t>(variant);
                    m.client_ends[1]->send(encode(h));                               // seat 1 says a state that is not the referee's, and then goes on and reports its own
                }
            });
            ASSERT_TRUE(sent);
            ASSERT_FALSE(m.host->frozen());
            ASSERT_TRUE(m.host->desyncs().empty());
            ASSERT_EQ(m.host->diverged_drops(), 1u);
            ASSERT_FALSE(m.host->client_present(1));
            ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2) && m.host->client_present(3));
            ASSERT_TRUE(m.clients[1]->desynced() && m.clients[1]->desync().turn == 19);
            ASSERT_FALSE(m.clients[0]->desynced() || m.clients[2]->desynced() || m.clients[3]->desynced());
        }
        {   // the same report, long after: its turn is forgotten (everybody has passed it), the report is not kept for anything
            ServerMatch m(5, 4, {40, 10});
            m.run(6000);
            for (uint32_t turn : {19u, 39u, 59u}) {
                HashMsg h;
                h.turn = turn;
                h.hash.total = 0xBADull;
                m.client_ends[2]->send(encode(h));
            }
            m.run(3000);
            ASSERT_TRUE(m.host->client_present(2) && m.host->desyncs().empty() && m.host->diverged_drops() == 0 && !m.host->frozen());
            m.settle();
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("S2.44 Dedicated Server: When Every Seat At The Table Differs From The Referee The Match Is Stopped As It Always Was: The Referee May Be The One That Is Wrong, So Nobody Is Dropped, Every Seat That Differs Is Told And Named, And Nothing Is Sealed Any More") {
        ServerMatch m(1, 4, {40, 10});
        m.run(3000);
        m.referee.get_unit(m.ids.ants[0][1]).hp = 3;                                 // the referee's own state is the odd one out
        m.run(3000, false);
        ASSERT_TRUE(m.host->frozen());
        ASSERT_EQ(m.host->desyncs().size(), size_t{4});                              // a seat for each, at the same turn, each with the referee's hash and its own
        uint8_t named = 0;
        for (const DesyncMsg& d : m.host->desyncs()) {
            named = static_cast<uint8_t>(named | (1u << d.player));
            ASSERT_TRUE(d.turn == m.host->desyncs()[0].turn && d.host != d.peer);
        }
        ASSERT_EQ(named, 15);
        ASSERT_EQ(m.host->diverged_drops(), 0u);
        for (uint8_t p = 0; p < 4; ++p) ASSERT_TRUE(m.clients[p]->desynced() && !m.referee.is_player_dropped(p) && m.host->client_present(p));       // everybody is told (the room closes the links), nobody dropped
        const uint32_t sealed = m.host->turns_sealed();
        m.run(2000, false);
        ASSERT_EQ(m.host->turns_sealed(), sealed);                                   // frozen for good
    } TEST_END();

    TEST_CASE("S2.45 Dedicated Server: A Seat Alone At The Table Cannot Be Told From A Wrong Referee: Its State Differing Stops The Match As It Always Did; Two Seats Of Which Only One Differs Do Not (The Other Vouches For The Referee)") {
        {
            ServerMatch m(1, 1, {20, 5});
            m.run(1500);
            m.sims[0]->get_unit(m.ids.ants[0][1]).hp = 3;
            m.run(3000, false);
            ASSERT_TRUE(m.host->frozen());
            ASSERT_TRUE(m.host->desyncs().size() == 1 && m.host->desyncs()[0].player == 0);
            ASSERT_EQ(m.host->diverged_drops(), 0u);
            ASSERT_TRUE(m.clients[0]->desynced() && m.host->client_present(0));
        }
        {
            ServerMatch m(1, 2, {20, 5});
            m.run(1500);
            m.sims[0]->get_unit(m.ids.ants[0][1]).hp = 3;
            m.run(3000, false);
            ASSERT_FALSE(m.host->frozen());
            ASSERT_EQ(m.host->diverged_drops(), 1u);
            ASSERT_TRUE(!m.host->client_present(0) && m.host->client_present(1) && !m.clients[1]->desynced() && m.clients[0]->desynced());
        }
    } TEST_END();

    TEST_CASE("S2.46 Dedicated Server: The Verdict Waits For A Seat That Has Not Run The Turn Yet (Two Windows That Stand Still For Six Seconds): Nobody Is Dropped Meanwhile, The Match Is Not Stopped, And When They Catch Up And Agree With The Referee The Seat That Differs Is Dropped; If They Leave Instead Nobody Is Left To Vouch For The Referee And The Match Is Stopped") {
        {
            ServerMatch m(1, 3, {30, 5});
            m.run(500);
            m.frozen_mask = 0x03;                                                    // seats 0 and 1: their windows do not run
            m.sims[2]->get_unit(m.ids.ants[2][1]).hp = 3;
            m.run(6000, false);
            ASSERT_FALSE(m.host->frozen());
            ASSERT_TRUE(m.host->desyncs().empty() && m.host->diverged_drops() == 0);
            ASSERT_TRUE(m.host->client_present(2) && !m.clients[2]->desynced());     // nobody has said which state is right
            m.frozen_mask = 0;                                                       // the windows run again, catch up, and agree with the referee
            m.run(8000, false);
            ASSERT_FALSE(m.host->frozen());
            ASSERT_EQ(m.host->diverged_drops(), 1u);
            ASSERT_TRUE(!m.host->client_present(2) && m.host->client_present(0) && m.host->client_present(1));
            ASSERT_TRUE(m.clients[2]->desynced() && !m.clients[0]->desynced() && !m.clients[1]->desynced());
        }
        {
            ServerMatch m(1, 3, {30, 5});
            m.run(500);
            m.frozen_mask = 0x03;
            m.sims[2]->get_unit(m.ids.ants[2][1]).hp = 3;
            m.run(3000, false);
            ASSERT_FALSE(m.host->frozen());
            m.clients[0]->leave();                                                   // the two who could vouch for the referee leave
            m.clients[1]->leave();
            m.run(1500, false);
            ASSERT_TRUE(m.host->frozen());                                           // nobody at the table agrees with the referee and nobody can: stopped, nobody dropped for it
            ASSERT_TRUE(m.host->desyncs().size() == 1 && m.host->desyncs()[0].player == 2);
            ASSERT_EQ(m.host->diverged_drops(), 0u);
            ASSERT_FALSE(m.host->client_present(0) || m.host->client_present(1));    // (they had left)
        }
    } TEST_END();

    TEST_CASE("S2.47 Dedicated Server: A Seat That Is Behind The Others By Several Seconds But Alive Is Waited For And Then Counts: A Slow Seat That Agrees Vouches For The Referee Like Any Other, And One That Differs Is Dropped Like Any Other") {
        ServerMatch m(1, 3, {40, 10});
        m.run(500);
        m.frozen_mask = 0x01;                                                        // seat 0 stands still for 5 s: it is behind, not gone (the lag policy drops it at 30 s without an acknowledgement)
        m.sims[1]->get_unit(m.ids.ants[1][1]).hp = 3;
        m.sims[2]->get_unit(m.ids.ants[2][1]).hp = 3;                                // seats 1 and 2 differ ...
        m.run(5000, false);
        ASSERT_FALSE(m.host->frozen());
        ASSERT_EQ(m.host->diverged_drops(), 0u);                                     // ... and the only seat that could vouch for the referee has not run the turn yet
        m.frozen_mask = 0;
        m.run(8000, false);
        ASSERT_FALSE(m.host->frozen());
        ASSERT_EQ(m.host->diverged_drops(), 2u);
        ASSERT_TRUE(m.host->client_present(0) && !m.host->client_present(1) && !m.host->client_present(2));
        ASSERT_TRUE(m.clients[1]->desynced() && m.clients[2]->desynced() && !m.clients[0]->desynced());
    } TEST_END();

    TEST_CASE("S2.48 Dedicated Server: The Verdict Does Not Depend On The Links: On Links From 10 To 150 ms With Jitter And Spikes, Whichever Report Comes First, The Same Seat Is Dropped, At The Same Tick On Every Machine, And Nobody Else Is") {
        const LoopbackNetwork::Link links[] = {{10, 0, 0, 0}, {40, 10, 0, 0}, {80, 60, 0, 0}, {150, 100, 800, 20}, {20, 2, 0, 0}};
        uint32_t seed = 1;
        for (const LoopbackNetwork::Link& link : links) {
            for (uint8_t odd_seat : {uint8_t{0}, uint8_t{3}}) {
                ServerMatch m(seed++, 4, link);
                ants_test::HashSkew skew(m.client_ends[odd_seat]);
                m.clients[odd_seat]->set_connection(&skew);
                DropWatch watch[5];
                m.run(14000, true, [&](uint32_t) {
                    for (uint8_t p = 0; p < 4; ++p) watch[p].look(*m.sims[p], odd_seat);
                    watch[4].look(m.referee, odd_seat);
                });
                ASSERT_FALSE(m.host->frozen());
                ASSERT_TRUE(m.host->desyncs().empty());
                ASSERT_EQ(m.host->diverged_drops(), 1u);
                for (uint8_t p = 0; p < 4; ++p) ASSERT_EQ(m.host->client_present(p), p != odd_seat);
                ASSERT_TRUE(m.clients[odd_seat]->desynced());
                for (uint8_t p = 0; p < 4; ++p) {
                    if (p != odd_seat) ASSERT_FALSE(m.clients[p]->desynced());
                }
                ASSERT_TRUE(watch[4].tick != 0);
                for (uint8_t p = 0; p < 4; ++p) {
                    if (p != odd_seat) ASSERT_EQ(watch[p].tick, watch[4].tick);        // the Quit travelled in the turn stream: the same tick for everybody who got it
                }
            }
        }
    } TEST_END();

    TEST_CASE("S2.49 Dedicated Server: Reports That Mean Nothing Change Nothing: Hashes Of Turns That Nobody Hashes, Of Turns That Were Never Sealed And Of Turns That Everybody Has Passed, And A Wrong One Repeated After The Right One Cost A Seat Neither Its Place Nor The Match") {
        {   // the sequencer: what is not a report of the seat is not kept, and the first report of a turn is the seat's
            JudgeRig r;
            for (uint32_t turn : {0u, 5u, 10u, 18u, 20u, 21u, 38u, 58u, 98u, 100u, 100000u, 0xFFFFFFFFu}) {
                ASSERT_TRUE(r.seq.on_hash(1, turn, r.state[1]).empty());
                r.seq.on_referee_hash(turn, r.state[0]);
            }
            ASSERT_EQ(r.seq.pending_reports(), size_t{0});                           // none of them is kept
            ASSERT_TRUE(r.seq.judge_hashes(7).kind == Kind::Nothing);
            r.report(1, 19, 0);                                                      // the right report first ...
            r.report(1, 19, 1);                                                      // ... and a wrong one after it
            r.report(0, 19, 0);
            r.seq.on_referee_hash(19, r.state[0]);
            ASSERT_TRUE(r.seq.judge_hashes(7).kind == Kind::Nothing);                // the first is the seat's report of the turn
            r.report(2, 39, 1);                                                      // the wrong one first ...
            r.report(2, 39, 0);                                                      // ... and the right one after it
            r.report(0, 39, 0);
            r.seq.on_referee_hash(39, r.state[0]);
            const HashVerdict v = r.seq.judge_hashes(7);
            ASSERT_TRUE(v.kind == Kind::Drop && seats_of(v) == 4 && v.seats[0].turn == 39);
            ASSERT_TRUE(r.seq.judge_hashes(7).kind == Kind::Nothing);                // (a turn is answered once)
        }
        {   // the sequencer: a seat that leaves the table takes its reports with it and cannot report again until it is back; a turn that everybody has passed is forgotten once it is answered
            JudgeRig r;
            r.report(2, 19, 1);
            r.report(0, 19, 0);
            r.seq.set_active(2, false);                                              // seat 2 is gone (dropped, or its connection is lost)
            r.seq.on_referee_hash(19, r.state[0]);
            ASSERT_TRUE(r.seq.judge_hashes(7).kind == Kind::Nothing);                // not at the table: not judged
            ASSERT_TRUE(r.seq.on_hash(2, 39, r.state[1]).empty());                   // a report of a seat that is gone is nobody's
            r.seq.on_referee_hash(39, r.state[0]);
            r.seq.set_active(2, true);                                               // back, level with the sequencer
            ASSERT_TRUE(r.seq.judge_hashes(7).kind == Kind::Nothing);                // what it said before it left is not held against it
            JudgeRig q;
            q.report(1, 19, 1);                                                      // a wrong report and nobody has said which state is right ...
            q.seq.on_referee_hash(19, q.state[0]);
            ASSERT_TRUE(q.seq.judge_hashes(7).kind == Kind::Nothing);
            for (uint8_t p = 0; p < 3; ++p) q.seq.on_ack(p, 70);                     // ... and everybody has passed the turn since, saying nothing
            ASSERT_EQ(q.seq.pending_reports(), size_t{2});                           // the turn is kept until it is answered, though everybody has passed it (the acknowledgements may come in the pass of the report)
            const HashVerdict v = q.seq.judge_hashes(7);
            ASSERT_TRUE(v.kind == Kind::Fail && seats_of(v) == 2);                   // nobody vouches for the referee and nobody can
            q.seq.on_ack(0, 71);                                                     // answered: the next acknowledgement forgets the turn
            ASSERT_EQ(q.seq.pending_reports(), size_t{0});
            ASSERT_TRUE(q.seq.judge_hashes(7).kind == Kind::Nothing);
        }
        {   // on the wire: junk from a seat costs nothing
            ServerMatch m(1, 3, {20, 5});
            m.run(2000);
            HashMsg h;
            h.hash.total = 0xBADull;
            for (uint32_t turn : {0u, 5u, 10u, 18u, 20u, 21u, 38u, 100000u, 0xFFFFFFFFu}) {
                h.turn = turn;
                m.client_ends[0]->send(encode(h));
            }
            m.run(1500);
            m.settle();
            ASSERT_TRUE(m.host->client_present(0) && m.host->desyncs().empty() && m.host->diverged_drops() == 0 && m.host->violations(0) == 0);
            ASSERT_TRUE(m.all_equal());
        }
    } TEST_END();

    TEST_CASE("S2.50 Dedicated Server: Several Seats That Differ At Once Are Dropped Together, Whatever Their Number, As Long As One Seat At The Table Agrees With The Referee: Two Of Four, Three Of Four; The Match Is Not Stopped") {
        for (const uint8_t odd_seats : {uint8_t{0x0C}, uint8_t{0x0E}}) {
            ServerMatch m(1, 4, {40, 10});
            std::vector<std::unique_ptr<ants_test::HashSkew>> skews;
            for (uint8_t p = 0; p < 4; ++p) {
                if ((odd_seats & (1u << p)) == 0) continue;
                skews.push_back(std::make_unique<ants_test::HashSkew>(m.client_ends[p]));
                m.clients[p]->set_connection(skews.back().get());
            }
            m.run(5000);
            ASSERT_FALSE(m.host->frozen());
            ASSERT_TRUE(m.host->desyncs().empty());
            for (uint8_t p = 0; p < 4; ++p) {
                const bool differs = (odd_seats & (1u << p)) != 0;
                ASSERT_EQ(m.host->client_present(p), !differs);
                ASSERT_EQ(m.clients[p]->desynced(), differs);
            }
            ASSERT_TRUE(m.host->client_present(0));                                  // the seat that agrees stays
            const uint32_t sealed = m.host->turns_sealed();
            m.run(2000);
            ASSERT_TRUE(m.host->turns_sealed() >= sealed + 30);                      // the match is sealed on
        }
    } TEST_END();

    TEST_CASE("S2.51 Dedicated Server (Seats Held): The Key Of A Seat That Was Dropped For Its State Is Told \"Dropped\" And Brings Nothing Back, The Seat's Attendance Is Final And No Pause Is Made; The Machine Itself Is Told And Does Not Ask For Its Seat Again") {
        HoldMatch m(3);
        ants_test::HashSkew skew(m.client_ends[2]);
        m.clients[2]->set_connection(&skew);
        m.run(5000);
        ASSERT_EQ(m.host->diverged_drops(), 1u);
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        ASSERT_FALSE(m.host->paused() || m.host->seat_held(2));                      // out for good, not held
        ASSERT_TRUE(m.host->desyncs().empty() && !m.host->frozen());
        ASSERT_TRUE(m.clients[2]->desynced() && m.clients[2]->lost() && !m.clients[2]->reconnecting());
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1));
        auto ends = m.open_link();                                                   // somebody shows its key
        HelloMsg hello;
        hello.name = "Seat 2";
        hello.key = m.keys[2];
        hello.have_turns = m.sealed();
        ends.second->send(encode(hello));
        m.run(400);
        int reason = -1;
        std::vector<uint8_t> msg;
        RejectMsg rejected;
        while (ends.second->poll(msg)) {
            if (peek_type(msg) == MsgType::Reject && decode(msg, rejected)) reason = static_cast<int>(rejected.reason);
        }
        ASSERT_EQ(reason, static_cast<int>(RejectReason::Dropped));
        ASSERT_EQ(m.host->rejoiners(), size_t{0});
        ASSERT_EQ(m.host->attendance().state(2), Attendance::State::Dropped);
        m.run(3000);
        m.settle();
        ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(1) && m.referee.is_player_dropped(2));
        ASSERT_TRUE(m.sims[0]->state_hash() == m.referee.state_hash() && m.sims[1]->state_hash() == m.referee.state_hash());
    } TEST_END();

    TEST_CASE("S2.52 Dedicated Server (Seats Held): Hash Messages From A Link That Is No Seat's Are Ignored: A Link That Says Nothing But A Hash Is Closed By The Door, And A Machine That Is Coming Back (A Hello With A Key, Not Yet Caught Up) Can Send Any Number Of Them Without An Offence, A Verdict Or A Change To The Seat Or The Match") {
        HoldMatch m(3);
        m.run(3000);
        HashMsg h;
        h.hash.total = 0xBADull;
        {   // a link that is no seat's and has not said Hello
            auto stranger = m.open_link();
            h.turn = 19;
            stranger.second->send(encode(h));
            m.run(300);
            ASSERT_FALSE(stranger.first->is_open());
        }
        {   // a machine that is coming back: the match is paused for it, and what it sends before the host has compared its state is no offence and no report
            m.auto_reconnect[1] = false;
            m.cut(1);
            m.run(300);
            ASSERT_TRUE(m.host->paused() && m.host->seat_held(1));
            auto back = m.open_link();
            HelloMsg hello;
            hello.name = "Seat 1";
            hello.key = m.keys[1];
            hello.have_turns = m.sealed();
            back.second->send(encode(hello));
            m.run(300);
            ASSERT_EQ(m.host->rejoiners(), size_t{1});
            for (uint32_t turn = 19; turn < m.sealed(); turn += 20) {
                h.turn = turn;
                back.second->send(encode(h));
                h.turn = turn + 1;                                                   // (and one that is not a hashed turn)
                back.second->send(encode(h));
            }
            m.run(600);
            ASSERT_EQ(m.host->rejoiners(), size_t{1});                               // still coming back: nothing was an offence
            ASSERT_TRUE(m.host->desyncs().empty() && !m.host->frozen() && m.host->diverged_drops() == 0);
            ASSERT_TRUE(m.host->client_present(0) && m.host->client_present(2));
            ASSERT_TRUE(m.host->seat_held(1) && !m.referee.is_player_dropped(1));
            back.second->send(encode_leave());
            m.run(500);
        }
        ASSERT_FALSE(m.host->frozen());
        ASSERT_TRUE(m.host->desyncs().empty() && m.host->diverged_drops() == 0);
    } TEST_END();

    TEST_CASE("S2.53 Dedicated Server: A Computer Player's Seat Sends No Hash And Is Not Judged: With A Person Who Differs And Another Who Agrees, The Person Is Dropped And The Bot's Seat Is Not Touched (It Is Acknowledged By The Host As Before); A Person Who Differs With Only Bots Beside It Stops The Match, As A Seat Alone Does") {
        {
            HoldOptions o;
            o.bot_mask = 0x04;                                                       // seats 0 and 1: people, seat 2: a bot
            HoldMatch m(3, o);
            ants_test::HashSkew skew(m.client_ends[0]);
            m.clients[0]->set_connection(&skew);
            m.run(5000);
            ASSERT_EQ(m.host->diverged_drops(), 1u);
            ASSERT_FALSE(m.host->client_present(0));
            ASSERT_TRUE(m.host->client_present(1) && !m.clients[1]->desynced());
            ASSERT_TRUE(m.host->desyncs().empty() && !m.host->frozen());
            const uint32_t sealed = m.host->turns_sealed();
            m.run(3000);
            ASSERT_TRUE(m.host->turns_sealed() >= sealed + 50);                      // the match is sealed on: the bot's acknowledgements are the host's own
            ASSERT_TRUE(m.referee.is_player_dropped(0) && !m.referee.is_player_dropped(1) && !m.referee.is_player_dropped(2));
        }
        {
            HoldOptions o;
            o.bot_mask = 0x02;                                                       // seat 0: a person, seat 1: a bot
            HoldMatch m(2, o);
            ants_test::HashSkew skew(m.client_ends[0]);
            m.clients[0]->set_connection(&skew);
            m.run(5000);
            ASSERT_TRUE(m.host->frozen());
            ASSERT_TRUE(m.host->desyncs().size() == 1 && m.host->desyncs()[0].player == 0);
            ASSERT_EQ(m.host->diverged_drops(), 0u);
            ASSERT_FALSE(m.referee.is_player_dropped(0) || m.referee.is_player_dropped(1));
        }
    } TEST_END();
}
